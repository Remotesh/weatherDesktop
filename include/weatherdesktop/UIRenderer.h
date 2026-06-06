#pragma once

#include "WeatherTypes.h"
#include "Config.h"
#include "Texture.h"
#include <string>
#include <vector>
#include <chrono>

namespace wd {

class LocationManager;

struct UIActions {
    bool refreshRequested = false;
    bool locationChanged = false;
    int newLocationIndex = -1;
    std::string searchQuery;
    GeoLocation selectedLocation;  // the city the user picked from results
    bool addLocationRequested = false;
    bool removeLocationRequested = false;
    bool quitRequested = false;
    bool settingsChanged = false;
    std::string acknowledgeKey;       // dedup key the user dismissed from banner
    bool clearNotifications = false;  // clear the notification center history
};

class UIRenderer {
public:
    UIRenderer(LocationManager& locMgr, Config& config);
    ~UIRenderer();

    void render(const WeatherData& data,
                const std::vector<WeatherAlert>& bannerAlerts,
                const std::vector<Notification>& history,
                bool isLoading,
                const std::string& errorMsg);

    void setSearchResults(const std::vector<GeoLocation>& results);
    void setIconAtlas(const Texture& atlas) { iconAtlas_ = atlas; }
    void setMoonAtlas(const Texture& atlas) { moonAtlas_ = atlas; }

    UIActions consumeActions();

private:
    void renderLocationBar(const WeatherData& data, int unreadCount);
    void renderSearchPopup();
    void renderCurrentConditions(const CurrentWeather& current);
    void renderTonight(const WeatherData& data);  // moon / meteors / aurora
    void renderHourlyForecast(const std::vector<HourlyForecast>& hourly);
    void renderDailyForecast(const std::vector<DailyForecast>& daily);
    // Active, unacknowledged alerts as floating toasts in the top-right corner.
    void renderToasts(const std::vector<WeatherAlert>& alerts);
    void renderStatusBar(bool isLoading, const std::string& errorMsg);
    void renderSettingsPopup();
    void renderNotificationCenter(const std::vector<Notification>& history);
    void renderFirstRunPrompt();
    void renderDailyTooltip(const DailyForecast& day);

    // Formatting helpers
    std::string formatTemp(double celsius) const;
    std::string formatSpeed(double kmh) const;
    std::string formatPrecip(double mm) const;

    // Draw a weather-condition sprite from the atlas, `height` px tall (the cell
    // is 1.25x wider than tall). No-op when the atlas failed to load.
    void drawWeatherIcon(int weatherCode, bool isNight, float height);
    // Draw a moon-phase sprite (5x3 atlas; phaseIndex 0..7 = New..Waning Crescent).
    void drawMoonSprite(int phaseIndex, float height);

    LocationManager& locMgr_;
    Config& config_;
    UIActions pendingActions_;

    char searchBuf_[256] = {};
    bool showSearchPopup_ = false;
    bool showSettings_ = false;
    bool showNotifications_ = false;
    std::vector<GeoLocation> searchResults_;
    std::chrono::steady_clock::time_point lastUpdateTime_;

    Texture iconAtlas_;     // 6x5 weather sprite sheet
    Texture moonAtlas_;     // 5x3 moon-phase sprite sheet
    bool isNight_ = false;  // computed per-frame from the location's local time
};

} // namespace wd
