#pragma once

#include "WeatherTypes.h"
#include <string>
#include <optional>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace wd {

class WeatherService {
public:
    WeatherService();
    ~WeatherService();

    // Blocking calls -- run from background thread
    std::optional<WeatherData> fetchWeather(const GeoLocation& location);
    std::vector<GeoLocation> geocode(const std::string& query);

    // Cache
    bool hasFreshCache(const GeoLocation& loc, int maxAgeMinutes = 10) const;
    std::optional<WeatherData> getCached(const GeoLocation& loc) const;
    void clearCache();

private:
    std::string httpGet(const std::string& url);
    WeatherData parseWeatherResponse(const std::string& json, const GeoLocation& loc);
    std::vector<GeoLocation> parseGeocodeResponse(const std::string& json);
    double fetchKpIndex();  // NOAA planetary Kp; -1 on failure

    std::unordered_map<std::string, WeatherData> cache_;
    mutable std::mutex cacheMutex_;
};

} // namespace wd
