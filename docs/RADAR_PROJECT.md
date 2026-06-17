# OpenRadar — a privacy-clean, open-source weather radar (spin-off)

A **separate** project (own repo, own license) that renders a weather-radar map
from **public-domain data only**, so Weather Desktop can show radar without ever
sending a user's viewport to a third-party tile host (the privacy problem with
RainViewer/OSM/Google tiles). Everything is fetched from US-government open data
and drawn locally.

## Why a separate project
- Radar has a heavyweight data pipeline (GRIB2 decode, reprojection, vector
  basemaps) that doesn't belong in the lightweight desktop app.
- It can ship on its own cadence and be reused by other apps.
- Weather Desktop consumes it through a thin seam (see "Integration").

## Data sources — all free, no key, public domain
| Layer | Source | Format | Notes |
|---|---|---|---|
| **Reflectivity mosaic (national)** | **NOAA MRMS** via NOAA Open Data Dissemination (NODD) on AWS/GCS | GRIB2 | `MergedReflectivityQCComposite` — one CONUS grid, updated ~2 min. Easiest start. Public domain. |
| Per-site radar (higher res) | **NEXRAD Level II** (`noaa-nexrad-level2` S3 bucket) / Level III products | Level II / III | Per-radar, ~88 sites. Heavier decode; phase 3. |
| Coastlines / state & county borders / rivers / lakes / urban | **Natural Earth** (public domain) and/or **US Census TIGER/Line** | Shapefile / GeoJSON | Vector basemap drawn locally — no tile server. |

All egress is to NOAA/AWS public buckets — same trust level as the Open-Meteo
calls the app already makes, and **no per-tile request that leaks where the user
is looking**. Optionally cache frames on disk for offline/replay.

## Pipeline
```
NODD (HTTPS, no key)
   └─ latest MRMS GRIB2  ──► GRIB2 decode ──► dBZ grid (lat/lon)
                                                  │
Natural Earth vectors ──► load once               ▼
                                          dBZ ──► NWS color ramp ──► RGBA raster
                                                  │
            viewport (bbox+zoom) ──► reproject (equirect/Mercator) ──► compose:
                              basemap vectors under, reflectivity over, alpha-blended
                                                  ▼
                                         frame (RGBA) or draw calls
```

- **GRIB2 decode:** ECMWF **eccodes** (Apache-2.0) is the pragmatic choice; a
  minimal self-contained GRIB2+PNG/JPEG2000 decoder is possible but more work.
- **Color ramp:** standard NWS reflectivity palette (dBZ → color), so it reads
  like every other radar.
- **Reprojection:** Web Mercator to match common mental models; equirectangular
  is fine for a first cut.
- **Animation:** keep the last N frames (NODD retains recent files) for a loop.

## Tech choice
Mirror this app's stack for reuse: **C++17 + CMake + SDL2 + OpenGL/ImGui**,
`libcurl` for fetch, `eccodes` for GRIB2, a tiny shapefile reader for Natural
Earth. That lets the radar render in the same window styling and, later, embed
directly.

## Licensing (all clean for open source)
- NOAA MRMS / NEXRAD: **US Government work, public domain.**
- Natural Earth: **public domain.** TIGER/Line: public domain.
- eccodes: Apache-2.0. SDL2: zlib. Dear ImGui: MIT.

## Phases
1. **National still** — fetch latest MRMS, decode, color, draw over a Natural
   Earth basemap for a fixed CONUS view. Proves the pipeline.
2. **Pan/zoom + animation loop** — viewport subsetting, last-N-frame playback.
3. **Per-site NEXRAD** — Level III for higher-resolution local views.
4. **Integration** — expose `renderRadar(bbox, zoom) -> RGBA` (or a draw API);
   Weather Desktop adds an opt-in "Radar" tab that centers on the active
   location. The app stays radar-free unless the user opens it.

## Integration seam with Weather Desktop
Keep it a library with a minimal interface, e.g.:
```cpp
// openradar: returns an RGBA frame for a geographic window, or draws into a GL texture.
struct RadarFrame { int w, h; std::vector<uint8_t> rgba; std::string validTime; };
RadarFrame renderRadar(double minLat, double minLon, double maxLat, double maxLon,
                       int widthPx, int heightPx);
```
Weather Desktop would call this only when the user opens the radar view, passing
a bbox around `activeLocation()`. No new always-on network, no third-party tiles.

## Open scoping questions
- **Coverage:** CONUS-only first (MRMS is US). International radar mosaics aren't
  uniformly public-domain; would be a later, source-by-source effort.
- **GRIB2 dependency:** take the `eccodes` dependency (fast path) vs. write a
  minimal decoder (zero deps, more work)?
- **Embed vs. launch:** in-process library/tab vs. a separate companion window.
