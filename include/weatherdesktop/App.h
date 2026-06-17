#pragma once

#include "Config.h"
#include "WeatherService.h"
#include "LocationManager.h"
#include "AlertEngine.h"
#include "NotificationManager.h"
#include "SystemTray.h"
#include "UIRenderer.h"
#include "ThreadSafeQueue.h"
#include "WeatherTypes.h"

#include <SDL.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <vector>
#include <string>
#include <set>

namespace wd {

class App {
public:
    App();
    ~App();

    bool initialize();
    void run();
    void shutdown();

    // Accessed by WndProc callback
    SystemTray& systemTray() { return systemTray_; }
    void minimizeToTray();
    void restoreFromTray();

    static App* instance() { return s_instance; }

private:
    bool initSDL();
    bool initImGui();
    bool initSystemTray();
    bool initNotifications();

    void processEvents();
    void updateFromBackground();
    void renderFrame();

    // Background worker
    void backgroundWorkerLoop();
    void requestWeatherUpdate(const GeoLocation& loc, bool isRefresh = false);
    void requestGeocode(const std::string& query);
    void requestDetectLocation();

    // Decide whether to pop tray notifications now, per the scheduler mode.
    // Runs on the UI thread (caller holds no lock).
    void maybeNotify();

    // State
    SDL_Window* window_ = nullptr;
    SDL_GLContext glContext_ = nullptr;
    bool running_ = true;
    bool minimizedToTray_ = false;
    bool shutdownDone_ = false;  // shutdown() is called from run() and ~App()

    // Modules
    Config config_;
    WeatherService weatherService_;
    LocationManager locationMgr_;
    AlertEngine alertEngine_;
    NotificationManager notificationMgr_;
    SystemTray systemTray_;
    UIRenderer* uiRenderer_ = nullptr;

    // Threading
    std::thread backgroundThread_;
    std::atomic<bool> backgroundRunning_{true};
    std::mutex dataMutex_;

    // Shared state (guarded by dataMutex_)
    WeatherData currentWeatherData_;
    std::vector<WeatherAlert> activeAlerts_;
    bool isLoading_ = false;
    std::string lastError_;  // last fetch failure, shown in the status bar

    // Notification log + per-alert pop-up bookkeeping. notifications_ is the
    // history shown in the center; acknowledged_ holds keys the user dismissed
    // from the banner; unpoppedKeys_ are alerts awaiting a pop-up (deferred by
    // quiet hours / digest). All guarded by dataMutex_.
    std::vector<Notification> notifications_;
    std::set<std::string> acknowledged_;
    std::set<std::string> unpoppedKeys_;
    int lastDigestDay_ = -1;  // yday of the last digest pop (avoid repeats)

    // Work queue types
    struct WorkRequest {
        enum Type { FetchWeather, Geocode, DetectLocation };
        Type type;
        GeoLocation location;
        std::string query;
        bool isRefresh = false;
        // Config snapshot taken at enqueue time (on the UI thread) so the worker
        // never reads config_ concurrently with the UI mutating it.
        bool alertsEnabled = true;
        bool useFahrenheit = true;
    };

    struct WorkResult {
        WorkRequest::Type type;
        std::optional<WeatherData> weatherData;
        std::vector<WeatherAlert> alerts;
        std::vector<GeoLocation> geocodeResults;
        std::optional<GeoLocation> detectedLocation;  // from DetectLocation
        std::string error;
    };

    ThreadSafeQueue<WorkRequest> workQueue_;
    ThreadSafeQueue<WorkResult> resultQueue_;

    // Polling
    std::chrono::steady_clock::time_point lastPollTime_;

    // Helpers
    std::string formatTemp(double celsius) const;
    static App* s_instance;

#ifdef _WIN32
    static HICON createAppIcon();
    HICON appIcon_ = nullptr;

    // WndProc
    static LRESULT CALLBACK CustomWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static WNDPROC s_originalWndProc;
    HWND hwnd_ = nullptr;
#endif
};

} // namespace wd
