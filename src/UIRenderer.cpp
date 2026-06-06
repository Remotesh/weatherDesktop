#include "weatherdesktop/UIRenderer.h"
#include "weatherdesktop/LocationManager.h"
#include "weatherdesktop/Theme.h"
#include "weatherdesktop/SkyEvents.h"
#include <imgui.h>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <chrono>
#include <algorithm>

namespace wd {

UIRenderer::UIRenderer(LocationManager& locMgr, Config& config)
    : locMgr_(locMgr), config_(config) {}

UIRenderer::~UIRenderer() {
    // GL context is still current here (App deletes the renderer before tearing
    // down ImGui/SDL), so it's safe to release the atlas textures.
    freeTexture(iconAtlas_);
    freeTexture(moonAtlas_);
}

void UIRenderer::drawMoonSprite(int phaseIndex, float height) {
    if (!moonAtlas_.valid() || phaseIndex < 0 || phaseIndex > 7) return;
    const int cols = 5, rows = 3;  // 8 phases live in the first two rows
    int c = phaseIndex % cols, r = phaseIndex / cols;
    ImVec2 uv0(static_cast<float>(c) / cols, static_cast<float>(r) / rows);
    ImVec2 uv1(static_cast<float>(c + 1) / cols, static_cast<float>(r + 1) / rows);
    const float aspect = 249.6f / 277.3f;  // cell w/h (~0.9, taller than wide)
    ImGui::Image(static_cast<ImTextureID>(moonAtlas_.id),
                 ImVec2(height * aspect, height), uv0, uv1);
}

void UIRenderer::drawWeatherIcon(int weatherCode, bool isNight, float height) {
    int cell = weatherCodeToIconCell(weatherCode, isNight);
    if (!iconAtlas_.valid() || cell < 0) return;
    const int cols = 6, rows = 5;
    int c = cell % cols, r = cell / cols;
    ImVec2 uv0(static_cast<float>(c) / cols, static_cast<float>(r) / rows);
    ImVec2 uv1(static_cast<float>(c + 1) / cols, static_cast<float>(r + 1) / rows);
    const float aspect = 1.25f;  // cell is (1536/6) / (1024/5) = 1.25
    ImGui::Image(static_cast<ImTextureID>(iconAtlas_.id),
                 ImVec2(height * aspect, height), uv0, uv1);
}

std::string UIRenderer::formatTemp(double celsius) const {
    char buf[32];
    if (config_.useFahrenheit()) {
        std::snprintf(buf, sizeof(buf), "%.0fF", celsiusToFahrenheit(celsius));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0fC", celsius);
    }
    return buf;
}

std::string UIRenderer::formatSpeed(double kmh) const {
    char buf[32];
    if (config_.useMph()) {
        std::snprintf(buf, sizeof(buf), "%.0f mph", kmhToMph(kmh));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f km/h", kmh);
    }
    return buf;
}

std::string UIRenderer::formatPrecip(double mm) const {
    char buf[32];
    if (config_.useFahrenheit()) { // US uses inches
        std::snprintf(buf, sizeof(buf), "%.2f in", mmToInches(mm));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f mm", mm);
    }
    return buf;
}

void UIRenderer::render(const WeatherData& data,
                        const std::vector<WeatherAlert>& bannerAlerts,
                        const std::vector<Notification>& history,
                        bool isLoading,
                        const std::string& errorMsg) {
    if (!data.fetchedAt.time_since_epoch().count()) {
        lastUpdateTime_ = {};
    } else {
        lastUpdateTime_ = data.fetchedAt;
    }

    // Day/night at the location (drives which icon variant to show).
    {
        auto now = std::chrono::system_clock::now();
        long long epoch = std::chrono::duration_cast<std::chrono::seconds>(
                              now.time_since_epoch()).count();
        long long localEpoch = epoch + data.utcOffsetSeconds;
        int localHour = static_cast<int>((localEpoch % 86400 + 86400) % 86400 / 3600);
        isNight_ = (localHour < 6 || localHour >= 19);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("WeatherDesktop", nullptr, flags);

    int unread = 0;
    for (const auto& n : history) if (!n.acknowledged) ++unread;

    if (!locMgr_.hasLocations()) {
        renderFirstRunPrompt();
    } else {
        renderLocationBar(data, unread);

        // Body: a two-column top band over a full-width 7-day band.
        //   LEFT  : current conditions ("now")
        //   RIGHT : 24-hour forecast, then the Tonight panel filling the rest
        //   BELOW : the 7-day forecast (separated by a rule)
        float bodyTotal = ImGui::GetContentRegionAvail().y -
                          ImGui::GetFrameHeightWithSpacing() - 6.0f;
        if (bodyTotal < 200.0f) bodyTotal = 200.0f;
        float dailyH = bodyTotal * 0.40f;
        if (dailyH < 190.0f) dailyH = 190.0f;
        if (dailyH > 260.0f) dailyH = 260.0f;
        float topH = bodyTotal - dailyH - ImGui::GetStyle().ItemSpacing.y * 2 -
                     2.0f;  // account for the separator rule
        if (topH < 160.0f) topH = 160.0f;

        float leftW = ImGui::GetContentRegionAvail().x * 0.34f;
        if (leftW < 230.0f) leftW = 230.0f;

        ImGui::BeginChild("current", ImVec2(leftW, topH));
        renderCurrentConditions(data.current);
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("rightcol", ImVec2(0, topH));
        renderHourlyForecast(data.hourly);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        renderTonight(data);
        ImGui::EndChild();

        // Rule delineating the 7-day band from the scrollable top area.
        ImGui::Separator();

        ImGui::BeginChild("dailywrap", ImVec2(0, dailyH));
        renderDailyForecast(data.daily);
        ImGui::EndChild();
    }

    renderStatusBar(isLoading, errorMsg);

    if (showSearchPopup_) renderSearchPopup();
    if (showSettings_) renderSettingsPopup();
    if (showNotifications_) renderNotificationCenter(history);

    ImGui::End();

    // Floating notification toasts (active, unacknowledged alerts), drawn as a
    // separate always-on-top overlay anchored to the top-right.
    renderToasts(bannerAlerts);
}

void UIRenderer::renderFirstRunPrompt() {
    ImGui::Spacing();
    ImGui::Spacing();

    float windowWidth = ImGui::GetContentRegionAvail().x;
    float textWidth = ImGui::CalcTextSize("Welcome to Weather Desktop").x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("Welcome to Weather Desktop");

    ImGui::Spacing();
    textWidth = ImGui::CalcTextSize("Add a location to get started:").x;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::Text("Add a location to get started:");

    ImGui::Spacing();
    ImGui::Spacing();

    float inputWidth = 300.0f;
    ImGui::SetCursorPosX((windowWidth - inputWidth - 70) * 0.5f);
    ImGui::SetNextItemWidth(inputWidth);
    bool submitted = ImGui::InputText("##firstSearch", searchBuf_, sizeof(searchBuf_),
                                       ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Search##firstrun") || submitted) && searchBuf_[0] != '\0') {
        pendingActions_.searchQuery = searchBuf_;
        showSearchPopup_ = true;
    }
}

void UIRenderer::renderLocationBar(const WeatherData& data, int unreadCount) {
    bool multiLoc = locMgr_.locations().size() > 1;

    if (multiLoc) {
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
            locMgr_.cyclePrev();
            pendingActions_.locationChanged = true;
            pendingActions_.newLocationIndex = locMgr_.activeIndex();
        }
        ImGui::SameLine();
    }

    ImGui::Text("%s", locMgr_.activeLocation().geo.displayName().c_str());

    if (multiLoc) {
        ImGui::SameLine();
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
            locMgr_.cycleNext();
            pendingActions_.locationChanged = true;
            pendingActions_.newLocationIndex = locMgr_.activeIndex();
        }
    }

    // Timezone + local time line
    if (data.utcOffsetSeconds != 0 || data.fetchedAt.time_since_epoch().count() > 0) {
        int offsetSec = data.utcOffsetSeconds;
        int offsetHours = offsetSec / 3600;
        int offsetMinRemainder = std::abs(offsetSec % 3600) / 60;

        char tzBuf[32];
        if (offsetMinRemainder != 0) {
            std::snprintf(tzBuf, sizeof(tzBuf), "UTC%+d:%02d", offsetHours, offsetMinRemainder);
        } else {
            std::snprintf(tzBuf, sizeof(tzBuf), "UTC%+d", offsetHours);
        }

        // Compute location's local time from system UTC + offset
        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        long long localEpoch = epoch + offsetSec;
        int localHour = static_cast<int>((localEpoch % 86400 + 86400) % 86400 / 3600);
        int localMin = static_cast<int>((localEpoch % 3600 + 3600) % 3600 / 60);
        int h12 = localHour % 12;
        if (h12 == 0) h12 = 12;
        const char* ampm = localHour < 12 ? "AM" : "PM";

        ImGui::TextDisabled("%s  %d:%02d %s", tzBuf, h12, localMin, ampm);
    }

    // Right-aligned controls: + / Refresh / X plus a bell that opens the
    // notification center (amber when there are unread alerts).
    char bell[24];
    std::snprintf(bell, sizeof bell, unreadCount > 0 ? "Alerts (%d)" : "Alerts",
                  unreadCount);
    float pad = ImGui::GetStyle().FramePadding.x * 2;
    float sp = ImGui::GetStyle().ItemSpacing.x;
    float groupW = ImGui::CalcTextSize("+").x + pad + sp +
                   ImGui::CalcTextSize("Refresh").x + pad + sp +
                   ImGui::CalcTextSize("X").x + pad + sp +
                   ImGui::CalcTextSize(bell).x + pad;
    ImGui::SameLine(ImGui::GetWindowWidth() - groupW -
                    ImGui::GetStyle().WindowPadding.x);

    if (ImGui::SmallButton("+")) {
        showSearchPopup_ = true;
        searchBuf_[0] = '\0';
        searchResults_.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add location");
    ImGui::SameLine();

    if (ImGui::SmallButton("Refresh")) {
        pendingActions_.refreshRequested = true;
    }
    ImGui::SameLine();

    if (ImGui::SmallButton("X")) {
        pendingActions_.removeLocationRequested = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this location");
    ImGui::SameLine();

    if (unreadCount > 0) ImGui::PushStyleColor(ImGuiCol_Text, theme::Amber);
    if (ImGui::SmallButton(bell)) showNotifications_ = !showNotifications_;
    if (unreadCount > 0) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Notifications");

    ImGui::Separator();
}

void UIRenderer::renderSearchPopup() {
    ImGui::OpenPopup("Search Location");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Search Location", &showSearchPopup_)) {
        ImGui::Text("Enter city name or zip code:");
        ImGui::SetNextItemWidth(-1);
        bool submitted = ImGui::InputText("##search", searchBuf_, sizeof(searchBuf_),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Search") || submitted) && searchBuf_[0] != '\0') {
            pendingActions_.searchQuery = searchBuf_;
        }

        ImGui::Separator();

        if (searchResults_.empty()) {
            ImGui::TextDisabled("No results. Try a different search.");
        } else {
            ImGui::Text("Select a location:");
            for (size_t i = 0; i < searchResults_.size(); ++i) {
                auto& loc = searchResults_[i];
                char label[256];
                std::snprintf(label, sizeof(label), "%s, %s, %s##%zu",
                             loc.name.c_str(), loc.admin1.c_str(), loc.country.c_str(), i);
                if (ImGui::Selectable(label)) {
                    // Carry the chosen location by value so a later search that
                    // overwrites searchResults_ can't change what gets added.
                    pendingActions_.selectedLocation = loc;
                    pendingActions_.addLocationRequested = true;
                    showSearchPopup_ = false;
                    searchBuf_[0] = '\0';
                }
            }
        }

        ImGui::EndPopup();
    }
}

void UIRenderer::renderCurrentConditions(const CurrentWeather& current) {
    ImGui::Spacing();

    float w = ImGui::GetContentRegionAvail().x;

    // Large condition sprite, centered at the top (caption is baked in).
    if (iconAtlas_.valid() &&
        weatherCodeToIconCell(current.weatherCode, isNight_) >= 0) {
        float h = w * 0.46f;
        if (h < 90.0f) h = 90.0f;
        if (h > 150.0f) h = 150.0f;
        float iw = h * 1.25f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - iw) * 0.5f);
        drawWeatherIcon(current.weatherCode, isNight_, h);
    } else {
        ImGui::Text("%s", weatherCodeToString(current.weatherCode));
    }

    ImGui::Spacing();

    // Big temperature + feels-like.
    ImGui::Text("%s", formatTemp(current.temperature).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(feels like %s)", formatTemp(current.apparentTemp).c_str());

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Details, one per line. The font is monospace so the colon column aligns.
    ImGui::Text("Humidity   %.0f%%", current.humidity);
    ImGui::Text("Wind       %s %s", formatSpeed(current.windSpeed).c_str(),
                windDirectionToString(current.windDirection));
    ImGui::Text("Gusts      %s", formatSpeed(current.windGusts).c_str());
    ImGui::Text("Precip     %s", formatPrecip(current.precipitation).c_str());
    ImGui::Text("Cloud      %.0f%%", current.cloudCover);
    if (current.snowfall > 0) {
        ImGui::Text("Snow       %.1f cm", current.snowfall);
    }
}

// Draw the moon at its current phase by scanline-filling the lit region.
static void drawMoon(ImVec2 c, float R, const MoonInfo& m) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 lit = ImGui::GetColorU32(theme::Accent);          // warm tan
    ImU32 shadow = ImGui::GetColorU32(theme::hex(0x2A1E18)); // unlit disk
    ImU32 rim = ImGui::GetColorU32(theme::Muted);
    dl->AddCircleFilled(c, R, shadow, 48);
    double cosP = std::cos(2.0 * 3.14159265358979 * m.phase);
    const int N = 64;
    for (int i = 0; i <= N; ++i) {
        float y = -R + (2.0f * R) * i / N;
        float hw = std::sqrt(std::max(0.0f, R * R - y * y));
        float tx = static_cast<float>(cosP) * hw;  // terminator x at this row
        float x0, x1;
        if (m.waxing) { x0 = tx; x1 = hw; }     // lit on the right
        else          { x0 = -hw; x1 = -tx; }   // lit on the left
        if (x1 > x0 + 0.5f) {
            dl->AddLine(ImVec2(c.x + x0, c.y + y), ImVec2(c.x + x1, c.y + y), lit, 2.0f);
        }
    }
    dl->AddCircle(c, R, rim, 48, 1.5f);
}

void UIRenderer::renderTonight(const WeatherData& data) {
    ImGui::TextDisabled("TONIGHT");
    ImGui::Spacing();

    std::time_t now = std::time(nullptr);
    MoonInfo moon = computeMoon(now);

    // Moon sprite sized to fill the panel height, with the sky readout beside it.
    float availH = ImGui::GetContentRegionAvail().y;
    float moonH = availH * 0.66f;
    if (moonH < 76.0f) moonH = 76.0f;
    if (moonH > 150.0f) moonH = 150.0f;

    if (moonAtlas_.valid()) {
        drawMoonSprite(moon.index, moonH);
    } else {
        float R = moonH * 0.5f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        drawMoon(ImVec2(p.x + R, p.y + R), R, moon);
        ImGui::Dummy(ImVec2(R * 2.0f, R * 2.0f));
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(moon.name);
    ImGui::TextDisabled("%.0f%% illuminated", moon.illumination * 100.0);
    ImGui::Spacing();

    // Aurora chance from the Kp index + latitude.
    AuroraChance ac = auroraChance(data.kpIndex, data.location.latitude);
    ImGui::TextUnformatted("Aurora:");
    ImGui::SameLine();
    if (ac == AuroraChance::Unknown) {
        ImGui::TextDisabled("--");
    } else {
        ImVec4 col = (ac == AuroraChance::Likely)     ? theme::Live
                     : (ac == AuroraChance::Possible) ? theme::Amber
                                                      : theme::Dim;
        ImGui::TextColored(col, "%s", auroraChanceLabel(ac));
        ImGui::SameLine();
        ImGui::TextDisabled("(Kp %.0f)", data.kpIndex);
    }

    // Active meteor shower (closest to peak).
    auto showers = activeMeteorShowers(now);
    if (showers.empty()) {
        ImGui::TextUnformatted("Meteors:");
        ImGui::SameLine();
        ImGui::TextDisabled("none active");
    } else {
        const ActiveShower& s = showers.front();
        if (s.nearPeak) {
            ImGui::TextColored(theme::Amber, "Meteors: %s, near peak", s.name);
        } else if (s.daysToPeak > 0) {
            ImGui::Text("Meteors: %s, peak in %dd", s.name, s.daysToPeak);
        } else {
            ImGui::Text("Meteors: %s active", s.name);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s - up to ~%d meteors/hr at peak", s.name, s.zhr);
        }
    }
    ImGui::EndGroup();
}

void UIRenderer::renderHourlyForecast(const std::vector<HourlyForecast>& hourly) {
    if (hourly.empty()) return;

    ImGui::Text("24-Hour Forecast");
    ImGui::Spacing();

    // Scrollable horizontal region
    ImGui::BeginChild("hourlyScroll", ImVec2(0, 90), false, ImGuiWindowFlags_HorizontalScrollbar);

    int cols = static_cast<int>(hourly.size());
    if (ImGui::BeginTable("hourly", cols,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX)) {
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 48.0f);
        }

        // Time row
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            // Parse hour from "2026-02-25T14:00" -> "2PM"
            if (hourly[i].time.size() >= 13) {
                int hour = 0;
                std::sscanf(hourly[i].time.c_str() + 11, "%d", &hour);
                if (i == 0) {
                    ImGui::TextUnformatted("Now");
                } else {
                    int h12 = hour % 12;
                    if (h12 == 0) h12 = 12;
                    ImGui::Text("%d%s", h12, hour < 12 ? "AM" : "PM");
                }
            }
        }

        // Temperature row
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::Text("%s", formatTemp(hourly[i].temperature).c_str());
        }

        // Condition row
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::TextUnformatted(weatherCodeToShortString(hourly[i].weatherCode));
        }

        // Precip probability row
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::Text("%.0f%%", hourly[i].precipProb);
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void UIRenderer::renderDailyForecast(const std::vector<DailyForecast>& daily) {
    if (daily.empty()) return;

    ImGui::Text("7-Day Forecast");
    ImGui::Spacing();

    int cols = std::min<int>(7, static_cast<int>(daily.size()));
    if (ImGui::BeginTable("forecast", cols,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchSame)) {
        // Day names
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            // Parse date string to get day name
            if (daily[i].date.size() >= 10) {
                struct tm tm = {};
                sscanf(daily[i].date.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday);
                tm.tm_year -= 1900;
                tm.tm_mon -= 1;
                mktime(&tm);
                static const char* dayNames[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
                if (i == 0) {
                    ImGui::TextUnformatted("Today");
                } else {
                    ImGui::TextUnformatted(dayNames[tm.tm_wday]);
                }
                if (ImGui::IsItemHovered()) renderDailyTooltip(daily[i]);
            }
        }

        // High/Low
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::Text("%s/%s", formatTemp(daily[i].tempMax).c_str(),
                       formatTemp(daily[i].tempMin).c_str());
            if (ImGui::IsItemHovered()) renderDailyTooltip(daily[i]);
        }

        // Condition (weather sprite, day variant; falls back to short text).
        // Each icon is sized to its column and centered.
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            if (iconAtlas_.valid() &&
                weatherCodeToIconCell(daily[i].weatherCode, false) >= 0) {
                float cellW = ImGui::GetContentRegionAvail().x;
                float h = cellW * 0.74f;  // width = 1.25*h, leaves a margin
                if (h > 118.0f) h = 118.0f;
                float w = h * 1.25f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (cellW - w) * 0.5f);
                drawWeatherIcon(daily[i].weatherCode, false, h);
            } else {
                ImGui::TextUnformatted(weatherCodeToShortString(daily[i].weatherCode));
            }
            if (ImGui::IsItemHovered()) renderDailyTooltip(daily[i]);
        }

        // Precip probability
        ImGui::TableNextRow();
        for (int i = 0; i < cols; ++i) {
            ImGui::TableSetColumnIndex(i);
            ImGui::Text("%.0f%%", daily[i].precipProbMax);
            if (ImGui::IsItemHovered()) renderDailyTooltip(daily[i]);
        }

        ImGui::EndTable();
    }
}

void UIRenderer::renderDailyTooltip(const DailyForecast& day) {
    ImGui::BeginTooltip();
    ImGui::Text("Date: %s", day.date.c_str());
    ImGui::Text("Condition: %s", weatherCodeToString(day.weatherCode));
    ImGui::Separator();
    ImGui::Text("High: %s   Low: %s", formatTemp(day.tempMax).c_str(),
                formatTemp(day.tempMin).c_str());
    ImGui::Separator();
    ImGui::Text("Precip Chance: %.0f%%", day.precipProbMax);
    if (day.precipitationSum > 0)
        ImGui::Text("Total Precip: %s", formatPrecip(day.precipitationSum).c_str());
    if (day.rainSum > 0)
        ImGui::Text("Rain: %s", formatPrecip(day.rainSum).c_str());
    if (day.snowfallSum > 0)
        ImGui::Text("Snowfall: %.1f cm", day.snowfallSum);
    ImGui::EndTooltip();
}

// A small filled warning triangle with an exclamation, drawn inline at the
// cursor. Advances the cursor by `size` so callers can SameLine() after it.
static void drawWarningIcon(float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 amber = ImGui::GetColorU32(theme::Amber);
    ImU32 dark = ImGui::GetColorU32(theme::hex(0x1C120F));
    float s = size;
    dl->AddTriangleFilled(ImVec2(p.x + s * 0.5f, p.y + s * 0.05f),
                          ImVec2(p.x + s * 0.04f, p.y + s * 0.92f),
                          ImVec2(p.x + s * 0.96f, p.y + s * 0.92f), amber);
    float cx = p.x + s * 0.5f;
    dl->AddRectFilled(ImVec2(cx - s * 0.06f, p.y + s * 0.32f),
                      ImVec2(cx + s * 0.06f, p.y + s * 0.64f), dark);
    dl->AddCircleFilled(ImVec2(cx, p.y + s * 0.77f), s * 0.08f, dark);
    ImGui::Dummy(ImVec2(s, s));
}

void UIRenderer::renderToasts(const std::vector<WeatherAlert>& alerts) {
    if (alerts.empty()) return;

    // Anchor a floating overlay to the top-right, just under the title/location
    // bar. Cap the height so a flurry of alerts scrolls instead of filling the
    // screen.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float w = 330.0f;
    float maxH = vp->Size.y * 0.6f;
    float estH = alerts.size() * 64.0f + 16.0f;
    float h = estH < maxH ? estH : maxH;
    ImGui::SetNextWindowPos(
        ImVec2(vp->Pos.x + vp->Size.x - w - 16.0f, vp->Pos.y + 46.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNavInputs;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme::hex(0x5A241D, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Error);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::Begin("##toasts", nullptr, flags);

    const float icon = ImGui::GetTextLineHeight();
    const float xBtn = ImGui::GetFrameHeight();
    for (const auto& alert : alerts) {
        ImGui::PushID(alert.deduplicationKey().c_str());
        drawWarningIcon(icon);
        ImGui::SameLine();
        float wrap = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                     xBtn - ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushTextWrapPos(wrap);
        ImGui::TextWrapped("%s: %s", alert.title.c_str(), alert.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::SameLine(ImGui::GetContentRegionMax().x - xBtn);
        if (ImGui::SmallButton("X")) {
            pendingActions_.acknowledgeKey = alert.deduplicationKey();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Acknowledge");
        ImGui::Separator();
        ImGui::PopID();
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void UIRenderer::renderStatusBar(bool isLoading, const std::string& errorMsg) {
    ImGui::Separator();

    if (isLoading) {
        ImGui::Text("Updating...");
    } else if (!errorMsg.empty()) {
        ImGui::TextColored(theme::Error, "%s", errorMsg.c_str());
    } else if (lastUpdateTime_.time_since_epoch().count() > 0) {
        auto now = std::chrono::steady_clock::now();
        auto ago = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdateTime_).count();
        if (ago < 1) {
            ImGui::Text("Updated just now");
        } else {
            ImGui::Text("Updated %lld min ago", static_cast<long long>(ago));
        }
    } else {
        ImGui::Text("No data");
    }

    // Right-aligned: Settings (the Alerts bell now lives in the top bar).
    float settingsW = ImGui::CalcTextSize("Settings").x +
                      ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine(ImGui::GetWindowWidth() - settingsW -
                    ImGui::GetStyle().WindowPadding.x);
    if (ImGui::SmallButton("Settings")) {
        showSettings_ = !showSettings_;
    }
}

void UIRenderer::renderSettingsPopup() {
    ImGui::OpenPopup("Settings");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 320), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Settings", &showSettings_)) {
        bool fahr = config_.useFahrenheit();
        if (ImGui::Checkbox("Use Fahrenheit", &fahr)) {
            config_.setUseFahrenheit(fahr);
            config_.setUseMph(fahr); // link mph with F
            pendingActions_.settingsChanged = true;
        }

        bool alerts = config_.alertsEnabled();
        if (ImGui::Checkbox("Enable Alerts", &alerts)) {
            config_.setAlertsEnabled(alerts);
            pendingActions_.settingsChanged = true;
        }

        // Notification scheduler (only meaningful when alerts are on).
        if (config_.alertsEnabled()) {
            // HH:MM editor backed by minutes-since-midnight. Returns true on edit.
            auto timeEdit = [&](const char* label, int minutes) -> int {
                int h = minutes / 60, m = minutes % 60;
                ImGui::PushItemWidth(54);
                ImGui::PushID(label);
                ImGui::TextUnformatted(label);
                ImGui::SameLine(110);
                ImGui::InputInt("h", &h, 0);
                ImGui::SameLine();
                ImGui::InputInt("m", &m, 0);
                ImGui::PopID();
                ImGui::PopItemWidth();
                if (h < 0) h = 0; if (h > 23) h = 23;
                if (m < 0) m = 0; if (m > 59) m = 59;
                return h * 60 + m;
            };

            ImGui::Spacing();
            ImGui::TextDisabled("When to notify");
            int mode = static_cast<int>(config_.notifyMode());
            const char* modes[] = {"As they happen", "Quiet hours", "Daily digest"};
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##notifmode", &mode, modes, 3)) {
                config_.setNotifyMode(static_cast<NotifyMode>(mode));
                pendingActions_.settingsChanged = true;
            }
            if (config_.notifyMode() == NotifyMode::QuietHours) {
                int s = timeEdit("Quiet from", config_.quietStartMinute());
                if (s != config_.quietStartMinute()) {
                    config_.setQuietStartMinute(s);
                    pendingActions_.settingsChanged = true;
                }
                int e = timeEdit("Quiet until", config_.quietEndMinute());
                if (e != config_.quietEndMinute()) {
                    config_.setQuietEndMinute(e);
                    pendingActions_.settingsChanged = true;
                }
            } else if (config_.notifyMode() == NotifyMode::Digest) {
                int d = timeEdit("Digest at", config_.digestMinute());
                if (d != config_.digestMinute()) {
                    config_.setDigestMinute(d);
                    pendingActions_.settingsChanged = true;
                }
            }
        }

        ImGui::Spacing();
        bool startMin = config_.startMinimized();
        if (ImGui::Checkbox("Start Minimized", &startMin)) {
            config_.setStartMinimized(startMin);
            pendingActions_.settingsChanged = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("Save & Close")) {
            config_.save();
            showSettings_ = false;
        }

        ImGui::EndPopup();
    }
}

void UIRenderer::renderNotificationCenter(const std::vector<Notification>& history) {
    ImGui::OpenPopup("Notifications");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380, 360), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Notifications", &showNotifications_)) {
        if (history.empty()) {
            ImGui::TextDisabled("No notifications yet.");
        } else {
            if (ImGui::SmallButton("Clear all")) {
                pendingActions_.clearNotifications = true;
            }
            ImGui::Separator();
            ImGui::BeginChild("notiflist", ImVec2(0, 280));
            // Newest first.
            for (auto it = history.rbegin(); it != history.rend(); ++it) {
                const Notification& n = *it;
                std::time_t tt = std::chrono::system_clock::to_time_t(n.time);
                std::tm lt{};
#ifdef _WIN32
                localtime_s(&lt, &tt);
#else
                localtime_r(&tt, &lt);
#endif
                char ts[24];
                std::strftime(ts, sizeof ts, "%m/%d %H:%M", &lt);
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      n.acknowledged ? theme::Dim : theme::Accent);
                ImGui::TextUnformatted(ts);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      n.acknowledged ? theme::Dim : theme::Fg);
                ImGui::TextWrapped("%s", n.alert.message.c_str());
                ImGui::PopStyleColor();
                ImGui::Separator();
            }
            ImGui::EndChild();
        }
        ImGui::EndPopup();
    }
}

void UIRenderer::setSearchResults(const std::vector<GeoLocation>& results) {
    searchResults_ = results;
}

UIActions UIRenderer::consumeActions() {
    UIActions actions = pendingActions_;
    pendingActions_ = UIActions{};
    return actions;
}

} // namespace wd
