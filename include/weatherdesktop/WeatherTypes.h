#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace wd {

struct GeoLocation {
    std::string name;
    std::string country;
    std::string admin1;
    double latitude = 0.0;
    double longitude = 0.0;
    std::string timezone;
    // When the user has renamed this location, `name` is the full label and the
    // region parts are not appended (but `country` is kept for the NWS check).
    bool customLabel = false;

    std::string displayName() const {
        if (customLabel) return name;
        std::string result = name;
        if (!admin1.empty()) result += ", " + admin1;
        if (!country.empty()) result += ", " + country;
        return result;
    }

    std::string cacheKey() const;
};

struct CurrentWeather {
    double temperature = 0.0;       // Celsius
    double apparentTemp = 0.0;      // Celsius
    double humidity = 0.0;          // %
    double precipitation = 0.0;     // mm
    double rain = 0.0;              // mm
    double snowfall = 0.0;          // cm
    int weatherCode = 0;            // WMO code
    double cloudCover = 0.0;        // %
    double pressure = 0.0;          // hPa (mean sea level)
    double dewPoint = 0.0;          // Celsius
    double visibility = 0.0;        // meters
    double uvIndex = 0.0;           // UV index (0..~11+)
    double windSpeed = 0.0;         // km/h
    double windDirection = 0.0;     // degrees
    double windGusts = 0.0;         // km/h
    std::string timestamp;
};

struct DailyForecast {
    std::string date;
    int weatherCode = 0;
    double tempMax = 0.0;           // Celsius
    double tempMin = 0.0;
    double precipitationSum = 0.0;  // mm
    double rainSum = 0.0;           // mm
    double snowfallSum = 0.0;       // cm
    double precipProbMax = 0.0;     // %

    // Cross-model forecast spread for the high/low (Phase 2 confidence band).
    // hasUncertainty is false when the multi-model call failed or too few models
    // covered this location/day. The deterministic tempMax/tempMin are clamped to
    // sit inside their band so the headline number never contradicts the spread.
    bool hasUncertainty = false;
    double tempMaxLow = 0.0, tempMaxHigh = 0.0;  // band around tempMax (Celsius)
    double tempMinLow = 0.0, tempMinHigh = 0.0;  // band around tempMin (Celsius)

    std::string sunrise;            // ISO local "2026-06-17T05:23" ("" if absent)
    std::string sunset;             // ISO local "2026-06-17T20:54"
    double uvIndexMax = 0.0;        // peak UV for the day
    double daylightSeconds = 0.0;   // length of daylight
};

// One 15-minute step of the precipitation nowcast (radar-blended, next ~2h).
struct MinutelyForecast {
    std::string time;               // "2026-06-17T14:15"
    double precipitation = 0.0;     // mm in this 15-min step
    int weatherCode = 0;
};

struct HourlyForecast {
    std::string time;               // "2026-02-25T14:00"
    double temperature = 0.0;       // Celsius
    int weatherCode = 0;
    double precipitation = 0.0;     // mm (liquid-equivalent for the hour)
    double snowfall = 0.0;          // cm (snow depth for the hour)
    double precipProb = 0.0;        // %
    double uvIndex = 0.0;           // UV index for the hour
    double pressure = 0.0;          // hPa (surface pressure)
    double windSpeed = 0.0;         // km/h
    double windDirection = 0.0;     // degrees
};

// 30-ish-year climate normal for today's date (Open-Meteo archive, no key).
struct ClimateNormals {
    bool valid = false;
    double normalHighC = 0.0;
    double normalLowC = 0.0;
};

// One predicted tide extreme.
struct TideEvent {
    std::string time;     // local clock "3:42 PM"
    bool high = false;    // high vs low tide
    double heightM = 0.0;
};

// Tides (NOAA CO-OPS, US coastal) + wave height (Open-Meteo Marine, global).
struct TideInfo {
    bool valid = false;            // true if either tides or waves are present
    bool hasTides = false;
    std::string stationName;
    std::vector<TideEvent> events; // upcoming high/low extremes
    double waveHeightM = -1.0;     // <0 = no wave data
};

// Air quality snapshot (Open-Meteo Air Quality API, no key). valid==false when
// the best-effort fetch failed. Negative members mean "not reported".
struct AirQuality {
    bool valid = false;
    double usAqi = -1.0;
    double europeanAqi = -1.0;
    double pm2_5 = -1.0;   // ug/m3
    double pm10 = -1.0;    // ug/m3
    double ozone = -1.0;   // ug/m3
    double no2 = -1.0;     // nitrogen dioxide, ug/m3
    double so2 = -1.0;     // sulphur dioxide, ug/m3
    double co = -1.0;      // carbon monoxide, ug/m3
};

struct WeatherData {
    GeoLocation location;
    CurrentWeather current;
    std::vector<HourlyForecast> hourly;
    std::vector<DailyForecast> daily;
    std::vector<MinutelyForecast> minutely;  // 15-min precip nowcast (next ~2h)
    AirQuality airQuality;
    ClimateNormals normals;
    TideInfo tides;
    std::string afdText;    // NWS Area Forecast Discussion (US); empty otherwise
    std::string afdOffice;  // issuing WFO (e.g. "OKX")
    std::string afdIssued;  // issuance time (ISO)
    int utcOffsetSeconds = 0;
    double kpIndex = -1.0;  // NOAA planetary Kp (for aurora); <0 = no data

    // False until the best-effort extras (Kp, model band, air quality) have been
    // fetched. The core forecast is rendered first; the extras fill in after.
    bool enriched = false;

    // Barometric tendency over the last 3h, read from hourly surface pressure at
    // parse time (current minus 3h-ago). hasPressureTrend is false when there
    // aren't enough past hours to compute it.
    bool hasPressureTrend = false;
    double pressureDelta3h = 0.0;   // hPa

    // Yesterday's high/low (from past_days=1) for the "vs yesterday" readout.
    bool hasYesterday = false;
    double yesterdayTempMax = 0.0;  // Celsius
    double yesterdayTempMin = 0.0;

    std::chrono::steady_clock::time_point fetchedAt;

    bool isValid() const {
        auto age = std::chrono::steady_clock::now() - fetchedAt;
        return std::chrono::duration_cast<std::chrono::minutes>(age).count() < 60;
    }
};

struct SavedLocation {
    GeoLocation geo;
    bool isDefault = false;
};

enum class AlertType {
    SevereWeather,
    TemperatureSwing,
    Rain,
    Snow,
    Hail,
    HighWind,
    FreezingPrecipitation,
    Official,  // issued by an official agency (e.g. US NWS), not derived locally
    SkyEvent   // informational: meteor shower / aurora / good stargazing
};

struct WeatherAlert {
    AlertType type;
    std::string title;
    std::string message;
    std::string locationName;
    std::chrono::system_clock::time_point timestamp;

    // Official (agency-issued) alerts. `official` flags them so the UI can rank
    // and style them above our locally-derived ones; `id` is the agency's stable
    // identifier, used for deduplication instead of the type+day heuristic.
    bool official = false;
    std::string id;
    std::string severity;  // e.g. "Extreme", "Severe", "Moderate" (official only)

    // When the alert's event has fully passed. Zero (epoch) means "no expiry".
    // Used to stop counting / prune notifications once their day is gone.
    std::chrono::system_clock::time_point validUntil{};

    std::string deduplicationKey() const;
};

// A logged alert in the notification center history.
struct Notification {
    WeatherAlert alert;
    std::chrono::system_clock::time_point time;
    bool acknowledged = false;  // user dismissed it from the banner
};

// Unit conversion helpers
inline double celsiusToFahrenheit(double c) { return c * 9.0 / 5.0 + 32.0; }
inline double fahrenheitToCelsius(double f) { return (f - 32.0) * 5.0 / 9.0; }
inline double kmhToMph(double kmh) { return kmh * 0.621371; }
inline double mphToKmh(double mph) { return mph / 0.621371; }
inline double mmToInches(double mm) { return mm * 0.0393701; }

// Short-range precipitation nowcast derived from the 15-minute series.
enum class NowcastState {
    NoData,        // no minutely data available
    Dry,           // dry now and staying dry through the window
    RainStarting,  // dry now, precip begins in `minutes`
    RainStopping,  // precip now, ends in `minutes`
    RainOngoing    // precip now and through the whole window
};

struct PrecipNowcast {
    NowcastState state = NowcastState::NoData;
    int minutes = 0;  // minutes until the start/stop transition
};

// Scan WeatherData::minutely for the next dry<->wet transition (next ~2h).
PrecipNowcast computePrecipNowcast(const WeatherData& data);

// WHO UV-index band ("Low".."Extreme").
const char* uvCategory(double uv);

// US AQI band ("Good".."Hazardous"). Returns "--" for a negative/no-data value.
const char* aqiCategory(double usAqi);

// One-sentence health guidance for a US AQI value (who should take care).
const char* aqiHealthGuidance(double usAqi);

// A single human-readable "takeaway" line for the day, picked by priority from
// the current/forecast data (precip onset, big swing, high UV, gusts, condition).
std::string dailyTakeaway(const WeatherData& data, bool useFahrenheit);

enum class PressureTrend { Falling, Steady, Rising };
// Classify a 3-hour barometric change (hPa): |delta| < 1 is steady.
PressureTrend classifyPressureTrend(double deltaHpa);

// WMO weather code to description
const char* weatherCodeToString(int code);
const char* weatherCodeToShortString(int code);

// Wind direction degrees to compass string
const char* windDirectionToString(double degrees);

// Map a WMO weather code to a cell in the 6x5 weather-icons atlas
// (index = row*6 + col). isNight picks the night variant where one exists.
// Returns -1 if there's no sensible icon.
int weatherCodeToIconCell(int code, bool isNight);

} // namespace wd
