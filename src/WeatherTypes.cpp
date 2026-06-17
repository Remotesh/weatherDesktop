#include "weatherdesktop/WeatherTypes.h"
#include <cmath>
#include <sstream>
#include <iomanip>

namespace wd {

std::string GeoLocation::cacheKey() const {
    // Full-ish precision (~10 m) plus the name so two nearby saved cities can't
    // collide onto one cache entry and show each other's weather.
    std::ostringstream ss;
    ss << name << '@' << std::fixed << std::setprecision(4)
       << latitude << ',' << longitude;
    return ss.str();
}

std::string WeatherAlert::deduplicationKey() const {
    // Official alerts carry a stable agency id; key off that so two distinct
    // warnings (e.g. Flood + Heat) don't collapse onto one AlertType::Official.
    if (official && !id.empty()) return "OFFICIAL|" + id;
    return std::to_string(static_cast<int>(type)) + "|" + locationName + "|" +
           std::to_string(std::chrono::duration_cast<std::chrono::hours>(
               timestamp.time_since_epoch()).count() / 24);
}

PrecipNowcast computePrecipNowcast(const WeatherData& data) {
    PrecipNowcast nc;
    const auto& m = data.minutely;
    if (m.empty()) return nc;  // NoData

    // A 15-min step counts as "wet" above a small threshold so a trace reading
    // doesn't flip the state. Steps are 15 minutes apart, so step i is ~i*15
    // minutes out from now (index 0 ~= the current quarter-hour).
    constexpr double kWet = 0.05;  // mm per 15-min step
    constexpr int kStepMin = 15;

    bool wetNow = m.front().precipitation > kWet;

    if (wetNow) {
        for (size_t i = 1; i < m.size(); ++i) {
            if (m[i].precipitation <= kWet) {
                nc.state = NowcastState::RainStopping;
                nc.minutes = static_cast<int>(i) * kStepMin;
                return nc;
            }
        }
        nc.state = NowcastState::RainOngoing;
        return nc;
    }

    for (size_t i = 1; i < m.size(); ++i) {
        if (m[i].precipitation > kWet) {
            nc.state = NowcastState::RainStarting;
            nc.minutes = static_cast<int>(i) * kStepMin;
            return nc;
        }
    }
    nc.state = NowcastState::Dry;
    return nc;
}

const char* uvCategory(double uv) {
    if (uv < 3.0) return "Low";
    if (uv < 6.0) return "Moderate";
    if (uv < 8.0) return "High";
    if (uv < 11.0) return "Very High";
    return "Extreme";
}

const char* aqiCategory(double usAqi) {
    if (usAqi < 0.0) return "--";
    if (usAqi <= 50.0) return "Good";
    if (usAqi <= 100.0) return "Moderate";
    if (usAqi <= 150.0) return "Unhealthy (Sensitive)";
    if (usAqi <= 200.0) return "Unhealthy";
    if (usAqi <= 300.0) return "Very Unhealthy";
    return "Hazardous";
}

PressureTrend classifyPressureTrend(double deltaHpa) {
    if (deltaHpa >= 1.0) return PressureTrend::Rising;
    if (deltaHpa <= -1.0) return PressureTrend::Falling;
    return PressureTrend::Steady;
}

const char* weatherCodeToString(int code) {
    switch (code) {
        case 0:  return "Clear Sky";
        case 1:  return "Mainly Clear";
        case 2:  return "Partly Cloudy";
        case 3:  return "Overcast";
        case 45: return "Fog";
        case 48: return "Rime Fog";
        case 51: return "Light Drizzle";
        case 53: return "Moderate Drizzle";
        case 55: return "Dense Drizzle";
        case 56: return "Freezing Drizzle";
        case 57: return "Heavy Freezing Drizzle";
        case 61: return "Slight Rain";
        case 63: return "Moderate Rain";
        case 65: return "Heavy Rain";
        case 66: return "Freezing Rain";
        case 67: return "Heavy Freezing Rain";
        case 71: return "Slight Snow";
        case 73: return "Moderate Snow";
        case 75: return "Heavy Snow";
        case 77: return "Snow Grains";
        case 80: return "Slight Showers";
        case 81: return "Moderate Showers";
        case 82: return "Violent Showers";
        case 85: return "Light Snow Showers";
        case 86: return "Heavy Snow Showers";
        case 95: return "Thunderstorm";
        case 96: return "Thunderstorm w/ Hail";
        case 99: return "Severe Thunderstorm w/ Hail";
        default: return "Unknown";
    }
}

const char* weatherCodeToShortString(int code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Clear";
        case 2:  return "PtCld";
        case 3:  return "Ovcst";
        case 45: case 48: return "Fog";
        case 51: case 53: case 55: return "Drzzl";
        case 56: case 57: return "FzDrz";
        case 61: case 63: return "Rain";
        case 65: return "HvRan";
        case 66: case 67: return "FzRan";
        case 71: case 73: return "Snow";
        case 75: return "HvSnw";
        case 77: return "SnGrn";
        case 80: case 81: return "Shwrs";
        case 82: return "Storm";
        case 85: case 86: return "SnShw";
        case 95: return "TStrm";
        case 96: case 99: return "Hail";
        default: return "???";
    }
}

const char* windDirectionToString(double degrees) {
    if (degrees < 0) return "N/A";
    int idx = static_cast<int>(std::fmod(degrees + 22.5, 360.0) / 45.0);
    static const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return dirs[idx % 8];
}

int weatherCodeToIconCell(int code, bool isNight) {
    // Atlas cells (row*6 + col), see resources/sprite_atlas.txt:
    //  0 sunny          1 mostly_sunny    2 partly_cloudy   3 cloudy
    //  4 fog            5 haze            6 slight_rain     7 rain
    //  8 heavy_rain     9 showers        10 rain_drizzle  11 light_rain
    // 12 thunderstorm  13 heavy_tstorm   14 tstorm        15 isolated_tstorm
    // 16 scattered_ts  17 hail           18 rain_snow     19 snow
    // 20 heavy_snow    21 snow_showers   22 flurries      23 windy
    // 24 clear_night   25 mostly_clear   26 pcloudy_night 27 cloudy_night
    // 28 showers_night 29 tstorm_night
    switch (code) {
        case 0:  return isNight ? 24 : 0;   // clear
        case 1:  return isNight ? 25 : 1;   // mainly clear
        case 2:  return isNight ? 26 : 2;   // partly cloudy
        case 3:  return isNight ? 27 : 3;   // overcast
        case 45: case 48: return 4;         // fog / rime fog
        case 51: case 53: return 10;        // drizzle
        case 55: return 7;                  // dense drizzle
        case 56: case 57: return 18;        // freezing drizzle
        case 61: return 6;                  // slight rain
        case 63: return 7;                  // moderate rain
        case 65: return 8;                  // heavy rain
        case 66: case 67: return 18;        // freezing rain
        case 71: return 22;                 // slight snow -> flurries
        case 73: return 19;                 // moderate snow
        case 75: return 20;                 // heavy snow
        case 77: return 22;                 // snow grains -> flurries
        case 80: case 81: return isNight ? 28 : 9;  // rain showers
        case 82: return 8;                  // violent showers -> heavy rain
        case 85: case 86: return 21;        // snow showers
        case 95: return isNight ? 29 : 12;  // thunderstorm
        case 96: case 99: return 17;        // thunderstorm w/ hail
        default: return -1;
    }
}

} // namespace wd
