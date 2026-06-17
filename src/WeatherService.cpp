#include "weatherdesktop/WeatherService.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <sstream>
#include <cmath>
#include <stdexcept>

namespace wd {

using json = nlohmann::json;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// libcurl progress callback. Returns nonzero to abort the transfer once the
// shutdown flag (passed as clientp) is set, so the worker thread doesn't have to
// wait out request timeouts on quit. clientp is the WeatherService::abort_ flag.
static int xferCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* flag = static_cast<std::atomic<bool>*>(clientp);
    return (flag && flag->load()) ? 1 : 0;
}

WeatherService::WeatherService() {}
WeatherService::~WeatherService() {}

std::string WeatherService::httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // Abort in-flight transfers promptly when the app is quitting.
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &abort_);
    // A descriptive User-Agent: api.weather.gov (NWS) rejects generic/empty ones.
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
                     "WeatherDesktop/1.0 (open-source desktop weather app)");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";
    // Open-Meteo returns a JSON error body (with no weather fields) on 4xx/5xx;
    // treat anything outside 2xx as a failure so it surfaces as an error rather
    // than parsing into an all-zero "0 degrees" forecast.
    if (httpCode < 200 || httpCode >= 300) return "";
    return response;
}

std::optional<WeatherData> WeatherService::fetchWeather(const GeoLocation& location) {
    // Check cache first
    if (hasFreshCache(location)) {
        return getCached(location);
    }

    std::ostringstream url;
    url << "https://api.open-meteo.com/v1/forecast?"
        << "latitude=" << location.latitude
        << "&longitude=" << location.longitude
        << "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
        << "precipitation,rain,showers,snowfall,weather_code,cloud_cover,"
        << "pressure_msl,dew_point_2m,visibility,uv_index,"
        << "wind_speed_10m,wind_direction_10m,wind_gusts_10m"
        << "&hourly=temperature_2m,weather_code,precipitation_probability,"
        << "precipitation,snowfall,uv_index,surface_pressure,"
        << "wind_speed_10m,wind_direction_10m"
        << "&minutely_15=precipitation,weather_code"
        << "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
        << "precipitation_sum,rain_sum,snowfall_sum,precipitation_probability_max,"
        << "sunrise,sunset,uv_index_max,daylight_duration"
        // past_days=1 brings yesterday's daily totals (for the "vs yesterday"
        // readout) and extra past hours (for the 3h pressure tendency) in the
        // same request -- no separate call.
        << "&timezone=auto&past_days=1&forecast_days=7";

    std::string response = httpGet(url.str());
    if (response.empty()) return std::nullopt;

    try {
        // Core forecast only -- the best-effort extras (Kp, model band, air
        // quality) are fetched separately by enrichWeather so the UI can paint
        // the forecast without waiting on three more round-trips.
        WeatherData data = parseWeatherResponse(response, location);
        data.enriched = false;

        // Store in cache, bounded so a long-running session can't grow it
        // without limit. Evict the oldest entry when over the cap.
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_[location.cacheKey()] = data;
        constexpr size_t kMaxCacheEntries = 32;
        while (cache_.size() > kMaxCacheEntries) {
            auto oldest = cache_.begin();
            for (auto it = cache_.begin(); it != cache_.end(); ++it) {
                if (it->second.fetchedAt < oldest->second.fetchedAt) oldest = it;
            }
            cache_.erase(oldest);
        }

        return data;
    } catch (...) {
        return std::nullopt;
    }
}

void WeatherService::enrichWeather(const GeoLocation& location, WeatherData& data) {
    if (data.enriched) return;
    data.kpIndex = fetchKpIndex();          // best-effort; -1 if unavailable
    fetchUncertaintyBand(location, data);   // best-effort confidence band
    fetchAirQuality(location, data);        // best-effort air quality
    data.enriched = true;

    // Update the cached entry so a later cache hit carries the extras too.
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(location.cacheKey());
    if (it != cache_.end()) it->second = data;
}

WeatherData WeatherService::parseWeatherResponse(const std::string& jsonStr, const GeoLocation& loc) {
    json j = json::parse(jsonStr);
    // Open-Meteo signals failures with {"error":true,"reason":"..."}; reject
    // those rather than building an all-zero forecast.
    if (j.value("error", false)) {
        throw std::runtime_error(j.value("reason", "weather API error"));
    }
    WeatherData data;
    data.location = loc;
    data.fetchedAt = std::chrono::steady_clock::now();
    data.utcOffsetSeconds = j.value("utc_offset_seconds", 0);

    if (j.contains("current")) {
        auto& c = j["current"];
        data.current.temperature = c.value("temperature_2m", 0.0);
        data.current.humidity = c.value("relative_humidity_2m", 0.0);
        data.current.apparentTemp = c.value("apparent_temperature", 0.0);
        data.current.precipitation = c.value("precipitation", 0.0);
        data.current.rain = c.value("rain", 0.0);
        data.current.snowfall = c.value("snowfall", 0.0);
        data.current.weatherCode = c.value("weather_code", 0);
        data.current.cloudCover = c.value("cloud_cover", 0.0);
        data.current.pressure = c.value("pressure_msl", 0.0);
        data.current.dewPoint = c.value("dew_point_2m", 0.0);
        data.current.visibility = c.value("visibility", 0.0);
        data.current.uvIndex = c.value("uv_index", 0.0);
        data.current.windSpeed = c.value("wind_speed_10m", 0.0);
        data.current.windDirection = c.value("wind_direction_10m", 0.0);
        data.current.windGusts = c.value("wind_gusts_10m", 0.0);
        data.current.timestamp = c.value("time", "");
    }

    if (j.contains("hourly")) {
        auto& h = j["hourly"];
        auto times = h.value("time", std::vector<std::string>{});
        auto temps = h.value("temperature_2m", std::vector<double>{});
        auto codes = h.value("weather_code", std::vector<int>{});
        auto precips = h.value("precipitation", std::vector<double>{});
        auto snows = h.value("snowfall", std::vector<double>{});
        auto precipProbs = h.value("precipitation_probability", std::vector<double>{});
        auto uvs = h.value("uv_index", std::vector<double>{});
        auto pressures = h.value("surface_pressure", std::vector<double>{});
        auto winds = h.value("wind_speed_10m", std::vector<double>{});
        auto windDirs = h.value("wind_direction_10m", std::vector<double>{});

        // Find the first future hour by comparing with current time string
        size_t startIdx = 0;
        if (!data.current.timestamp.empty()) {
            for (size_t i = 0; i < times.size(); ++i) {
                if (times[i] >= data.current.timestamp) {
                    startIdx = i;
                    break;
                }
            }
        }

        // Barometric tendency: current surface pressure vs 3h earlier (past hours
        // are present thanks to past_days=1). Skipped if we're too near the array
        // start to look back 3 hours.
        if (startIdx >= 3 && startIdx < pressures.size()) {
            data.pressureDelta3h = pressures[startIdx] - pressures[startIdx - 3];
            data.hasPressureTrend = true;
        }

        // Keep next 24 hours
        size_t endIdx = std::min(startIdx + 24, times.size());
        for (size_t i = startIdx; i < endIdx; ++i) {
            HourlyForecast hr;
            hr.time = i < times.size() ? times[i] : "";
            hr.temperature = i < temps.size() ? temps[i] : 0.0;
            hr.weatherCode = i < codes.size() ? codes[i] : 0;
            hr.precipitation = i < precips.size() ? precips[i] : 0.0;
            hr.snowfall = i < snows.size() ? snows[i] : 0.0;
            hr.precipProb = i < precipProbs.size() ? precipProbs[i] : 0.0;
            hr.uvIndex = i < uvs.size() ? uvs[i] : 0.0;
            hr.pressure = i < pressures.size() ? pressures[i] : 0.0;
            hr.windSpeed = i < winds.size() ? winds[i] : 0.0;
            hr.windDirection = i < windDirs.size() ? windDirs[i] : 0.0;
            data.hourly.push_back(hr);
        }
    }

    if (j.contains("minutely_15")) {
        auto& m = j["minutely_15"];
        auto times = m.value("time", std::vector<std::string>{});
        auto precips = m.value("precipitation", std::vector<double>{});
        auto codes = m.value("weather_code", std::vector<int>{});

        // Start at the first step at/after the current observation, then keep the
        // next ~2 hours (8 x 15-min steps) -- enough for a "rain in N min" readout.
        size_t startIdx = 0;
        if (!data.current.timestamp.empty()) {
            for (size_t i = 0; i < times.size(); ++i) {
                if (times[i] >= data.current.timestamp) { startIdx = i; break; }
            }
        }
        size_t endIdx = std::min(startIdx + 8, times.size());
        for (size_t i = startIdx; i < endIdx; ++i) {
            MinutelyForecast mf;
            mf.time = times[i];
            mf.precipitation = i < precips.size() ? precips[i] : 0.0;
            mf.weatherCode = i < codes.size() ? codes[i] : 0;
            data.minutely.push_back(mf);
        }
    }

    if (j.contains("daily")) {
        auto& d = j["daily"];
        auto dates = d.value("time", std::vector<std::string>{});
        auto codes = d.value("weather_code", std::vector<int>{});
        auto maxTemps = d.value("temperature_2m_max", std::vector<double>{});
        auto minTemps = d.value("temperature_2m_min", std::vector<double>{});
        auto precipSums = d.value("precipitation_sum", std::vector<double>{});
        auto rainSums = d.value("rain_sum", std::vector<double>{});
        auto snowSums = d.value("snowfall_sum", std::vector<double>{});
        auto precipProbs = d.value("precipitation_probability_max", std::vector<double>{});
        auto sunrises = d.value("sunrise", std::vector<std::string>{});
        auto sunsets = d.value("sunset", std::vector<std::string>{});
        auto uvMaxes = d.value("uv_index_max", std::vector<double>{});
        auto daylights = d.value("daylight_duration", std::vector<double>{});

        size_t count = dates.size();

        // past_days=1 prepends yesterday, so locate "today" by matching the
        // current observation's date rather than assuming index 0.
        std::string todayDate = data.current.timestamp.substr(
            0, std::min<size_t>(10, data.current.timestamp.size()));
        size_t todayIdx = 0;
        bool found = false;
        if (!todayDate.empty()) {
            for (size_t i = 0; i < dates.size(); ++i) {
                if (dates[i] == todayDate) { todayIdx = i; found = true; break; }
            }
        }
        // Fallback when the dates can't be matched: with past_days=1 yesterday is
        // index 0, so today is index 1 (when present).
        if (!found && count >= 2) todayIdx = 1;

        if (todayIdx >= 1 && todayIdx - 1 < maxTemps.size()) {
            data.hasYesterday = true;
            data.yesterdayTempMax = maxTemps[todayIdx - 1];
            data.yesterdayTempMin = (todayIdx - 1 < minTemps.size())
                                        ? minTemps[todayIdx - 1] : 0.0;
        }

        for (size_t i = todayIdx; i < count; ++i) {
            DailyForecast day;
            day.date = i < dates.size() ? dates[i] : "";
            day.weatherCode = i < codes.size() ? codes[i] : 0;
            day.tempMax = i < maxTemps.size() ? maxTemps[i] : 0.0;
            day.tempMin = i < minTemps.size() ? minTemps[i] : 0.0;
            day.precipitationSum = i < precipSums.size() ? precipSums[i] : 0.0;
            day.rainSum = i < rainSums.size() ? rainSums[i] : 0.0;
            day.snowfallSum = i < snowSums.size() ? snowSums[i] : 0.0;
            day.precipProbMax = i < precipProbs.size() ? precipProbs[i] : 0.0;
            day.sunrise = i < sunrises.size() ? sunrises[i] : "";
            day.sunset = i < sunsets.size() ? sunsets[i] : "";
            day.uvIndexMax = i < uvMaxes.size() ? uvMaxes[i] : 0.0;
            day.daylightSeconds = i < daylights.size() ? daylights[i] : 0.0;
            data.daily.push_back(day);
        }
    }

    return data;
}

std::vector<GeoLocation> WeatherService::geocode(const std::string& query) {
    std::string encodedQuery;
    CURL* curl = curl_easy_init();
    if (curl) {
        char* escaped = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.length()));
        if (escaped) {
            encodedQuery = escaped;
            curl_free(escaped);
        }
        curl_easy_cleanup(curl);
    }
    if (encodedQuery.empty()) return {};

    std::string url = "https://geocoding-api.open-meteo.com/v1/search?name=" +
                      encodedQuery + "&count=5&language=en&format=json";

    std::string response = httpGet(url);
    if (response.empty()) return {};

    try {
        return parseGeocodeResponse(response);
    } catch (...) {
        return {};
    }
}

double WeatherService::fetchKpIndex() {
    // NOAA SWPC planetary K-index feed (free, no key). The JSON is an array
    // whose first row is a header and whose last row is the most recent reading:
    //   [["time_tag","Kp","a_running","station_count"], ["2026-..","3.00",..], ..]
    std::string resp = httpGet(
        "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json");
    if (resp.empty()) return -1.0;
    try {
        json j = json::parse(resp);
        if (!j.is_array() || j.size() < 2) return -1.0;
        const auto& last = j.back();
        if (!last.is_array() || last.size() < 2) return -1.0;
        return std::stod(last[1].get<std::string>());
    } catch (...) {
        return -1.0;
    }
}

void WeatherService::fetchUncertaintyBand(const GeoLocation& loc, WeatherData& data) {
    if (data.daily.empty()) return;

    // Ask several independent models for the daily high/low. Open-Meteo returns
    // one suffixed column per model (e.g. temperature_2m_max_gfs_seamless); the
    // spread across them is a cheap, key-free proxy for forecast confidence.
    std::ostringstream url;
    url << "https://api.open-meteo.com/v1/forecast?"
        << "latitude=" << loc.latitude
        << "&longitude=" << loc.longitude
        << "&daily=temperature_2m_max,temperature_2m_min"
        << "&models=icon_seamless,gfs_seamless,ecmwf_ifs04,gem_seamless,jma_seamless"
        << "&timezone=auto&forecast_days=" << data.daily.size();

    std::string resp = httpGet(url.str());
    if (resp.empty()) return;

    try {
        json j = json::parse(resp);
        if (j.value("error", false) || !j.contains("daily")) return;
        auto& d = j["daily"];

        // Per day, gather every model's value for max and for min (skipping the
        // nulls Open-Meteo emits where a model doesn't cover this point), then
        // take the min/max across models as the band.
        const size_t n = data.daily.size();
        std::vector<std::vector<double>> maxByDay(n), minByDay(n);

        for (auto it = d.begin(); it != d.end(); ++it) {
            const std::string& key = it.key();
            if (!it->is_array()) continue;
            std::vector<std::vector<double>>* dest = nullptr;
            if (key.rfind("temperature_2m_max", 0) == 0) dest = &maxByDay;
            else if (key.rfind("temperature_2m_min", 0) == 0) dest = &minByDay;
            if (!dest) continue;
            for (size_t i = 0; i < n && i < it->size(); ++i) {
                const auto& v = (*it)[i];
                if (v.is_number()) (*dest)[i].push_back(v.get<double>());
            }
        }

        for (size_t i = 0; i < n; ++i) {
            // Need at least two models to call it a spread.
            if (maxByDay[i].size() < 2 || minByDay[i].size() < 2) continue;
            auto mmHi = std::minmax_element(maxByDay[i].begin(), maxByDay[i].end());
            auto mmLo = std::minmax_element(minByDay[i].begin(), minByDay[i].end());
            DailyForecast& day = data.daily[i];
            day.hasUncertainty = true;
            day.tempMaxLow = *mmHi.first;
            day.tempMaxHigh = *mmHi.second;
            day.tempMinLow = *mmLo.first;
            day.tempMinHigh = *mmLo.second;
            // Reconciliation: keep the deterministic headline inside its band so
            // the number and the band can never visibly contradict each other.
            day.tempMax = std::min(std::max(day.tempMax, day.tempMaxLow), day.tempMaxHigh);
            day.tempMin = std::min(std::max(day.tempMin, day.tempMinLow), day.tempMinHigh);
        }
    } catch (...) {
        // best-effort: leave hasUncertainty false
    }
}

std::vector<WeatherAlert> WeatherService::fetchOfficialAlerts(const GeoLocation& loc) {
    // The US National Weather Service covers the United States only.
    if (loc.country != "US" && loc.country != "United States") return {};

    // Short-TTL cache (independent of the forecast cache).
    {
        std::lock_guard<std::mutex> lock(alertsCacheMutex_);
        auto it = alertsCache_.find(loc.cacheKey());
        if (it != alertsCache_.end()) {
            auto age = std::chrono::steady_clock::now() - it->second.fetchedAt;
            if (std::chrono::duration_cast<std::chrono::minutes>(age).count() < 3) {
                return it->second.alerts;
            }
        }
    }

    std::ostringstream url;
    url << "https://api.weather.gov/alerts/active?status=actual&message_type=alert"
        << "&point=" << loc.latitude << "," << loc.longitude;

    std::vector<WeatherAlert> alerts;
    std::string resp = httpGet(url.str());
    if (!resp.empty()) {
        try {
            json j = json::parse(resp);
            auto now = std::chrono::system_clock::now();
            if (j.contains("features") && j["features"].is_array()) {
                for (auto& f : j["features"]) {
                    if (!f.contains("properties")) continue;
                    auto& p = f["properties"];
                    WeatherAlert a;
                    a.type = AlertType::Official;
                    a.official = true;
                    a.id = p.value("id", f.value("id", std::string{}));
                    a.severity = p.value("severity", std::string{});
                    a.title = p.value("event", std::string("Weather Alert"));
                    // Prefer the short headline; fall back to the area description.
                    a.message = p.value("headline", std::string{});
                    if (a.message.empty()) a.message = p.value("areaDesc", std::string{});
                    a.locationName = loc.name;
                    a.timestamp = now;
                    alerts.push_back(std::move(a));
                }
            }
        } catch (...) {
            // leave alerts empty on parse failure
        }
    }

    {
        std::lock_guard<std::mutex> lock(alertsCacheMutex_);
        alertsCache_[loc.cacheKey()] = {std::chrono::steady_clock::now(), alerts};
    }
    return alerts;
}

void WeatherService::fetchAirQuality(const GeoLocation& loc, WeatherData& data) {
    std::ostringstream url;
    url << "https://air-quality-api.open-meteo.com/v1/air-quality?"
        << "latitude=" << loc.latitude
        << "&longitude=" << loc.longitude
        << "&current=us_aqi,european_aqi,pm2_5,pm10,ozone"
        << "&timezone=auto";
    std::string resp = httpGet(url.str());
    if (resp.empty()) return;
    try {
        data.airQuality = parseAirQuality(resp);
    } catch (...) {
        // leave airQuality.valid = false
    }
}

AirQuality WeatherService::parseAirQuality(const std::string& jsonStr) {
    AirQuality aq;
    json j = json::parse(jsonStr);
    if (j.value("error", false) || !j.contains("current")) return aq;
    auto& c = j["current"];
    aq.usAqi = c.value("us_aqi", -1.0);
    aq.europeanAqi = c.value("european_aqi", -1.0);
    aq.pm2_5 = c.value("pm2_5", -1.0);
    aq.pm10 = c.value("pm10", -1.0);
    aq.ozone = c.value("ozone", -1.0);
    // Consider the snapshot usable if at least one AQI index came back.
    aq.valid = (aq.usAqi >= 0.0 || aq.europeanAqi >= 0.0);
    return aq;
}

std::vector<GeoLocation> WeatherService::parseGeocodeResponse(const std::string& jsonStr) {
    json j = json::parse(jsonStr);
    std::vector<GeoLocation> results;

    if (!j.contains("results")) return results;

    for (auto& r : j["results"]) {
        GeoLocation loc;
        loc.name = r.value("name", "");
        loc.country = r.value("country_code", "");
        loc.admin1 = r.value("admin1", "");
        loc.latitude = r.value("latitude", 0.0);
        loc.longitude = r.value("longitude", 0.0);
        loc.timezone = r.value("timezone", "");
        results.push_back(std::move(loc));
    }

    return results;
}

bool WeatherService::hasFreshCache(const GeoLocation& loc, int maxAgeMinutes) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(loc.cacheKey());
    if (it == cache_.end()) return false;
    auto age = std::chrono::steady_clock::now() - it->second.fetchedAt;
    return std::chrono::duration_cast<std::chrono::minutes>(age).count() < maxAgeMinutes;
}

std::optional<WeatherData> WeatherService::getCached(const GeoLocation& loc) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(loc.cacheKey());
    if (it == cache_.end()) return std::nullopt;
    return it->second;
}

void WeatherService::clearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
}

} // namespace wd
