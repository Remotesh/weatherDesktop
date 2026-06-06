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

WeatherService::WeatherService() {}
WeatherService::~WeatherService() {}

std::string WeatherService::httpGet(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "WeatherDesktop/0.1");

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
        << "wind_speed_10m,wind_direction_10m,wind_gusts_10m"
        << "&hourly=temperature_2m,weather_code,precipitation_probability,"
        << "precipitation,wind_speed_10m,wind_direction_10m"
        << "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
        << "precipitation_sum,rain_sum,snowfall_sum,precipitation_probability_max"
        << "&timezone=auto&forecast_days=7";

    std::string response = httpGet(url.str());
    if (response.empty()) return std::nullopt;

    try {
        WeatherData data = parseWeatherResponse(response, location);
        data.kpIndex = fetchKpIndex();  // best-effort; -1 if unavailable

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
        auto precipProbs = h.value("precipitation_probability", std::vector<double>{});
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

        // Keep next 24 hours
        size_t endIdx = std::min(startIdx + 24, times.size());
        for (size_t i = startIdx; i < endIdx; ++i) {
            HourlyForecast hr;
            hr.time = i < times.size() ? times[i] : "";
            hr.temperature = i < temps.size() ? temps[i] : 0.0;
            hr.weatherCode = i < codes.size() ? codes[i] : 0;
            hr.precipitation = i < precips.size() ? precips[i] : 0.0;
            hr.precipProb = i < precipProbs.size() ? precipProbs[i] : 0.0;
            hr.windSpeed = i < winds.size() ? winds[i] : 0.0;
            hr.windDirection = i < windDirs.size() ? windDirs[i] : 0.0;
            data.hourly.push_back(hr);
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

        size_t count = dates.size();
        for (size_t i = 0; i < count; ++i) {
            DailyForecast day;
            day.date = i < dates.size() ? dates[i] : "";
            day.weatherCode = i < codes.size() ? codes[i] : 0;
            day.tempMax = i < maxTemps.size() ? maxTemps[i] : 0.0;
            day.tempMin = i < minTemps.size() ? minTemps[i] : 0.0;
            day.precipitationSum = i < precipSums.size() ? precipSums[i] : 0.0;
            day.rainSum = i < rainSums.size() ? rainSums[i] : 0.0;
            day.snowfallSum = i < snowSums.size() ? snowSums[i] : 0.0;
            day.precipProbMax = i < precipProbs.size() ? precipProbs[i] : 0.0;
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
