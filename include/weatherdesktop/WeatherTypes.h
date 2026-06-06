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

    std::string displayName() const {
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
};

struct HourlyForecast {
    std::string time;               // "2026-02-25T14:00"
    double temperature = 0.0;       // Celsius
    int weatherCode = 0;
    double precipitation = 0.0;     // mm
    double precipProb = 0.0;        // %
    double windSpeed = 0.0;         // km/h
    double windDirection = 0.0;     // degrees
};

struct WeatherData {
    GeoLocation location;
    CurrentWeather current;
    std::vector<HourlyForecast> hourly;
    std::vector<DailyForecast> daily;
    int utcOffsetSeconds = 0;
    double kpIndex = -1.0;  // NOAA planetary Kp (for aurora); <0 = no data
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
    FreezingPrecipitation
};

struct WeatherAlert {
    AlertType type;
    std::string title;
    std::string message;
    std::string locationName;
    std::chrono::system_clock::time_point timestamp;

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
