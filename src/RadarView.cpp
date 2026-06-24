#include "weatherdesktop/RadarView.h"
#include "weatherdesktop/Theme.h"

#include <imgui.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>

namespace wd {

using json = nlohmann::json;
static const double kPI = 3.14159265358979323846;

static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Zoom bounds. The user may navigate to kMaxZoom, but RainViewer only has real
// radar data through kMaxDataZoom -- past that it returns a "zoom not supported"
// placeholder tile. So we never fetch deeper than kMaxDataZoom and instead
// upscale those tiles to fill the view (lower fidelity, but real data).
static const int kMinZoom = 3;
static const int kMaxDataZoom = 7;
static const int kMaxZoom = 12;

// ---- networking ----------------------------------------------------------
static size_t binWrite(void* p, size_t s, size_t n, void* u) {
    auto* v = static_cast<std::vector<unsigned char>*>(u);
    unsigned char* b = static_cast<unsigned char*>(p);
    v->insert(v->end(), b, b + s * n);
    return s * n;
}
static int binAbort(void* fl, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* a = static_cast<std::atomic<bool>*>(fl);
    return (a && a->load()) ? 1 : 0;
}
static std::vector<unsigned char> httpGetBinary(const std::string& url,
                                                std::atomic<bool>& abort) {
    std::vector<unsigned char> out;
    CURL* c = curl_easy_init();
    if (!c) return out;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, binWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "WeatherDesktop/1.1 (radar)");
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, binAbort);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &abort);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    if (r != CURLE_OK || code < 200 || code >= 300) out.clear();
    return out;
}

// Fetch on a PERSISTENT handle -- the TCP/TLS connection is kept alive across
// tiles to the same host, which is the dominant cost for many small requests.
static std::vector<unsigned char> fetchWith(CURL* c, const std::string& url,
                                            std::atomic<bool>& abort,
                                            long* outCode = nullptr) {
    std::vector<unsigned char> out;
    if (!c) return out;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, binWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "WeatherDesktop/1.1 (radar)");
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, binAbort);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &abort);
    CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (outCode) *outCode = code;
    if (r != CURLE_OK || code < 200 || code >= 300) out.clear();
    return out;
}

// ---- slippy-map projection ----------------------------------------------
static void lonLatToTile(double lon, double lat, int z, double& x, double& y) {
    double n = std::ldexp(1.0, z);  // 2^z, exact + cheaper than pow
    x = (lon + 180.0) / 360.0 * n;
    double latRad = lat * kPI / 180.0;
    y = (1.0 - std::asinh(std::tan(latRad)) / kPI) / 2.0 * n;
}
static void tileToLonLat(double x, double y, int z, double& lon, double& lat) {
    double n = std::ldexp(1.0, z);
    lon = x / n * 360.0 - 180.0;
    lat = std::atan(std::sinh(kPI * (1.0 - 2.0 * y / n))) * 180.0 / kPI;
}

void RadarView::loadBasemap() {
    if (basemapReady_.load()) return;
    auto parseFile = [&](const std::string& file, std::vector<Polyline>& target) {
        std::ifstream f(resourcePath(file), std::ios::binary);
        if (!f) return;
        std::string s((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
        if (s.empty()) return;
        json j;
        try { j = json::parse(s); } catch (...) { return; }
        if (!j.contains("features")) return;
        for (auto& feat : j["features"]) {
            if (!feat.contains("geometry")) continue;
            auto& g = feat["geometry"];
            std::string type = g.value("type", std::string{});
            auto addLine = [&](const json& coords) {
                Polyline pl;
                pl.minLon = 1e9f; pl.maxLon = -1e9f; pl.minLat = 1e9f; pl.maxLat = -1e9f;
                for (auto& c : coords) {
                    if (!c.is_array() || c.size() < 2) continue;
                    Pt p{static_cast<float>(c[0].get<double>()),
                         static_cast<float>(c[1].get<double>())};
                    pl.pts.push_back(p);
                    pl.minLon = std::min(pl.minLon, p.lon);
                    pl.maxLon = std::max(pl.maxLon, p.lon);
                    pl.minLat = std::min(pl.minLat, p.lat);
                    pl.maxLat = std::max(pl.maxLat, p.lat);
                }
                if (pl.pts.size() >= 2) target.push_back(std::move(pl));
            };
            if (type == "LineString") {
                addLine(g["coordinates"]);
            } else if (type == "MultiLineString") {
                for (auto& line : g["coordinates"]) addLine(line);
            }
        }
    };
    parseFile("ne_50m_coastline.json", basemap_);
    parseFile("ne_50m_admin1_lines.json", basemap_);  // states / provinces
    parseFile("ne_50m_admin0_lines.json", borders_);  // country borders
    parseFile("ne_10m_counties.json", counties_);     // US county lines
    parseFile("ne_10m_roads.json", roads_);            // interstates / motorways

    // Populated places (city/town labels).
    {
        std::ifstream f(resourcePath("ne_50m_places.json"), std::ios::binary);
        if (f) {
            std::string s((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
            try {
                json j = json::parse(s);
                if (j.contains("features")) {
                    for (auto& feat : j["features"]) {
                        if (!feat.contains("geometry") || !feat.contains("properties")) continue;
                        auto& g = feat["geometry"];
                        if (g.value("type", std::string{}) != "Point") continue;
                        auto& co = g["coordinates"];
                        if (!co.is_array() || co.size() < 2) continue;
                        auto& pr = feat["properties"];
                        Place pl;
                        pl.lon = static_cast<float>(co[0].get<double>());
                        pl.lat = static_cast<float>(co[1].get<double>());
                        pl.rank = pr.value("scalerank", 10);
                        // ASCII name -- the bitmap font has no unicode glyphs.
                        pl.name = pr.value("nameascii", pr.value("name", std::string{}));
                        if (!pl.name.empty()) places_.push_back(std::move(pl));
                    }
                }
            } catch (...) {
            }
        }
    }

    basemapReady_.store(true);
}

// Cache each vector point's tile-pixel position for the given zoom. The mercator
// transform (tan/asinh) for a fixed lon/lat depends only on zoom, not on the pan
// center -- so we do it once per zoom change here, and the per-frame draw becomes
// a plain add (screen = offset + px). Big win now that roads add ~80k points.
void RadarView::buildProjection(int z) {
    auto project = [z](Polyline& pl) {
        pl.px.resize(pl.pts.size());
        pl.py.resize(pl.pts.size());
        for (size_t i = 0; i < pl.pts.size(); ++i) {
            double tx, ty;
            lonLatToTile(pl.pts[i].lon, pl.pts[i].lat, z, tx, ty);
            pl.px[i] = static_cast<float>(tx * 256.0);
            pl.py[i] = static_cast<float>(ty * 256.0);
        }
    };
    for (auto& pl : basemap_) project(pl);
    for (auto& pl : borders_) project(pl);
    for (auto& pl : counties_) project(pl);
    for (auto& pl : roads_) project(pl);
    for (auto& pc : places_) {
        double tx, ty;
        lonLatToTile(pc.lon, pc.lat, z, tx, ty);
        pc.px = static_cast<float>(tx * 256.0);
        pc.py = static_cast<float>(ty * 256.0);
    }
    projZoom_ = z;
}

void RadarView::setWind(bool have, double dirDeg, double speedKmh, double gustKmh,
                        bool mph) {
    haveWind_ = have;
    windDir_ = dirDeg;
    windSpeed_ = speedKmh;
    windGust_ = gustKmh;
    windMph_ = mph;
}

// ---- lifecycle -----------------------------------------------------------
RadarView::~RadarView() {
    stopWorker();  // GL teardown handled by close(); destructor only stops the thread
}

void RadarView::toggle(double lat, double lon) {
    if (open_.load()) { close(); return; }
    {
        std::lock_guard<std::mutex> lk(mx_);
        centerLat_ = lat;
        centerLon_ = lon;
        frames_.clear();
        host_.clear();
        bytes_.clear();
        have_.clear();
        pastCount_ = 0;
    }
    curFrame_ = 0;
    playing_ = false;
    frameInit_ = false;
    frameTimer_ = 0.0f;
    rlUntil_.store(0);
    open_.store(true);
    startWorker();
}

void RadarView::close() {
    open_.store(false);
    stopWorker();
    clearTiles();  // GL thread
    std::lock_guard<std::mutex> lk(mx_);
    frames_.clear();
    bytes_.clear();
    have_.clear();
}

void RadarView::startWorker() {
    stopWorker();
    abort_.store(false);
    running_.store(true);
    worker_ = std::thread(&RadarView::workerLoop, this);
    const int kFetchers = 3;  // keep parallelism modest -- RainViewer 429s on bursts
    for (int i = 0; i < kFetchers; ++i)
        fetchers_.emplace_back(&RadarView::fetcherLoop, this);
}

void RadarView::stopWorker() {
    abort_.store(true);
    running_.store(false);
    jobsCv_.notify_all();  // wake idle fetchers so they can exit
    if (worker_.joinable()) worker_.join();
    for (auto& t : fetchers_)
        if (t.joinable()) t.join();
    fetchers_.clear();
    std::lock_guard<std::mutex> lk(jobsMx_);
    jobs_.clear();
}

void RadarView::clearTiles() {
    for (auto& kv : tex_) freeTexture(kv.second);
    tex_.clear();
    std::lock_guard<std::mutex> lk(jobsMx_);
    jobs_.clear();
}

// ---- worker: load basemap once, fetch maps JSON + the visible tiles ------
void RadarView::workerLoop() {
    loadBasemap();  // parse the bundled Natural Earth GeoJSON once (off the UI thread)

    auto lastMaps = std::chrono::steady_clock::now() -
                    std::chrono::minutes(10);

    while (running_.load()) {
        // (Re)fetch the frame list every few minutes (or if we have none yet).
        bool needMaps;
        {
            std::lock_guard<std::mutex> lk(mx_);
            needMaps = frames_.empty();
        }
        auto now = std::chrono::steady_clock::now();
        if (needMaps ||
            std::chrono::duration_cast<std::chrono::minutes>(now - lastMaps).count() >= 3) {
            auto raw = httpGetBinary(
                "https://api.rainviewer.com/public/weather-maps.json", abort_);
            lastMaps = now;
            if (!raw.empty()) {
                try {
                    json j = json::parse(std::string(raw.begin(), raw.end()));
                    std::string host = j.value("host", std::string{});
                    std::vector<Frame> fr;
                    int pastN = 0;
                    if (j.contains("radar")) {
                        auto& rad = j["radar"];
                        for (const char* key : {"past", "nowcast"}) {
                            if (!rad.contains(key)) continue;
                            for (auto& f : rad[key]) {
                                Frame ff;
                                ff.path = f.value("path", std::string{});
                                ff.nowcast = std::string(key) == "nowcast";
                                ff.time = f.value("time", 0LL);
                                if (!ff.path.empty()) {
                                    fr.push_back(ff);
                                    if (!ff.nowcast) ++pastN;
                                }
                            }
                        }
                    }
                    std::lock_guard<std::mutex> lk(mx_);
                    // RainViewer's frame window slides (~every 10 min): a given
                    // index maps to a different timestamp after a regeneration.
                    // Only invalidate the tile cache when the paths actually
                    // change, so we refresh once per update -- not every poll.
                    bool changed = host != host_ || fr.size() != frames_.size();
                    if (!changed)
                        for (size_t i = 0; i < fr.size(); ++i)
                            if (fr[i].path != frames_[i].path) { changed = true; break; }
                    host_ = host;
                    frames_ = std::move(fr);
                    pastCount_ = pastN;
                    if (changed) {
                        have_.clear();
                        bytes_.clear();
                        texDirty_.store(true);  // GL thread drops the old textures
                    }
                    // Position on the most-recent observed frame *before* the
                    // first tile sweep, so we fetch the frame that's displayed.
                    if (!frameInit_) {
                        curFrame_ = pastN > 0 ? pastN - 1
                                              : static_cast<int>(frames_.size()) - 1;
                        if (curFrame_ < 0) curFrame_ = 0;
                        frameInit_ = true;
                    }
                    if (curFrame_ >= static_cast<int>(frames_.size()))
                        curFrame_ = static_cast<int>(frames_.size()) - 1;
                    if (curFrame_ < 0) curFrame_ = 0;
                } catch (...) {
                }
            }
        }

        // Enqueue any tiles the current view is missing (all frames, displayed
        // frame first); the fetcher pool drains the queue in parallel.
        enqueueNeeded();

        // Idle briefly between planning passes (interruptible by close()).
        for (int i = 0; i < 6 && running_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void RadarView::enqueueNeeded() {
    int z, minx, maxx, miny, maxy, n, cur;
    {
        std::lock_guard<std::mutex> lk(mx_);
        if (host_.empty() || frames_.empty()) return;
        z = zoom_;
        minx = vMinX_; maxx = vMaxX_; miny = vMinY_; maxy = vMaxY_;
        n = static_cast<int>(frames_.size());
        cur = curFrame_;
    }
    if (maxx < minx || maxy < miny) return;
    if (cur < 0 || cur >= n) cur = 0;

    // Never fetch past the data zoom -- map the view bounds down to z7 tiles and
    // let render() upscale them. (At deep zoom the view covers a fraction of one
    // z7 tile, so this is also far fewer requests.)
    int fz = std::min(z, kMaxDataZoom);
    int dzf = z - fz;
    minx >>= dzf; maxx >>= dzf; miny >>= dzf; maxy >>= dzf;
    int maxTile = 1 << fz;

    // Fetch the displayed frame eagerly; only prefetch the *other* frames while
    // animating. Paused (the common case) we request ~one screen of tiles, not
    // 13 screens at once -- that burst is what trips RainViewer's 429 limit.
    std::vector<int> order;
    order.reserve(n);
    order.push_back(cur);
    if (playing_)
        for (int d = 1; d < n; ++d) order.push_back((cur + d) % n);

    std::vector<TileKey> toQueue;
    {
        std::lock_guard<std::mutex> lk(mx_);
        for (int fi : order)
            for (int tx = minx; tx <= maxx; ++tx)
                for (int ty = miny; ty <= maxy; ++ty) {
                    if (tx < 0 || ty < 0 || tx >= maxTile || ty >= maxTile) continue;
                    TileKey k{fi, fz, tx, ty};
                    if (have_.count(k)) continue;  // already requested or fetched
                    have_[k] = true;               // mark requested
                    toQueue.push_back(k);
                }
    }
    if (!toQueue.empty()) {
        {
            std::lock_guard<std::mutex> lk(jobsMx_);
            for (const auto& k : toQueue) jobs_.push_back(k);
        }
        jobsCv_.notify_all();
    }
}

void RadarView::fetcherLoop() {
    CURL* c = curl_easy_init();
    while (running_.load()) {
        TileKey k;
        {
            std::unique_lock<std::mutex> lk(jobsMx_);
            jobsCv_.wait(lk, [&] { return !jobs_.empty() || !running_.load(); });
            if (!running_.load()) break;
            k = jobs_.front();
            jobs_.pop_front();
        }
        // Back off while rate-limited: wait out the 429 cooldown before fetching.
        while (running_.load()) {
            long long wait = rlUntil_.load() - nowMs();
            if (wait <= 0) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<long long>(wait, 150)));
        }
        if (!running_.load()) break;

        std::string url;
        {
            std::lock_guard<std::mutex> lk(mx_);
            if (host_.empty() || k.f < 0 || k.f >= static_cast<int>(frames_.size()))
                continue;
            char path[256];
            std::snprintf(path, sizeof path, "%s%s/256/%d/%d/%d/4/1_1.png",
                          host_.c_str(), frames_[k.f].path.c_str(), k.z, k.x, k.y);
            url = path;
        }
        long code = 0;
        auto png = fetchWith(c, url, abort_, &code);
        std::lock_guard<std::mutex> lk(mx_);
        if (!png.empty()) {
            bytes_[k] = std::move(png);   // awaiting GL upload
        } else {
            if (code == 429)              // rate limited -> pause all fetchers a bit
                rlUntil_.store(nowMs() + 3000);
            have_.erase(k);               // allow a retry once the backoff clears
        }
    }
    if (c) curl_easy_cleanup(c);
}

// Human label for a radar frame: local clock time + how far it is from now,
// e.g. "2:30 PM  (now)", "2:10 PM  (-20 min)", "3:00 PM  (+30 min forecast)".
static std::string frameTimeLabel(long long t, bool nowcast, std::time_t now) {
    if (t <= 0) return nowcast ? "forecast" : "";
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &tt);
#else
    lt = *std::localtime(&tt);
#endif
    char clock[16];
    std::strftime(clock, sizeof clock, "%I:%M %p", &lt);
    const char* c = clock;
    if (c[0] == '0') ++c;  // "02:30 PM" -> "2:30 PM"
    long long dmin = (t - static_cast<long long>(now)) / 60;
    char rel[32];
    if (dmin >= -2 && dmin <= 2) std::snprintf(rel, sizeof rel, "now");
    else if (dmin < 0) std::snprintf(rel, sizeof rel, "-%lld min", -dmin);
    else std::snprintf(rel, sizeof rel, "+%lld min%s", dmin, nowcast ? " forecast" : "");
    return std::string(c) + "  (" + rel + ")";
}

// 16-point compass for the meteorological "from" direction.
static const char* windCompass(double deg) {
    static const char* d[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                              "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    int i = static_cast<int>(std::floor(std::fmod(deg + 11.25, 360.0) / 22.5));
    if (i < 0) i += 16;
    return d[i % 16];
}

// ---- render (GL/main thread) --------------------------------------------
void RadarView::render() {
    if (!open_.load()) return;

    ImGui::SetNextWindowSize(ImVec2(720, 660), ImGuiCond_FirstUseEver);
    bool open = true;
    if (!ImGui::Begin("Weather Radar", &open)) {
        ImGui::End();
        if (!open) close();
        return;
    }

    int frameCount, pastN;
    bool haveData, hostEmpty;
    double clat, clon;
    int z;
    std::vector<long long> ftimes;
    std::vector<char> fnow;
    {
        std::lock_guard<std::mutex> lk(mx_);
        frameCount = static_cast<int>(frames_.size());
        pastN = pastCount_;
        hostEmpty = host_.empty();
        haveData = !hostEmpty && frameCount > 0;
        clat = centerLat_; clon = centerLon_; z = zoom_;
        ftimes.reserve(frames_.size());
        fnow.reserve(frames_.size());
        for (const auto& f : frames_) { ftimes.push_back(f.time); fnow.push_back(f.nowcast ? 1 : 0); }
    }

    // Once frames arrive, jump to the most-recent observed frame (paused).
    if (!frameInit_ && frameCount > 0) {
        curFrame_ = pastN > 0 ? pastN - 1 : frameCount - 1;
        frameInit_ = true;
    }

    // The frame window slid (new radar data): drop the now-stale textures so
    // the freshly-fetched tiles replace them.
    if (texDirty_.exchange(false)) {
        for (auto& kv : tex_) freeTexture(kv.second);
        tex_.clear();
    }

    // Upload a batch of fetched tiles (any frame) -> GL textures each frame, so
    // every frame's tiles are ready for smooth animation. Capped to avoid hitching.
    {
        std::vector<std::pair<TileKey, std::vector<unsigned char>>> batch;
        {
            std::lock_guard<std::mutex> lk(mx_);
            int budget = 16;
            for (auto it = bytes_.begin(); it != bytes_.end() && budget > 0; --budget) {
                batch.emplace_back(it->first, std::move(it->second));
                it = bytes_.erase(it);
            }
        }
        for (auto& b : batch) {
            Texture t = loadTextureFromMemory(b.second.data(), b.second.size());
            if (t.valid()) {
                tex_[b.first] = t;
            } else {
                // Decode failed -> the tile would otherwise be lost forever
                // (have_ still set). Clear it so the next pass refetches.
                std::lock_guard<std::mutex> lk(mx_);
                have_.erase(b.first);
            }
        }
    }

    if (frameCount > 0) {
        if (curFrame_ >= frameCount) curFrame_ = frameCount - 1;
        if (curFrame_ < 0) curFrame_ = 0;
    }

    // --- controls ---
    if (ImGui::Button(playing_ ? "Pause" : "Play")) playing_ = !playing_;
    ImGui::SameLine();
    if (ImGui::Button("<") && frameCount > 0)
        curFrame_ = (curFrame_ - 1 + frameCount) % frameCount;
    ImGui::SameLine();
    if (ImGui::Button(">") && frameCount > 0)
        curFrame_ = (curFrame_ + 1) % frameCount;
    ImGui::SameLine();

    // Prominent readout: what time this radar frame represents.
    std::time_t nowT = std::time(nullptr);
    if (frameCount > 0 && curFrame_ < static_cast<int>(ftimes.size())) {
        bool nc = fnow[curFrame_] != 0;
        long long ft = ftimes[curFrame_];
        long long dmin = ft > 0 ? (ft - static_cast<long long>(nowT)) / 60 : 0;
        ImVec4 col = nc ? theme::Amber
                        : (dmin >= -2 && dmin <= 2 ? theme::Live : theme::Fg);
        ImGui::TextColored(col, "%s", frameTimeLabel(ft, nc, nowT).c_str());
    } else {
        ImGui::TextDisabled("%s", hostEmpty ? "loading..." : "");
    }

    if (frameCount > 0) {
        ImGui::SetNextItemWidth(-1);
        // Slider shows position; the colored readout above names the actual time.
        char fmt[24];
        std::snprintf(fmt, sizeof fmt, "%d / %d", curFrame_ + 1, frameCount);
        ImGui::SliderInt("##frame", &curFrame_, 0, frameCount - 1, fmt);
    }

    // --- canvas ---
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float footer = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 canvas(avail.x, std::max(160.0f, avail.y - footer));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1(p0.x + canvas.x, p0.y + canvas.y);
    ImGui::InvisibleButton("##radarcanvas", canvas);
    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(theme::hex(0x0E1418)));
    dl->PushClipRect(p0, p1, true);

    // Web-Mercator projection for the current center/zoom (native 256px tiles).
    double cx, cy;
    lonLatToTile(clon, clat, z, cx, cy);
    ImVec2 cen(p0.x + canvas.x * 0.5f, p0.y + canvas.y * 0.5f);
    // Affine offset: a cached tile-pixel coord px maps to screen as offX + px.
    double offX = cen.x - cx * 256.0;
    double offY = cen.y - cy * 256.0;

    // Visible tile bounds (hand to the worker so it fetches exactly the view).
    int minTx = static_cast<int>(std::floor(cx + (p0.x - cen.x) / 256.0));
    int maxTx = static_cast<int>(std::floor(cx + (p1.x - cen.x) / 256.0));
    int minTy = static_cast<int>(std::floor(cy + (p0.y - cen.y) / 256.0));
    int maxTy = static_cast<int>(std::floor(cy + (p1.y - cen.y) / 256.0));
    { std::lock_guard<std::mutex> lk(mx_); vMinX_=minTx; vMaxX_=maxTx; vMinY_=minTy; vMaxY_=maxTy; }

    // Bound GPU memory smoothly: when the cache is large, evict only tiles that
    // are off-screen or from a stale zoom (keep all frames of the current view
    // for animation). No mass wipe -> no refetch storm / rate-limit cliff.
    if (tex_.size() > 1200) {
        const int pad = 2;
        std::vector<TileKey> drop;
        for (auto& kv : tex_) {
            const TileKey& k = kv.first;
            // Keep the current zoom in view AND any lower-zoom ancestor of the
            // view (needed for the upscale fallback). Drop deeper/stale zooms.
            bool keep = false;
            if (k.z <= z) {
                int dz = z - k.z;
                keep = k.x >= (minTx >> dz) - pad && k.x <= (maxTx >> dz) + pad &&
                       k.y >= (minTy >> dz) - pad && k.y <= (maxTy >> dz) + pad;
            }
            if (!keep) drop.push_back(k);
        }
        for (const auto& k : drop) { freeTexture(tex_[k]); tex_.erase(k); }
        if (!drop.empty()) {
            std::lock_guard<std::mutex> lk(mx_);
            for (const auto& k : drop) { have_.erase(k); bytes_.erase(k); }
        }
    }

    // View lon/lat box for basemap culling.
    double wLonMin, wLatMax, wLonMax, wLatMin;
    tileToLonLat(minTx, minTy, z, wLonMin, wLatMax);
    tileToLonLat(maxTx + 1, maxTy + 1, z, wLonMax, wLatMin);

    // Radar reflectivity tiles for the current frame, under the coastlines.
    if (haveData) {
        for (int tx = minTx; tx <= maxTx; ++tx) {
            for (int ty = minTy; ty <= maxTy; ++ty) {
                ImVec2 tl(cen.x + static_cast<float>(tx - cx) * 256.0f,
                          cen.y + static_cast<float>(ty - cy) * 256.0f);
                ImVec2 br(tl.x + 256, tl.y + 256);

                // Best available tile: the exact one if present, else the nearest
                // cached lower-zoom ancestor, upscaled into this slot. We never
                // fetch past z7, so deep zoom always lands on an upscaled ancestor
                // (lower fidelity, real data) -- never the "unsupported" blank.
                const Texture* src = nullptr;
                ImVec2 uv0(0, 0), uv1(1, 1);
                bool exact = false;
                TileKey srcKey{curFrame_, z, tx, ty};
                auto ti = tex_.find(srcKey);
                if (ti != tex_.end() && ti->second.valid()) {
                    src = &ti->second; exact = true;
                } else {
                    for (int dz = 1; dz <= z - kMinZoom; ++dz) {
                        int az = z - dz, ax = tx >> dz, ay = ty >> dz;
                        TileKey ak{curFrame_, az, ax, ay};
                        auto ai = tex_.find(ak);
                        if (ai == tex_.end() || !ai->second.valid()) continue;
                        int scale = 1 << dz, sx = tx - (ax << dz), sy = ty - (ay << dz);
                        uv0 = ImVec2(static_cast<float>(sx) / scale, static_cast<float>(sy) / scale);
                        uv1 = ImVec2(static_cast<float>(sx + 1) / scale, static_cast<float>(sy + 1) / scale);
                        src = &ai->second; srcKey = ak;
                        break;
                    }
                }
                if (src)
                    dl->AddImage(static_cast<ImTextureID>(src->id), tl, br, uv0, uv1,
                                 exact ? IM_COL32(255, 255, 255, 240)
                                       : IM_COL32(255, 255, 255, 205));
            }
        }
    }

    // Basemap coastlines / borders + roads (bbox-culled, cached projection).
    if (basemapReady_.load()) {
        if (projZoom_ != z) buildProjection(z);  // recompute only on zoom change

        // Draw a set of polylines in one color, culled to the view box.
        auto drawLines = [&](const std::vector<Polyline>& lines, ImU32 col, float th) {
            for (const auto& pl : lines) {
                if (pl.maxLon < wLonMin || pl.minLon > wLonMax ||
                    pl.maxLat < wLatMin || pl.minLat > wLatMax) continue;
                ImVec2 prev(static_cast<float>(offX + pl.px[0]),
                            static_cast<float>(offY + pl.py[0]));
                for (size_t i = 1; i < pl.pts.size(); ++i) {
                    ImVec2 cur2(static_cast<float>(offX + pl.px[i]),
                                static_cast<float>(offY + pl.py[i]));
                    bool gone = (prev.x < p0.x && cur2.x < p0.x) || (prev.x > p1.x && cur2.x > p1.x) ||
                                (prev.y < p0.y && cur2.y < p0.y) || (prev.y > p1.y && cur2.y > p1.y);
                    if (!gone) dl->AddLine(prev, cur2, col, th);
                    prev = cur2;
                }
            }
        };

        // Layered from faint detail up to prominent borders:
        // counties (zoomed in only) -> coast/states -> country borders -> roads.
        if (z >= 7)
            drawLines(counties_, ImGui::GetColorU32(theme::hex(0x3E4C54)), 1.0f);
        drawLines(basemap_, ImGui::GetColorU32(theme::hex(0x6F8A98)), 1.0f);
        drawLines(borders_, ImGui::GetColorU32(theme::hex(0xA6C2D0)), 1.4f);
        // Interstates / motorways only matter once zoomed in; gate to keep the
        // continental view fast and uncluttered.
        if (z >= 6)
            drawLines(roads_, ImGui::GetColorU32(theme::hex(0xC79A4E, 0.85f)), 1.2f);

        // City/town labels -- more appear as you zoom in (by scalerank).
        int maxRank = (z - 3) * 2;
        ImU32 dotc = ImGui::GetColorU32(theme::Accent);
        ImU32 lblc = ImGui::GetColorU32(theme::Fg);
        ImU32 shad = ImGui::GetColorU32(theme::hex(0x000000, 0.65f));
        for (const auto& pc : places_) {
            if (pc.rank > maxRank) continue;
            if (pc.lon < wLonMin || pc.lon > wLonMax ||
                pc.lat < wLatMin || pc.lat > wLatMax) continue;
            ImVec2 sp(static_cast<float>(offX + pc.px), static_cast<float>(offY + pc.py));
            if (sp.x < p0.x || sp.x > p1.x || sp.y < p0.y || sp.y > p1.y) continue;
            dl->AddCircleFilled(sp, 2.5f, dotc);
            ImVec2 lp(sp.x + 4, sp.y - 6);
            dl->AddText(ImVec2(lp.x + 1, lp.y + 1), shad, pc.name.c_str());  // shadow
            dl->AddText(lp, lblc, pc.name.c_str());
        }
    }

    // Location marker.
    ImU32 mk = ImGui::GetColorU32(theme::Amber);
    dl->AddCircle(cen, 6.0f, mk, 16, 2.0f);
    dl->AddLine(ImVec2(cen.x - 10, cen.y), ImVec2(cen.x + 10, cen.y), mk, 1.5f);
    dl->AddLine(ImVec2(cen.x, cen.y - 10), ImVec2(cen.x, cen.y + 10), mk, 1.5f);

    // Wind gauge (top-right): arrow flies downwind; label shows the "from"
    // direction + speed at the centered location.
    if (haveWind_) {
        const float r = 26.0f;
        ImVec2 gc(p1.x - r - 16.0f, p0.y + r + 16.0f);
        dl->AddCircleFilled(gc, r + 6.0f, IM_COL32(10, 18, 22, 205), 32);
        dl->AddCircle(gc, r, ImGui::GetColorU32(theme::hex(0x6F8A98)), 32, 1.2f);
        dl->AddText(ImVec2(gc.x - 4.0f, gc.y - r - 14.0f),
                    ImGui::GetColorU32(theme::Dim), "N");

        double toBear = windDir_ + 180.0;  // wind blows toward FROM-bearing + 180
        double a = toBear * kPI / 180.0;
        float dx = static_cast<float>(std::sin(a));
        float dy = -static_cast<float>(std::cos(a));
        ImVec2 tip(gc.x + dx * (r - 4.0f), gc.y + dy * (r - 4.0f));
        ImVec2 tail(gc.x - dx * (r - 8.0f), gc.y - dy * (r - 8.0f));
        ImU32 ac = ImGui::GetColorU32(theme::Amber);
        dl->AddLine(tail, tip, ac, 2.5f);
        double aL = (toBear + 152.0) * kPI / 180.0, aR = (toBear - 152.0) * kPI / 180.0;
        dl->AddLine(tip, ImVec2(tip.x + static_cast<float>(std::sin(aL)) * 9.0f,
                                tip.y - static_cast<float>(std::cos(aL)) * 9.0f), ac, 2.5f);
        dl->AddLine(tip, ImVec2(tip.x + static_cast<float>(std::sin(aR)) * 9.0f,
                                tip.y - static_cast<float>(std::cos(aR)) * 9.0f), ac, 2.5f);

        double sp = windMph_ ? windSpeed_ * 0.621371 : windSpeed_;
        double gu = windMph_ ? windGust_ * 0.621371 : windGust_;
        const char* unit = windMph_ ? "mph" : "km/h";
        char buf[64];
        if (gu > sp + 5.0)
            std::snprintf(buf, sizeof buf, "%s %.0f g%.0f %s", windCompass(windDir_), sp, gu, unit);
        else
            std::snprintf(buf, sizeof buf, "%s %.0f %s", windCompass(windDir_), sp, unit);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText(ImVec2(gc.x - ts.x * 0.5f, gc.y + r + 9.0f),
                    ImGui::GetColorU32(theme::Fg), buf);
    }

    char ll[48];
    std::snprintf(ll, sizeof ll, "%.3f, %.3f", clat, clon);
    dl->AddText(ImVec2(p0.x + 6, p0.y + 6), ImGui::GetColorU32(theme::Fg), ll);
    if (!haveData)
        dl->AddText(ImVec2(p0.x + 10, p0.y + 26),
                    ImGui::GetColorU32(theme::Dim), "Loading radar...");
    // Past the data zoom we're showing enlarged tiles; say so honestly.
    if (haveData && z > kMaxDataZoom)
        dl->AddText(ImVec2(p0.x + 6, p1.y - 18),
                    ImGui::GetColorU32(theme::Dim), "Enlarged - radar detail maxes out here");
    dl->PopClipRect();

    // --- interaction: drag-pan + wheel-zoom ---
    if (active) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0.0f || d.y != 0.0f) {
            double nlon, nlat;
            tileToLonLat(cx - d.x / 256.0, cy - d.y / 256.0, z, nlon, nlat);
            if (nlat > 85.0) nlat = 85.0; if (nlat < -85.0) nlat = -85.0;
            std::lock_guard<std::mutex> lk(mx_);
            centerLat_ = nlat; centerLon_ = nlon;
        }
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        int nz = z + (ImGui::GetIO().MouseWheel > 0.0f ? 1 : -1);
        nz = std::max(kMinZoom, std::min(kMaxZoom, nz));
        if (nz != z) {
            // Keep existing textures: tiles are keyed by zoom, so returning to a
            // level redraws instantly from cache. Eviction bounds memory; the
            // worker fetches only the new level's missing tiles.
            std::lock_guard<std::mutex> lk(mx_);
            zoom_ = nz;
        }
    }

    // --- footer: required attribution ---
    ImGui::TextDisabled("Radar (c) RainViewer  -  basemap Natural Earth");

    ImGui::End();
    if (!open) close();

    if (playing_ && frameCount > 1) {
        frameTimer_ += ImGui::GetIO().DeltaTime;
        if (frameTimer_ >= 0.6f) {
            frameTimer_ = 0.0f;
            curFrame_ = (curFrame_ + 1) % frameCount;
        }
    }
}

}  // namespace wd
