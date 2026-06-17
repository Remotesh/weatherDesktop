#include "test_framework.h"
#include "weatherdesktop/AlertEngine.h"

using namespace wd;

namespace {

DailyForecast day(int code, double max, double min,
                  double rainSum = 0.0, double snowSum = 0.0, double prob = 0.0) {
    DailyForecast d;
    d.weatherCode = code;
    d.tempMax = max;
    d.tempMin = min;
    d.rainSum = rainSum;
    d.snowfallSum = snowSum;
    d.precipProbMax = prob;
    return d;
}

bool hasType(const std::vector<WeatherAlert>& alerts, AlertType t) {
    for (const auto& a : alerts) if (a.type == t) return true;
    return false;
}

// A benign baseline: clear now, two mild days, no precip, no big swing.
WeatherData calmDay() {
    WeatherData d;
    d.location.name = "Testville";
    d.current.weatherCode = 0;
    d.current.windGusts = 10.0;
    d.daily = {day(1, 20, 13, 0.0, 0.0, 10.0), day(1, 21, 14, 0.0, 0.0, 10.0)};
    return d;
}

}  // namespace

TEST_CASE(alert_calm_day_is_quiet) {
    AlertEngine engine;
    auto alerts = engine.evaluate(calmDay(), true);
    CHECK(alerts.empty());
}

TEST_CASE(alert_high_wind) {
    AlertEngine engine;
    WeatherData d = calmDay();
    d.current.windGusts = 70.0;  // > 64 km/h (~40 mph) threshold
    CHECK(hasType(engine.evaluate(d, true), AlertType::HighWind));
}

TEST_CASE(alert_severe_current_code) {
    AlertEngine engine;
    WeatherData d = calmDay();
    d.current.weatherCode = 95;  // thunderstorm
    CHECK(hasType(engine.evaluate(d, true), AlertType::SevereWeather));
}

TEST_CASE(alert_freezing_precipitation) {
    AlertEngine engine;
    WeatherData d = calmDay();
    d.current.weatherCode = 56;  // freezing drizzle
    CHECK(hasType(engine.evaluate(d, true), AlertType::FreezingPrecipitation));
}

TEST_CASE(alert_hail) {
    AlertEngine engine;
    WeatherData d = calmDay();
    d.current.weatherCode = 96;  // thunderstorm with hail
    CHECK(hasType(engine.evaluate(d, true), AlertType::Hail));
}

TEST_CASE(alert_rain_expected) {
    AlertEngine engine;
    WeatherData d = calmDay();
    d.daily[0] = day(61, 18, 12, /*rainSum=*/6.0, 0.0, 80.0);
    CHECK(hasType(engine.evaluate(d, true), AlertType::Rain));
}

TEST_CASE(alert_snow_expected) {
    AlertEngine engine;
    WeatherData d = calmDay();
    d.daily[0] = day(73, 1, -4, 0.0, /*snowSum=*/5.0, 70.0);
    CHECK(hasType(engine.evaluate(d, true), AlertType::Snow));
}

TEST_CASE(alert_temperature_swing) {
    AlertEngine engine;
    WeatherData d = calmDay();
    // Warming swing: tomorrow's high (22) far above today's low (5) -> > 8.3 C.
    d.daily = {day(1, 10, 5), day(1, 22, 16)};
    CHECK(hasType(engine.evaluate(d, true), AlertType::TemperatureSwing));
}
