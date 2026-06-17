#include "test_framework.h"
#include "weatherdesktop/WeatherTypes.h"

using namespace wd;

TEST_CASE(unit_conversions) {
    CHECK_NEAR(celsiusToFahrenheit(0.0), 32.0, 1e-9);
    CHECK_NEAR(celsiusToFahrenheit(100.0), 212.0, 1e-9);
    CHECK_NEAR(fahrenheitToCelsius(32.0), 0.0, 1e-9);
    CHECK_NEAR(fahrenheitToCelsius(212.0), 100.0, 1e-9);
    CHECK_NEAR(kmhToMph(100.0), 62.1371, 1e-3);
    CHECK_NEAR(mphToKmh(kmhToMph(100.0)), 100.0, 1e-6);  // round trip
    CHECK_NEAR(mmToInches(25.4), 1.0, 1e-3);
}

TEST_CASE(weather_code_strings) {
    CHECK_STR_EQ(weatherCodeToString(0), "Clear Sky");
    CHECK_STR_EQ(weatherCodeToString(95), "Thunderstorm");
    CHECK_STR_EQ(weatherCodeToString(99), "Severe Thunderstorm w/ Hail");
    CHECK_STR_EQ(weatherCodeToString(123456), "Unknown");
    CHECK_STR_EQ(weatherCodeToShortString(0), "Clear");
    CHECK_STR_EQ(weatherCodeToShortString(123456), "???");
}

TEST_CASE(wind_direction_compass) {
    CHECK_STR_EQ(windDirectionToString(0.0), "N");
    CHECK_STR_EQ(windDirectionToString(90.0), "E");
    CHECK_STR_EQ(windDirectionToString(180.0), "S");
    CHECK_STR_EQ(windDirectionToString(270.0), "W");
    CHECK_STR_EQ(windDirectionToString(45.0), "NE");
    CHECK_STR_EQ(windDirectionToString(360.0), "N");   // wraps
    CHECK_STR_EQ(windDirectionToString(359.0), "N");   // rounds to N
    CHECK_STR_EQ(windDirectionToString(-1.0), "N/A");  // no data sentinel
}

TEST_CASE(weather_icon_cell_day_night) {
    CHECK_EQ(weatherCodeToIconCell(0, false), 0);    // clear day
    CHECK_EQ(weatherCodeToIconCell(0, true), 24);    // clear night
    CHECK_EQ(weatherCodeToIconCell(95, false), 12);  // tstorm day
    CHECK_EQ(weatherCodeToIconCell(95, true), 29);   // tstorm night
    CHECK_EQ(weatherCodeToIconCell(123456, false), -1);  // unknown -> no icon
}

TEST_CASE(geolocation_cache_key) {
    GeoLocation g;
    g.name = "Portland";
    g.latitude = 45.51520;
    g.longitude = -122.67840;
    // name@lat,lon with 4 decimal places.
    CHECK_STR_EQ(g.cacheKey(), "Portland@45.5152,-122.6784");
}

TEST_CASE(alert_dedup_key_official_vs_derived) {
    WeatherAlert official;
    official.official = true;
    official.id = "urn:oid:2.49.0.1.840.0.abc";
    CHECK_STR_EQ(official.deduplicationKey(), "OFFICIAL|urn:oid:2.49.0.1.840.0.abc");

    // An official alert with no id falls back to the type/day heuristic.
    WeatherAlert officialNoId;
    officialNoId.official = true;
    officialNoId.type = AlertType::Official;
    officialNoId.locationName = "Town";
    CHECK_STR_EQ(officialNoId.deduplicationKey(),
                 std::to_string(static_cast<int>(AlertType::Official)) + "|Town|0");

    WeatherAlert derived;
    derived.type = AlertType::Rain;  // enum index 2
    derived.locationName = "Town";
    CHECK_STR_EQ(derived.deduplicationKey(), "2|Town|0");
}

// --- Precipitation nowcast (the new minutely logic) ---

static MinutelyForecast step(double precip) {
    MinutelyForecast m;
    m.precipitation = precip;
    return m;
}

TEST_CASE(nowcast_no_data) {
    WeatherData d;  // empty minutely
    CHECK(computePrecipNowcast(d).state == NowcastState::NoData);
}

TEST_CASE(nowcast_dry_window) {
    WeatherData d;
    d.minutely = {step(0.0), step(0.0), step(0.02), step(0.0)};  // all under threshold
    PrecipNowcast nc = computePrecipNowcast(d);
    CHECK(nc.state == NowcastState::Dry);
}

TEST_CASE(nowcast_rain_starting) {
    WeatherData d;
    d.minutely = {step(0.0), step(0.0), step(0.3), step(0.4)};  // dry now, wet at idx 2
    PrecipNowcast nc = computePrecipNowcast(d);
    CHECK(nc.state == NowcastState::RainStarting);
    CHECK_EQ(nc.minutes, 30);  // 2 steps * 15 min
}

TEST_CASE(nowcast_rain_stopping) {
    WeatherData d;
    d.minutely = {step(0.5), step(0.0), step(0.0)};  // wet now, dry at idx 1
    PrecipNowcast nc = computePrecipNowcast(d);
    CHECK(nc.state == NowcastState::RainStopping);
    CHECK_EQ(nc.minutes, 15);
}

TEST_CASE(nowcast_rain_ongoing) {
    WeatherData d;
    d.minutely = {step(0.5), step(0.6), step(0.2)};  // wet throughout
    PrecipNowcast nc = computePrecipNowcast(d);
    CHECK(nc.state == NowcastState::RainOngoing);
}

// --- Tier-1 categorization helpers ---

TEST_CASE(uv_category_bands) {
    CHECK_STR_EQ(uvCategory(0.0), "Low");
    CHECK_STR_EQ(uvCategory(2.9), "Low");
    CHECK_STR_EQ(uvCategory(3.0), "Moderate");
    CHECK_STR_EQ(uvCategory(6.0), "High");
    CHECK_STR_EQ(uvCategory(8.0), "Very High");
    CHECK_STR_EQ(uvCategory(11.0), "Extreme");
    CHECK_STR_EQ(uvCategory(15.0), "Extreme");
}

TEST_CASE(aqi_category_bands) {
    CHECK_STR_EQ(aqiCategory(-1.0), "--");  // no data
    CHECK_STR_EQ(aqiCategory(0.0), "Good");
    CHECK_STR_EQ(aqiCategory(50.0), "Good");
    CHECK_STR_EQ(aqiCategory(75.0), "Moderate");
    CHECK_STR_EQ(aqiCategory(120.0), "Unhealthy (Sensitive)");
    CHECK_STR_EQ(aqiCategory(180.0), "Unhealthy");
    CHECK_STR_EQ(aqiCategory(250.0), "Very Unhealthy");
    CHECK_STR_EQ(aqiCategory(400.0), "Hazardous");
}

TEST_CASE(pressure_trend_classification) {
    CHECK(classifyPressureTrend(3.0) == PressureTrend::Rising);
    CHECK(classifyPressureTrend(1.0) == PressureTrend::Rising);
    CHECK(classifyPressureTrend(0.0) == PressureTrend::Steady);
    CHECK(classifyPressureTrend(0.5) == PressureTrend::Steady);
    CHECK(classifyPressureTrend(-0.5) == PressureTrend::Steady);
    CHECK(classifyPressureTrend(-1.0) == PressureTrend::Falling);
    CHECK(classifyPressureTrend(-4.0) == PressureTrend::Falling);
}
