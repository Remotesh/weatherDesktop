#include "test_framework.h"
#include "weatherdesktop/WeatherService.h"

#include <stdexcept>

using namespace wd;

// A representative Open-Meteo forecast response covering current, hourly (with
// the new snowfall field), the 15-minute nowcast, and daily blocks.
static const char* kForecastJson = R"({
  "utc_offset_seconds": -18000,
  "current": {
    "time": "2026-06-17T13:00",
    "temperature_2m": 21.5,
    "relative_humidity_2m": 55,
    "apparent_temperature": 22.0,
    "precipitation": 0.0,
    "rain": 0.0,
    "snowfall": 0.0,
    "weather_code": 3,
    "cloud_cover": 80,
    "pressure_msl": 1013.2,
    "dew_point_2m": 12.5,
    "visibility": 24000,
    "uv_index": 6.0,
    "wind_speed_10m": 10,
    "wind_direction_10m": 180,
    "wind_gusts_10m": 20
  },
  "hourly": {
    "time": ["2026-06-17T13:00", "2026-06-17T14:00", "2026-06-17T15:00"],
    "temperature_2m": [21.5, 22.0, 23.0],
    "weather_code": [3, 61, 71],
    "precipitation": [0.0, 0.5, 0.0],
    "snowfall": [0.0, 0.0, 1.2],
    "precipitation_probability": [10, 40, 30],
    "uv_index": [6.0, 5.0, 4.0],
    "surface_pressure": [1012, 1011, 1010],
    "wind_speed_10m": [10, 11, 12],
    "wind_direction_10m": [180, 190, 200]
  },
  "minutely_15": {
    "time": ["2026-06-17T13:00", "2026-06-17T13:15", "2026-06-17T13:30"],
    "precipitation": [0.0, 0.2, 0.0],
    "weather_code": [3, 61, 3]
  },
  "daily": {
    "time": ["2026-06-17", "2026-06-18"],
    "weather_code": [3, 61],
    "temperature_2m_max": [25, 26],
    "temperature_2m_min": [15, 16],
    "precipitation_sum": [0.5, 1.0],
    "rain_sum": [0.5, 1.0],
    "snowfall_sum": [0.0, 0.0],
    "precipitation_probability_max": [40, 60],
    "sunrise": ["2026-06-17T05:23", "2026-06-18T05:24"],
    "sunset": ["2026-06-17T20:54", "2026-06-18T20:55"],
    "uv_index_max": [7.0, 6.0],
    "daylight_duration": [55800, 55700]
  }
})";

TEST_CASE(parse_forecast_current_block) {
    GeoLocation loc;
    loc.name = "Test";
    WeatherData d = WeatherService::parseWeatherResponse(kForecastJson, loc);

    CHECK_EQ(d.utcOffsetSeconds, -18000);
    CHECK_NEAR(d.current.temperature, 21.5, 1e-9);
    CHECK_NEAR(d.current.humidity, 55.0, 1e-9);
    CHECK_NEAR(d.current.apparentTemp, 22.0, 1e-9);
    CHECK_EQ(d.current.weatherCode, 3);
    CHECK_NEAR(d.current.windDirection, 180.0, 1e-9);
    CHECK_STR_EQ(d.current.timestamp, "2026-06-17T13:00");
    // Tier-1 current metrics.
    CHECK_NEAR(d.current.pressure, 1013.2, 1e-6);
    CHECK_NEAR(d.current.dewPoint, 12.5, 1e-6);
    CHECK_NEAR(d.current.visibility, 24000.0, 1e-6);
    CHECK_NEAR(d.current.uvIndex, 6.0, 1e-9);
}

TEST_CASE(parse_forecast_hourly_with_snowfall) {
    GeoLocation loc;
    WeatherData d = WeatherService::parseWeatherResponse(kForecastJson, loc);

    CHECK_EQ((int)d.hourly.size(), 3);
    CHECK_NEAR(d.hourly[1].precipitation, 0.5, 1e-9);
    CHECK_NEAR(d.hourly[1].precipProb, 40.0, 1e-9);
    CHECK_NEAR(d.hourly[2].snowfall, 1.2, 1e-9);  // the newly-added field
    CHECK_EQ(d.hourly[2].weatherCode, 71);
    CHECK_NEAR(d.hourly[0].uvIndex, 6.0, 1e-9);
    CHECK_NEAR(d.hourly[1].pressure, 1011.0, 1e-6);
}

TEST_CASE(parse_forecast_minutely_nowcast) {
    GeoLocation loc;
    WeatherData d = WeatherService::parseWeatherResponse(kForecastJson, loc);

    CHECK_EQ((int)d.minutely.size(), 3);
    CHECK_NEAR(d.minutely[1].precipitation, 0.2, 1e-9);

    // End-to-end: parsed minutely series should drive the nowcast (dry now,
    // wet at the 2nd step -> rain starting in 15 minutes).
    PrecipNowcast nc = computePrecipNowcast(d);
    CHECK(nc.state == NowcastState::RainStarting);
    CHECK_EQ(nc.minutes, 15);
}

TEST_CASE(parse_forecast_daily_block) {
    GeoLocation loc;
    WeatherData d = WeatherService::parseWeatherResponse(kForecastJson, loc);

    CHECK_EQ((int)d.daily.size(), 2);
    CHECK_NEAR(d.daily[0].tempMax, 25.0, 1e-9);
    CHECK_NEAR(d.daily[0].rainSum, 0.5, 1e-9);
    CHECK_NEAR(d.daily[1].precipProbMax, 60.0, 1e-9);
    CHECK_FALSE(d.daily[0].hasUncertainty);  // no band without the second call
    // Tier-1 daily fields.
    CHECK_STR_EQ(d.daily[0].sunrise, "2026-06-17T05:23");
    CHECK_STR_EQ(d.daily[0].sunset, "2026-06-17T20:54");
    CHECK_NEAR(d.daily[0].uvIndexMax, 7.0, 1e-9);
    CHECK_NEAR(d.daily[0].daylightSeconds, 55800.0, 1e-6);
}

// past_days=1 prepends yesterday: today must be located by date (not index 0),
// yesterday's high/low captured, and the 3h pressure tendency computed from the
// extra past hours.
static const char* kPastDaysJson = R"({
  "utc_offset_seconds": 0,
  "current": {
    "time": "2026-06-17T13:00", "temperature_2m": 20, "weather_code": 1,
    "wind_direction_10m": 0
  },
  "hourly": {
    "time": ["2026-06-17T10:00", "2026-06-17T11:00", "2026-06-17T12:00",
             "2026-06-17T13:00", "2026-06-17T14:00"],
    "temperature_2m": [16, 17, 18, 20, 21],
    "weather_code": [1, 1, 1, 1, 1],
    "surface_pressure": [1010, 1011, 1012, 1015, 1016]
  },
  "daily": {
    "time": ["2026-06-16", "2026-06-17", "2026-06-18"],
    "weather_code": [1, 1, 1],
    "temperature_2m_max": [19, 25, 26],
    "temperature_2m_min": [9, 15, 16]
  }
})";

TEST_CASE(parse_forecast_pastdays_locates_today) {
    GeoLocation loc;
    WeatherData d = WeatherService::parseWeatherResponse(kPastDaysJson, loc);

    // Yesterday (2026-06-16) is dropped from the display list; today is first.
    CHECK_EQ((int)d.daily.size(), 2);
    CHECK_STR_EQ(d.daily[0].date, "2026-06-17");
    CHECK_NEAR(d.daily[0].tempMax, 25.0, 1e-9);

    // Yesterday captured for the "vs yesterday" readout.
    CHECK(d.hasYesterday);
    CHECK_NEAR(d.yesterdayTempMax, 19.0, 1e-9);
    CHECK_NEAR(d.yesterdayTempMin, 9.0, 1e-9);

    // Hourly display starts at "now" (13:00) -> 2 entries (13:00, 14:00).
    CHECK_EQ((int)d.hourly.size(), 2);

    // Pressure tendency: 1015 (now) - 1010 (3h ago) = +5 -> rising.
    CHECK(d.hasPressureTrend);
    CHECK_NEAR(d.pressureDelta3h, 5.0, 1e-6);
    CHECK(classifyPressureTrend(d.pressureDelta3h) == PressureTrend::Rising);
}

TEST_CASE(parse_air_quality) {
    const char* json = R"({
      "current": {"us_aqi": 42, "european_aqi": 30, "pm2_5": 9.1, "pm10": 14.0, "ozone": 60}
    })";
    AirQuality aq = WeatherService::parseAirQuality(json);
    CHECK(aq.valid);
    CHECK_NEAR(aq.usAqi, 42.0, 1e-9);
    CHECK_NEAR(aq.pm2_5, 9.1, 1e-6);
    CHECK_STR_EQ(aqiCategory(aq.usAqi), "Good");
}

TEST_CASE(parse_air_quality_empty_is_invalid) {
    AirQuality aq = WeatherService::parseAirQuality(R"({"latitude": 45.0})");
    CHECK_FALSE(aq.valid);
}

TEST_CASE(parse_forecast_error_body_throws) {
    GeoLocation loc;
    bool threw = false;
    try {
        WeatherService::parseWeatherResponse(
            R"({"error": true, "reason": "Invalid coordinates"})", loc);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE(parse_geocode_results) {
    const char* json = R"({
      "results": [
        {"name": "Portland", "country_code": "US", "admin1": "Oregon",
         "latitude": 45.52, "longitude": -122.68, "timezone": "America/Los_Angeles"},
        {"name": "Portland", "country_code": "US", "admin1": "Maine",
         "latitude": 43.66, "longitude": -70.26, "timezone": "America/New_York"}
      ]
    })";
    auto results = WeatherService::parseGeocodeResponse(json);
    CHECK_EQ((int)results.size(), 2);
    CHECK_STR_EQ(results[0].name, "Portland");
    CHECK_STR_EQ(results[0].country, "US");
    CHECK_STR_EQ(results[0].admin1, "Oregon");
    CHECK_NEAR(results[0].latitude, 45.52, 1e-6);
    CHECK_STR_EQ(results[1].admin1, "Maine");
}

TEST_CASE(parse_geocode_no_results) {
    auto results = WeatherService::parseGeocodeResponse(R"({"generationtime_ms": 0.1})");
    CHECK(results.empty());
}
