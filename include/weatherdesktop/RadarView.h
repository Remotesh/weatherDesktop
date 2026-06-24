#pragma once

#include "Texture.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace wd {

// In-app weather-radar overlay: animated RainViewer reflectivity tiles (past +
// short-range nowcast/forecast frames) over a lat/lon graticule, centered on the
// active location. Tiles are fetched on a dedicated thread; textures are uploaded
// on the GL/main thread in render(). Closed = no thread, no fetches, ~0 cost.
//
// NOTE: radar data (c) RainViewer (rainviewer.com) -- attribution shown in-window.
class RadarView {
public:
    ~RadarView();

    void toggle(double lat, double lon);  // open centered here, or close if open
    void close();                          // GL-thread only (frees textures)
    bool isOpen() const { return open_.load(); }

    // Current wind at the centered location, for the on-map gauge (main thread).
    void setWind(bool have, double dirDeg, double speedKmh, double gustKmh, bool mph);

    // Call once per frame on the GL/main thread while open.
    void render();

private:
    struct Frame { std::string path; bool nowcast = false; long long time = 0; };
    struct TileKey {
        int f = 0, z = 0, x = 0, y = 0;
        bool operator<(const TileKey& o) const {
            return std::tie(f, z, x, y) < std::tie(o.f, o.z, o.x, o.y);
        }
    };
    // A projected basemap polyline (lon/lat points) with its bounding box.
    // px/py cache the tile-pixel projection at projZoom_ so the per-frame draw is
    // a cheap affine offset instead of a trig transform per point.
    struct Pt { float lon, lat; };
    struct Polyline {
        std::vector<Pt> pts;
        std::vector<float> px, py;  // tile-pixel coords at projZoom_
        float minLon, maxLon, minLat, maxLat;
    };
    // A populated place (city/town) label.
    struct Place { float lon, lat, px = 0, py = 0; int rank; std::string name; };

    void startWorker();
    void stopWorker();
    void workerLoop();                    // coordinator: maps + enqueue needed tiles
    void enqueueNeeded();                 // queue missing tiles for the current view
    void fetcherLoop();                   // pool thread: persistent-handle tile fetch
    void loadBasemap();                   // parse the bundled Natural Earth GeoJSON
    void buildProjection(int z);          // cache tile-pixel coords for the basemap/roads
    void clearTiles();                    // drop textures + pending bytes (GL thread)

    std::atomic<bool> open_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> abort_{false};
    std::thread worker_;                // coordinator
    std::vector<std::thread> fetchers_; // parallel tile-fetch pool

    std::deque<TileKey> jobs_;          // tile-fetch work queue
    std::mutex jobsMx_;
    std::condition_variable jobsCv_;

    std::mutex mx_;                 // guards frames_, host_, bytes_, have_, center/zoom
    std::string host_;
    std::vector<Frame> frames_;
    std::map<TileKey, std::vector<unsigned char>> bytes_;  // fetched PNG, awaiting upload
    std::map<TileKey, bool> have_;                          // keys ever fetched (skip refetch)
    std::map<TileKey, Texture> tex_;                        // uploaded (GL thread only)
    std::atomic<long long> rlUntil_{0};  // steady-clock ms to pause fetching until (429 backoff)

    double centerLat_ = 0.0, centerLon_ = 0.0;
    int zoom_ = 5;
    // Visible tile bounds the render computed last; the worker fetches these.
    int vMinX_ = 0, vMaxX_ = 0, vMinY_ = 0, vMaxY_ = 0;

    std::vector<Polyline> basemap_;            // coastlines + state/province lines
    std::vector<Polyline> borders_;            // country borders
    std::vector<Polyline> counties_;           // US county lines (zoom-gated)
    std::vector<Polyline> roads_;              // interstates / motorways (zoom-gated)
    std::vector<Place> places_;                // city/town labels
    std::atomic<bool> basemapReady_{false};
    std::atomic<bool> texDirty_{false};        // frame set changed -> drop textures (GL thread)
    int projZoom_ = -1;                        // zoom the px/py caches were built for

    int pastCount_ = 0;       // number of "past" (observed) frames
    bool frameInit_ = false;  // have we positioned curFrame_ on the latest frame yet
    int curFrame_ = 0;
    bool playing_ = false;    // open paused on the most-recent frame
    float frameTimer_ = 0.0f;

    // Wind gauge (set each frame from the main thread; read in render()).
    bool haveWind_ = false;
    double windDir_ = 0.0, windSpeed_ = 0.0, windGust_ = 0.0;  // speed/gust in km/h
    bool windMph_ = false;
};

}  // namespace wd
