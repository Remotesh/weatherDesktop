#pragma once

#include "WeatherTypes.h"
#include <vector>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <ctime>

namespace wd {

class AlertEngine {
public:
    // useFahrenheit only affects how the temperature-swing message is formatted.
    // Weather-derived alerts only (deterministic); each carries a validUntil.
    std::vector<WeatherAlert> evaluate(const WeatherData& data, bool useFahrenheit) const;

    // Informational sky events (meteor-shower-near-peak, aurora-likely), which
    // depend on the date -- `now` is injected so this is testable. Kept separate
    // from evaluate() so the weather-alert tests stay date-independent.
    std::vector<WeatherAlert> evaluateSkyEvents(const WeatherData& data,
                                                std::time_t now) const;

    void markNotified(const WeatherAlert& alert);
    bool wasRecentlyNotified(const WeatherAlert& alert) const;

private:
    struct TempSwingInfo {
        bool detected = false;
        bool isWarming = false;
        double targetTemp = 0.0;
        int dayOffset = 0;
    };

    bool isSevereWeatherCode(int code) const;
    TempSwingInfo findTempSwing(const std::vector<DailyForecast>& daily) const;
    bool hasPrecipitation(const WeatherData& data) const;
    bool hasSnow(const WeatherData& data) const;
    bool hasHail(const WeatherData& data) const;
    bool hasHighWind(const WeatherData& data) const;
    bool hasFreezingPrecipitation(int code) const;

    mutable std::mutex alertMutex_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> recentAlerts_;
};

} // namespace wd
