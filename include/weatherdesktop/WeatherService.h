#pragma once

#include "WeatherTypes.h"
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>

namespace wd {

class WeatherService {
public:
    WeatherService();
    ~WeatherService();

    // Blocking calls -- run from background thread.
    // fetchWeather returns the core forecast quickly (for a fast first paint);
    // enrichWeather then fills in the best-effort extras (Kp, model band, air
    // quality) and updates the cached entry.
    std::optional<WeatherData> fetchWeather(const GeoLocation& location);
    void enrichWeather(const GeoLocation& location, WeatherData& data);
    std::vector<GeoLocation> geocode(const std::string& query);

    // Signal in-flight HTTP transfers to abort ASAP (called on shutdown so the
    // worker thread can be joined without waiting out request timeouts).
    void requestAbort() { abort_ = true; }

    // Official agency alerts (US NWS only; empty elsewhere or on failure).
    // Cached briefly so location switches re-check without hammering the API.
    std::vector<WeatherAlert> fetchOfficialAlerts(const GeoLocation& location);

    // Cache
    bool hasFreshCache(const GeoLocation& loc, int maxAgeMinutes = 10) const;
    std::optional<WeatherData> getCached(const GeoLocation& loc) const;
    void clearCache();

    // Pure response parsers -- no network or instance state, so they're static
    // and exercised directly by the unit tests. parseWeatherResponse throws on an
    // Open-Meteo {"error":true,...} body.
    static WeatherData parseWeatherResponse(const std::string& json, const GeoLocation& loc);
    static std::vector<GeoLocation> parseGeocodeResponse(const std::string& json);
    static AirQuality parseAirQuality(const std::string& json);

private:
    std::string httpGet(const std::string& url);
    // Set once on shutdown; a libcurl progress callback polls it to abort any
    // in-flight transfer (see WeatherService.cpp).
    std::atomic<bool> abort_{false};

    double fetchKpIndex();  // NOAA planetary Kp; -1 on failure
    // Second, best-effort call: fill each day's tempMax/Min confidence band from
    // the spread across several forecast models. No-op on failure.
    void fetchUncertaintyBand(const GeoLocation& loc, WeatherData& data);
    // Best-effort Air Quality API call (no key). Leaves airQuality.valid=false on
    // failure.
    void fetchAirQuality(const GeoLocation& loc, WeatherData& data);

    std::unordered_map<std::string, WeatherData> cache_;
    mutable std::mutex cacheMutex_;

    // Official alerts get their own short-TTL cache, independent of the forecast
    // cache, so switching back to a location re-checks alerts even when its
    // forecast is still fresh.
    struct AlertCacheEntry {
        std::chrono::steady_clock::time_point fetchedAt;
        std::vector<WeatherAlert> alerts;
    };
    std::unordered_map<std::string, AlertCacheEntry> alertsCache_;
    mutable std::mutex alertsCacheMutex_;
};

} // namespace wd
