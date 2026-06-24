#include "weatherdesktop/AlertEngine.h"
#include "weatherdesktop/SkyEvents.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace wd {

// A system_clock time_point at 23:59:59 local time, `dayOffset` days from today.
// Alerts use this for validUntil so a notification stops counting once its day
// has fully passed.
static std::chrono::system_clock::time_point endOfLocalDay(int dayOffset) {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    lt.tm_mday += dayOffset;
    lt.tm_hour = 23;
    lt.tm_min = 59;
    lt.tm_sec = 59;
    lt.tm_isdst = -1;
    std::time_t end = std::mktime(&lt);  // normalizes the day overflow
    return std::chrono::system_clock::from_time_t(end);
}

// True if the daily forecast for today+dayOffset reads clear / mainly clear --
// i.e. good odds for stargazing that night.
static bool skiesClearOn(const WeatherData& data, int dayOffset) {
    if (dayOffset < 0 || dayOffset >= static_cast<int>(data.daily.size())) return false;
    return data.daily[dayOffset].weatherCode <= 1;
}

bool AlertEngine::isSevereWeatherCode(int code) const {
    // Severe WMO codes: freezing precip, heavy rain/snow, violent showers, thunderstorms
    static const int severeCodes[] = {56, 57, 65, 66, 67, 75, 82, 86, 95, 96, 99};
    for (int c : severeCodes) {
        if (code == c) return true;
    }
    return false;
}

bool AlertEngine::hasFreezingPrecipitation(int code) const {
    return code == 56 || code == 57 || code == 66 || code == 67;
}

bool AlertEngine::hasHail(const WeatherData& data) const {
    if (data.current.weatherCode == 96 || data.current.weatherCode == 99) return true;
    for (auto& day : data.daily) {
        if (day.weatherCode == 96 || day.weatherCode == 99) return true;
    }
    return false;
}

AlertEngine::TempSwingInfo AlertEngine::findTempSwing(const std::vector<DailyForecast>& daily) const {
    TempSwingInfo info;
    if (daily.size() < 2) return info;

    double maxSwing = 0.0;
    for (size_t i = 0; i + 1 < daily.size() && i < 3; ++i) {
        // Warming: next day's high vs current day's low
        double warmSwing = daily[i + 1].tempMax - daily[i].tempMin;
        if (warmSwing > 8.3 && warmSwing > maxSwing) {
            maxSwing = warmSwing;
            info.detected = true;
            info.isWarming = true;
            info.targetTemp = daily[i + 1].tempMax;
            info.dayOffset = static_cast<int>(i + 1);
        }
        // Cooling: current day's high vs next day's low
        double coolSwing = daily[i].tempMax - daily[i + 1].tempMin;
        if (coolSwing > 8.3 && coolSwing > maxSwing) {
            maxSwing = coolSwing;
            info.detected = true;
            info.isWarming = false;
            info.targetTemp = daily[i + 1].tempMin;
            info.dayOffset = static_cast<int>(i + 1);
        }
    }
    return info;
}

bool AlertEngine::hasPrecipitation(const WeatherData& data) const {
    // Check today and tomorrow
    for (size_t i = 0; i < std::min<size_t>(2, data.daily.size()); ++i) {
        if (data.daily[i].rainSum > 0 || data.daily[i].precipProbMax > 60) return true;
    }
    return false;
}

bool AlertEngine::hasSnow(const WeatherData& data) const {
    for (size_t i = 0; i < std::min<size_t>(2, data.daily.size()); ++i) {
        if (data.daily[i].snowfallSum > 0) return true;
    }
    return false;
}

bool AlertEngine::hasHighWind(const WeatherData& data) const {
    return data.current.windGusts > 64.0; // 40 mph in km/h
}

std::vector<WeatherAlert> AlertEngine::evaluate(const WeatherData& data, bool useFahrenheit) const {
    std::vector<WeatherAlert> alerts;
    auto now = std::chrono::system_clock::now();

    // Severe weather (current)
    if (isSevereWeatherCode(data.current.weatherCode)) {
        WeatherAlert alert;
        alert.type = AlertType::SevereWeather;
        alert.title = "Severe Weather";
        alert.message = std::string(weatherCodeToString(data.current.weatherCode)) +
                        " in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // Severe weather (forecast - today/tomorrow)
    for (size_t i = 0; i < std::min<size_t>(2, data.daily.size()); ++i) {
        if (isSevereWeatherCode(data.daily[i].weatherCode) &&
            data.daily[i].weatherCode != data.current.weatherCode) {
            WeatherAlert alert;
            alert.type = AlertType::SevereWeather;
            alert.title = "Severe Weather Forecast";
            alert.message = std::string(weatherCodeToString(data.daily[i].weatherCode)) +
                            " expected " + (i == 0 ? "today" : "tomorrow") +
                            " in " + data.location.name;
            alert.locationName = data.location.name;
            alert.timestamp = now;
            alerts.push_back(alert);
        }
    }

    // Temperature swing
    auto swingInfo = findTempSwing(data.daily);
    if (swingInfo.detected) {
        WeatherAlert alert;
        alert.type = AlertType::TemperatureSwing;
        alert.title = "Temperature Change";

        char tempBuf[32];
        if (useFahrenheit) {
            std::snprintf(tempBuf, sizeof(tempBuf), "%.0fF",
                          celsiusToFahrenheit(swingInfo.targetTemp));
        } else {
            std::snprintf(tempBuf, sizeof(tempBuf), "%.0fC", swingInfo.targetTemp);
        }

        const char* direction = swingInfo.isWarming ? "warming to" : "dropping to";
        const char* when = (swingInfo.dayOffset == 1) ? "tomorrow" : "in the next few days";

        alert.message = std::string("Temps ") + direction + " " + tempBuf +
                        " " + when + " in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // Rain
    if (hasPrecipitation(data)) {
        WeatherAlert alert;
        alert.type = AlertType::Rain;
        alert.title = "Rain Expected";
        std::string when = "today";
        if (data.daily.size() >= 2 && data.daily[0].rainSum <= 0 && data.daily[0].precipProbMax <= 60) {
            when = "tomorrow";
        }
        alert.message = "Rain expected " + when + " in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // Snow
    if (hasSnow(data)) {
        WeatherAlert alert;
        alert.type = AlertType::Snow;
        alert.title = "Snow Expected";
        alert.message = "Snowfall expected in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // Hail
    if (hasHail(data)) {
        WeatherAlert alert;
        alert.type = AlertType::Hail;
        alert.title = "Hail Warning";
        alert.message = "Thunderstorm with hail in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // High wind
    if (hasHighWind(data)) {
        WeatherAlert alert;
        alert.type = AlertType::HighWind;
        alert.title = "High Wind";
        alert.message = "Wind gusts over 40 mph in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // Freezing precipitation
    if (hasFreezingPrecipitation(data.current.weatherCode)) {
        WeatherAlert alert;
        alert.type = AlertType::FreezingPrecipitation;
        alert.title = "Freezing Precipitation";
        alert.message = std::string(weatherCodeToString(data.current.weatherCode)) +
                        " in " + data.location.name;
        alert.locationName = data.location.name;
        alert.timestamp = now;
        alerts.push_back(alert);
    }

    // Stamp each alert with when its event has passed, so a notification stops
    // counting / gets pruned once its day is gone. Forecast-oriented alerts stay
    // valid through tomorrow; "right now" conditions expire at end of today.
    for (auto& a : alerts) {
        switch (a.type) {
            case AlertType::Rain:
            case AlertType::Snow:
            case AlertType::Hail:
            case AlertType::TemperatureSwing:
            case AlertType::SevereWeather:
                a.validUntil = endOfLocalDay(1);
                break;
            default:  // HighWind, FreezingPrecipitation
                a.validUntil = endOfLocalDay(0);
                break;
        }
    }

    return alerts;
}

std::vector<WeatherAlert> AlertEngine::evaluateSkyEvents(const WeatherData& data,
                                                         std::time_t now) const {
    std::vector<WeatherAlert> out;
    auto sysNow = std::chrono::system_clock::now();

    // Notable meteor shower approaching or at peak. Limit to the strong,
    // worth-staying-up-for showers within a couple nights so it's advance notice,
    // not nightly noise.
    auto showers = activeMeteorShowers(now);
    if (!showers.empty()) {
        const ActiveShower& s = showers.front();  // closest to peak
        if (s.zhr >= 40 && s.daysToPeak >= -1 && s.daysToPeak <= 2) {
            WeatherAlert a;
            a.type = AlertType::SkyEvent;
            a.title = "Meteor Shower";
            std::string when;
            if (s.nearPeak || s.daysToPeak <= 0) when = "tonight";
            else if (s.daysToPeak == 1) when = "tomorrow night";
            else when = "in " + std::to_string(s.daysToPeak) + " nights";
            char buf[160];
            std::snprintf(buf, sizeof buf, "%s peaks %s (up to ~%d/hr)",
                          s.name, when.c_str(), s.zhr);
            a.message = buf;
            if (skiesClearOn(data, std::max(0, s.daysToPeak)))
                a.message += " - clear skies expected";
            a.locationName = data.location.name;
            a.timestamp = sysNow;
            a.validUntil = endOfLocalDay(std::max(0, s.daysToPeak));
            out.push_back(a);
        }
    }

    // Aurora likely tonight for this latitude.
    if (auroraChance(data.kpIndex, data.location.latitude) == AuroraChance::Likely) {
        WeatherAlert a;
        a.type = AlertType::SkyEvent;
        a.title = "Aurora";
        char buf[120];
        std::snprintf(buf, sizeof buf, "Aurora likely tonight (Kp %.0f)", data.kpIndex);
        a.message = buf;
        if (skiesClearOn(data, 0)) a.message += " - clear skies expected";
        a.locationName = data.location.name;
        a.timestamp = sysNow;
        a.validUntil = endOfLocalDay(0);
        out.push_back(a);
    }

    return out;
}

void AlertEngine::markNotified(const WeatherAlert& alert) {
    std::lock_guard<std::mutex> lock(alertMutex_);
    recentAlerts_[alert.deduplicationKey()] = std::chrono::steady_clock::now();
}

bool AlertEngine::wasRecentlyNotified(const WeatherAlert& alert) const {
    std::lock_guard<std::mutex> lock(alertMutex_);
    auto it = recentAlerts_.find(alert.deduplicationKey());
    if (it == recentAlerts_.end()) return false;
    auto age = std::chrono::steady_clock::now() - it->second;
    return std::chrono::duration_cast<std::chrono::hours>(age).count() < 6;
}

} // namespace wd
