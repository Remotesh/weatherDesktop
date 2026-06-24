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
    bool detectLocationRequested = false;  // one-shot OS geolocation
    bool editRequested = false;            // edit the active location (name/coords)
    GeoLocation editLocation;              // the edited values to apply
    bool openRadarRequested = false;       // open the radar overlay
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
    // Feedback for the one-shot "Detect my location" button (empty = clear).
    void setDetectStatus(const std::string& s) { detectStatus_ = s; }
    void setIconAtlas(const Texture& atlas) { iconAtlas_ = atlas; }
    void setMoonAtlas(const Texture& atlas) { moonAtlas_ = atlas; }

    UIActions consumeActions();

private:
    void renderLocationBar(const WeatherData& data, int unreadCount);
    void renderSearchPopup();
    void renderEditPopup();
    void renderCurrentConditions(const WeatherData& data);  // dispatches to the two below
    void renderCurrentHeader(const WeatherData& data);      // static: icon/temp/takeaway
    void renderCurrentMetrics(const WeatherData& data);     // scrollable: metrics blob
    // The SKY panel is an auto-cycling tabbed carousel ("weather-channel" rotation)
    // of focus cards; renderSkyCarousel draws the tab strip + the active card.
    void renderSkyCarousel(const WeatherData& data);
    void renderSkyCardSky(const WeatherData& data);         // moon / sun arc / aurora / meteors
    void renderSkyCardAir(const WeatherData& data);         // AQI + pollutant breakdown
    void renderSkyCardDiscussion(const WeatherData& data);  // NWS forecast discussion
    void renderSkyCardAlmanac(const WeatherData& data);     // historical normals
    void renderSkyCardTides(const WeatherData& data);       // tides + waves
    // Sun-path arc (sunrise->sunset) with the sun at the current time and a
    // contextual "Sunset/Sunrise in ..." headline.
    void renderSunTracker(const WeatherData& data);
    void renderHourlyForecast(const std::vector<HourlyForecast>& hourly);
    // Column-aligned trend charts (temperature line + precip bars). colW/leftPad
    // must match the hourly row layout so the charts line up with the columns and
    // scroll with them.
    void renderHourlyChart(const std::vector<HourlyForecast>& hourly,
                           float colW, float leftPad);
    void renderDailyForecast(const std::vector<DailyForecast>& daily);
    // Active, unacknowledged alerts as floating toasts in the top-right corner.
    void renderToasts(const std::vector<WeatherAlert>& alerts);
    void renderStatusBar(bool isLoading, const std::string& errorMsg);
    void renderSettingsPopup();
    void renderNotificationCenter(const std::vector<Notification>& history);
    void renderFirstRunPrompt();
    void renderDailyTooltip(const DailyForecast& day);
    void renderHourlyTooltip(const HourlyForecast& hr);

    // Formatting helpers
    std::string formatTemp(double celsius) const;
    std::string formatSpeed(double kmh) const;
    std::string formatPrecip(double mm) const;
    std::string formatSnow(double cm) const;
    std::string formatPressure(double hPa) const;
    std::string formatVisibility(double meters) const;

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
    bool showEdit_ = false;
    char editNameBuf_[128] = {};
    double editLat_ = 0.0;
    double editLon_ = 0.0;
    bool showSettings_ = false;
    bool showNotifications_ = false;
    std::vector<GeoLocation> searchResults_;

    // Manual coordinate entry + OS-detect state for the search popup.
    double manualLat_ = 0.0;
    double manualLon_ = 0.0;
    char labelBuf_[64] = {};
    std::string detectStatus_;
    std::chrono::steady_clock::time_point lastUpdateTime_;

    Texture iconAtlas_;     // 6x5 weather sprite sheet
    Texture moonAtlas_;     // 5x3 moon-phase sprite sheet
    bool isNight_ = false;  // computed per-frame from the location's local time

    // SKY carousel state. The set of available cards is rebuilt each frame (some
    // depend on data being present); skyCard_ indexes into that per-frame list.
    enum class SkyCard { Sky, Air, Discussion, Almanac, Tides };
    int skyCard_ = 0;
    bool skyAutoAdvance_ = true;  // cleared when the user picks a tab
    float skyDwell_ = 0.0f;       // seconds shown on the current card
};

} // namespace wd
