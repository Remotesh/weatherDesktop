# Weather Desktop

A small, native **Windows weather app** for the desktop — current conditions, a
24-hour and 7-day forecast with pixel-art weather icons, severe-weather alerts,
and a "Tonight" panel with the moon phase, aurora odds, and active meteor
showers. It lives in the system tray and notifies you when the weather turns.

Built with SDL2 + [Dear ImGui](https://github.com/ocornut/imgui) + OpenGL.
Weather data from [Open-Meteo](https://open-meteo.com/) (no API key); aurora
visibility from the free [NOAA SWPC](https://www.swpc.noaa.gov/) planetary
K-index feed.

## Features

- **Current conditions** — temperature, feels-like, humidity, wind/gusts,
  precip, cloud cover, with a condition sprite.
- **Forecasts** — scrollable 24-hour view and a full 7-day outlook.
- **Alerts** — severe weather, temperature swings, rain/snow/hail, high wind and
  freezing precipitation, surfaced as floating toasts and OS notifications.
- **Notification scheduling** — deliver alerts as they happen, only outside
  quiet hours, or as a once-a-day digest.
- **Notification center** — dismiss/acknowledge alerts and review recent history.
- **Tonight** — moon phase (with illumination), aurora chance for your latitude,
  and the night's active meteor showers.
- **Multiple locations**, °F/°C + mph/km-h, start-minimized, and a system-tray
  presence.

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

## Credits

- Font: **Kenney Space** ([kenney.nl](https://kenney.nl/), CC0).
- Weather and moon-phase sprite sheets © their author, included with the app.
- Weather: Open-Meteo. Space weather: NOAA SWPC.

## License

MIT — see [LICENSE](LICENSE).
