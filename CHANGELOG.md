# Changelog

## v1.2.0 — 2026-06-23

An in-app weather radar, a rotating info carousel, and light/dark themes.

### Added
- **Weather radar** — an in-app, animated radar overlay (RainViewer past +
  nowcast frames) with play/scrub controls and per-frame timestamps ("2:10 PM
  (-20 min)" / "+30 min forecast"). Drag to pan, wheel to zoom. A locally-drawn
  **Natural Earth basemap** (coastlines, state, country and US county lines,
  interstates/motorways, and city labels) means only RainViewer ever sees the
  viewport. Includes an on-map **wind gauge** for the centered location.
  - Tiles are fetched on a small connection-reused pool with rate-limit backoff;
    deep zoom **upscales the nearest data instead of going blank**, so the view is
    never empty even past the radar's native resolution.
- **Auto-cycling SKY carousel** — the Sky panel rotates through Sky / Air /
  Discussion / Almanac / Tides cards (pauses on hover), surfacing:
  - **Air quality breakdown** — US AQI with a pollutant table and health guidance.
  - **Forecast discussion** — the NWS Area Forecast Discussion narrative (US).
  - **Almanac** — today vs. the 1991–2020 climate normal.
  - **Tides & marine** — next high/low tide (NOAA, US) and wave height.
- **Daily takeaway** — a one-line plain-language summary atop the current
  conditions, which now split into a static header + a scrollable metrics list.
- **Rain push alerts** — a notification when rain is about to start; **clicking a
  notification opens the app**.
- **Light / dark themes** — selectable in Settings (window chrome included).

## v1.1.0 — 2026-06-17

More accurate data, more useful panels, and a faster, snappier app.

### Added
- **Rain nowcast** — minute-by-minute "rain starting/stopping in ~N min" from
  Open-Meteo's 15-minute precipitation series.
- **Official US alerts** — NWS active watches/warnings, ranked above the
  locally-derived alerts.
- **Air quality** (US AQI), **UV index**, **dew point**, **visibility**, and
  **barometric pressure** with a 3-hour rising/steady/falling tendency.
- **Forecast confidence band** — a cross-model spread chip (high/med/low) per day
  in the 7-day forecast, with the deterministic high/low clamped inside its band.
- **24-hour trend chart** — temperature line + precipitation bars aligned to the
  hourly columns, with a per-hour breakdown (incl. rain/snow amounts) on hover.
- **Sky panel** — a sunrise→sunset arc with the sun at the current time and a
  contextual "Sunset/Sunrise in …" headline, alongside the moon/aurora/meteors.
- **Tray quick-look** — a compact weather card on tray-icon hover.
- **Location options** — exact-coordinate entry and one-shot OS geolocation
  (Windows), plus a "vs yesterday" comparison.
- **Unit tests** — a zero-dependency test runner wired into CTest.

### Changed
- Forecasts now use Open-Meteo's higher-resolution regional models via
  `best_match`, with elevation-aware downscaling.
- The hourly card has a single horizontal scroll and no longer scrolls
  vertically; the current-conditions card is a compact two-column layout.

### Fixed
- **Faster startup** — the core forecast paints immediately; the extras
  (air quality, model band, aurora, official alerts) fill in right after instead
  of blocking the first paint.
- **Faster quit** — in-flight network requests are aborted on exit, and a
  shutdown no longer kicks off a stray background fetch, so the app closes
  promptly instead of waiting out request timeouts.

## v1.0.0 — 2026-06-05

First public release: a native tray weather app with current conditions, a
24-hour and 7-day forecast, smart alerts with quiet-hours/digest scheduling, and
a "Tonight" panel (moon phase, aurora chance, meteor showers).
