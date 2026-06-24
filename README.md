# Weather Desktop

A small, native **Windows weather app** for the desktop — current conditions, a
24-hour and 7-day forecast with pixel-art weather icons, official + derived
weather alerts, air quality, a minute-by-minute rain nowcast, an **in-app weather
radar**, and a rotating "Sky" panel with a sunrise→sunset arc, moon phase, aurora
odds, meteor showers, air quality, the NWS forecast discussion, almanac normals,
and tides. Light/dark themes. It lives in the system tray and shows a quick-look
card on hover.

![Weather Desktop](docs/screenshot.png)

Built with SDL2 + [Dear ImGui](https://github.com/ocornut/imgui) + OpenGL.
Weather, air quality and geocoding from [Open-Meteo](https://open-meteo.com/)
(no API key); official US alerts from the [NWS API](https://www.weather.gov/documentation/services-web-api);
aurora visibility from the free [NOAA SWPC](https://www.swpc.noaa.gov/) planetary
K-index feed.

## Features

- **Current conditions** — temperature, feels-like, humidity, dew point,
  wind/gusts, precipitation, cloud cover, visibility, UV index, **air quality**
  (US AQI), and barometric **pressure with a 3-hour rising/steady/falling
  tendency**.
- **Rain nowcast** — a minute-by-minute "rain starting/stopping in ~N min" line
  from Open-Meteo's 15-minute precipitation series.
- **Forecasts** — a 24-hour view with an aligned temperature/precip **trend
  chart** (hover any column for a per-hour breakdown including rain/snow
  amounts), and a 7-day outlook showing a **cross-model confidence** chip per day.
- **Alerts** — official **US NWS watches/warnings** plus locally-derived alerts
  (severe weather, temperature swings, rain/snow/hail, high wind, freezing
  precipitation), surfaced as floating toasts and OS notifications.
- **Notification scheduling** — deliver alerts as they happen, only outside quiet
  hours, or as a once-a-day digest; a notification center keeps the history.
- **Weather radar** — an in-app, animated radar overlay ([RainViewer](https://www.rainviewer.com/)
  past + nowcast frames) with play/scrub, per-frame timestamps, drag-pan and
  wheel-zoom, an on-map wind gauge, and a locally-drawn **Natural Earth basemap**
  (coastlines, state/country/county lines, interstates, city labels) — so only
  RainViewer sees the viewport.
- **Sky carousel** — an auto-cycling panel (pauses on hover) rotating through a
  **sunrise→sunset arc** (sun at the current time, moon phase, aurora odds, meteor
  showers), an **air-quality** breakdown, the **NWS Area Forecast Discussion**,
  an **almanac** (today vs. the 1991–2020 normal), and **tides & marine**.
- **Daily takeaway** — a one-line plain-language summary of the day atop the
  current conditions.
- **Tray quick-look** — hover the tray icon for a compact card (temp, condition,
  feels-like, high/low, wind, rain) without opening the window.
- **Locations & look** — search by city/zip, enter exact coordinates, or detect
  your location via the OS (Windows); light/dark **themes**; °F/°C + mph/km-h,
  "vs yesterday" comparison, start-minimized, and a system-tray presence.

## Build (Windows, MSYS2 / mingw-w64)

Install the toolchain and dependencies:

```
pacman -S --needed \
    mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
    mingw-w64-x86_64-SDL2 mingw-w64-x86_64-curl mingw-w64-x86_64-nlohmann-json
```

Then configure and build:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable lands at `build/WeatherDesktop.exe`. The `resources/` folder
(font + sprite sheets) is copied next to it automatically; ship them together.

## Tests

A small zero-dependency unit-test runner covers the pure logic (forecast/AQI
parsing, alert rules, nowcast, sky events, unit conversions, the thread-safe
queue). It builds as a separate target and is wired into CTest:

```
cmake --build build --target wd_tests
ctest --test-dir build --output-on-failure
```

## Credits

- Font: **Kenney Space** ([kenney.nl](https://kenney.nl/), CC0).
- Weather and moon-phase sprite sheets © their author, included with the app.
- Weather, air quality, marine and climate data: **[Open-Meteo](https://open-meteo.com/)**
  (CC BY 4.0). Official alerts & forecast discussion: **US NWS**. Tides: **NOAA CO-OPS**.
  Space weather: **NOAA SWPC**.
- Weather radar: **[RainViewer](https://www.rainviewer.com/)**. Radar basemap:
  **[Natural Earth](https://www.naturalearthdata.com/)** (public domain).

## License

MIT — see [LICENSE](LICENSE).
