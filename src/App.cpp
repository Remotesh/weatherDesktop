#include "weatherdesktop/App.h"
#include "weatherdesktop/Theme.h"
#include "weatherdesktop/Texture.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <SDL.h>
#include <SDL_syswm.h>
#include <GL/gl.h>

#ifdef _WIN32
#include <dwmapi.h>
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#endif

#include <curl/curl.h>
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace wd {

// True if `nowMin` (minutes since local midnight) is inside the quiet window,
// which may wrap past midnight (e.g. 22:00 -> 07:00).
static bool inQuietWindow(int nowMin, int startMin, int endMin) {
    if (startMin == endMin) return false;
    if (startMin < endMin) return nowMin >= startMin && nowMin < endMin;
    return nowMin >= startMin || nowMin < endMin;
}

#ifdef _WIN32
WNDPROC App::s_originalWndProc = nullptr;
#endif
App* App::s_instance = nullptr;

#ifdef _WIN32
// UTF-8 (std::string) -> UTF-16 (std::wstring) for the Win32 wide APIs. Returns
// empty on conversion failure rather than underflowing the length.
static std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring w(sz - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], sz);
    return w;
}
#endif

App::App()
    : locationMgr_(config_, weatherService_) {
    s_instance = this;
}

App::~App() {
    shutdown();
    s_instance = nullptr;
}

#ifdef _WIN32
LRESULT CALLBACK App::CustomWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = App::instance();
    if (!app) return CallWindowProc(s_originalWndProc, hwnd, msg, wp, lp);

    switch (msg) {
    case WM_CLOSE:
        app->minimizeToTray();
        return 0;

    case SystemTray::WM_TRAYICON:
        app->systemTray().handleMessage(msg, wp, lp);
        return 0;

    case WM_COMMAND:
        if (app->systemTray().handleMessage(msg, wp, lp))
            return 0;
        break;
    }

    return CallWindowProc(s_originalWndProc, hwnd, msg, wp, lp);
}

HICON App::createAppIcon() {
    const int size = 32;
    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    UINT* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS,
                                       reinterpret_cast<void**>(&pBits), nullptr, 0);
    if (!hBitmap) {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        return nullptr;
    }

    HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(memDC, hBitmap));

    // Fill with the Korra chocolate background (ABGR: 0xFF + B,G,R of #1C120F)
    for (int i = 0; i < size * size; i++) {
        pBits[i] = 0xFF0F121C;
    }

    // Draw "W" using GDI text
    HFONT hFont = CreateFontW(
        size - 6, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT oldFont = static_cast<HFONT>(SelectObject(memDC, hFont));

    SetTextColor(memDC, RGB(0xE0, 0x79, 0x42)); // Korra amber accent
    SetBkMode(memDC, TRANSPARENT);

    RECT rect = {0, 0, size, size};
    DrawTextW(memDC, L"W", 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Set alpha to 0xFF for all pixels that were drawn on
    for (int i = 0; i < size * size; i++) {
        pBits[i] |= 0xFF000000;
    }

    SelectObject(memDC, oldFont);
    DeleteObject(hFont);
    SelectObject(memDC, oldBitmap);

    HBITMAP hMask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmMask = hMask;
    iconInfo.hbmColor = hBitmap;

    HICON hIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hBitmap);
    DeleteObject(hMask);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);

    return hIcon;
}
#endif // _WIN32

bool App::initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return false;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    window_ = SDL_CreateWindow(
        "Weather Desktop",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        900, 640,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window_) return false;

    glContext_ = SDL_GL_CreateContext(window_);
    if (!glContext_) return false;

    SDL_GL_MakeCurrent(window_, glContext_);
    SDL_GL_SetSwapInterval(1); // vsync

#ifdef _WIN32
    // Get native HWND
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window_, &wmInfo)) {
        hwnd_ = wmInfo.info.win.window;
    }

    if (hwnd_) {
        // Dark title bar
        BOOL isDarkMode = TRUE;
        DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE,
                              &isDarkMode, sizeof(isDarkMode));

        // Tint the title bar to the Korra palette. COLORREF is 0x00BBGGRR;
        // RGB() packs in that order. Caption = window bg (#1C120F), text =
        // cream (#E6D6C4), border = warm brown (#5C3F29). Exact colors apply on
        // Windows 11 22000+; older builds keep the dark immersive bar above.
        COLORREF caption = RGB(0x1B, 0x16, 0x13);
        COLORREF text = RGB(0xE6, 0xD6, 0xC4);
        COLORREF border = RGB(0x5C, 0x3F, 0x29);
        DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &text, sizeof(text));
        DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));

        // Custom "W" icon
        appIcon_ = createAppIcon();
        if (appIcon_) {
            SendMessage(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon_));
            SendMessage(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon_));
        }
    }
#endif

    return true;
}

bool App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't save layout

    // ImGui's built-in ProggyClean, scaled up a bit for readability. It's
    // monospace, so the space-padded current-conditions column lines up.
    ImFontConfig fontCfg;
    fontCfg.SizePixels = 16.0f;
    io.Fonts->AddFontDefault(&fontCfg);

    // Style: the kengine-site "Korra" palette, shared with filex.
    theme::apply();

    ImGui_ImplSDL2_InitForOpenGL(window_, glContext_);
    ImGui_ImplOpenGL3_Init("#version 330");

    uiRenderer_ = new UIRenderer(locationMgr_, config_);
    // Weather condition sprite sheet (6x5 atlas) + moon-phase sheet (5x3). Both
    // share the window's #1B1613 background, so they blend without color-keying.
    // Invalid texture -> icons just won't draw.
    uiRenderer_->setIconAtlas(loadTexture(resourcePath("weather-icons.png")));
    uiRenderer_->setMoonAtlas(loadTexture(resourcePath("moon-phases.png")));
    return true;
}

bool App::initSystemTray() {
#ifdef _WIN32
    if (!hwnd_) return false;

    HICON icon = appIcon_ ? appIcon_ : LoadIcon(nullptr, IDI_APPLICATION);

    // Subclass the WndProc
    s_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CustomWndProc)));

    return systemTray_.initialize(hwnd_, icon);
#else
    return systemTray_.initialize();
#endif
}

bool App::initNotifications() {
    return notificationMgr_.initialize(&systemTray_);
}

bool App::initialize() {
    if (!config_.load()) return false;
    if (!initSDL()) return false;
    if (!initImGui()) return false;
    initSystemTray();
    initNotifications();

    // Start background worker
    backgroundRunning_ = true;
    backgroundThread_ = std::thread(&App::backgroundWorkerLoop, this);

    // If we have locations, fetch weather immediately
    if (locationMgr_.hasLocations()) {
        requestWeatherUpdate(locationMgr_.activeLocation().geo);
    }

    // Handle start minimized
    if (config_.startMinimized()) {
        minimizeToTray();
    }

    return true;
}

void App::minimizeToTray() {
    if (window_) {
        SDL_HideWindow(window_);
    }
    minimizedToTray_ = true;
}

void App::restoreFromTray() {
    if (window_) {
        SDL_ShowWindow(window_);
        SDL_RaiseWindow(window_);
    }
    minimizedToTray_ = false;
}

void App::requestWeatherUpdate(const GeoLocation& loc, bool isRefresh) {
    WorkRequest req;
    req.type = WorkRequest::FetchWeather;
    req.location = loc;
    req.isRefresh = isRefresh;
    // Snapshot config here on the UI thread (the only thread that mutates it)
    // so the worker doesn't read config_ concurrently.
    req.alertsEnabled = config_.alertsEnabled();
    req.useFahrenheit = config_.useFahrenheit();
    workQueue_.push(req);

    std::lock_guard<std::mutex> lock(dataMutex_);
    isLoading_ = true;
}

void App::requestGeocode(const std::string& query) {
    WorkRequest req;
    req.type = WorkRequest::Geocode;
    req.query = query;
    workQueue_.push(req);
}

void App::backgroundWorkerLoop() {
    while (backgroundRunning_) {
        WorkRequest req;
        if (workQueue_.waitPopFor(req, std::chrono::seconds(5))) {
            WorkResult result;
            result.type = req.type;

            if (req.type == WorkRequest::FetchWeather) {
                auto weatherData = weatherService_.fetchWeather(req.location);
                if (weatherData) {
                    result.weatherData = weatherData;
                    if (req.alertsEnabled) {
                        result.alerts =
                            alertEngine_.evaluate(*weatherData, req.useFahrenheit);
                    }
                } else {
                    result.error = "Couldn't reach the weather service - check your connection.";
                }
            } else if (req.type == WorkRequest::Geocode) {
                result.geocodeResults = weatherService_.geocode(req.query);
                if (result.geocodeResults.empty()) {
                    result.error = "No results found";
                }
            }

            resultQueue_.push(std::move(result));
        }
    }
}

void App::updateFromBackground() {
    WorkResult result;
    while (resultQueue_.tryPop(result)) {
        if (result.type == WorkRequest::FetchWeather) {
            std::lock_guard<std::mutex> lock(dataMutex_);
            isLoading_ = false;

            if (result.weatherData) {
                lastError_.clear();
                currentWeatherData_ = *result.weatherData;
                activeAlerts_ = result.alerts;

                // Update tray tooltip
                std::string tip = formatTemp(currentWeatherData_.current.temperature) + " " +
                    weatherCodeToString(currentWeatherData_.current.weatherCode) + " - " +
                    currentWeatherData_.location.name;
#ifdef _WIN32
                systemTray_.setTooltip(utf8ToWide(tip));
#else
                systemTray_.setTooltip(tip);
#endif

                // Log new alerts into the notification center and queue them
                // for a pop-up; maybeNotify() decides *when* per the schedule.
                if (config_.alertsEnabled()) {
                    auto nowSys = std::chrono::system_clock::now();
                    for (auto& alert : activeAlerts_) {
                        if (!alertEngine_.wasRecentlyNotified(alert)) {
                            alertEngine_.markNotified(alert);
                            Notification n;
                            n.alert = alert;
                            n.time = nowSys;
                            notifications_.push_back(n);
                            unpoppedKeys_.insert(alert.deduplicationKey());
                        }
                    }
                    constexpr size_t kMaxHistory = 50;
                    if (notifications_.size() > kMaxHistory) {
                        notifications_.erase(
                            notifications_.begin(),
                            notifications_.begin() +
                                (notifications_.size() - kMaxHistory));
                    }
                }
            } else {
                // Fetch failed - remember why so the status bar can show it.
                lastError_ = result.error.empty() ? "Update failed" : result.error;
            }
        } else if (result.type == WorkRequest::Geocode) {
            if (uiRenderer_) {
                uiRenderer_->setSearchResults(result.geocodeResults);
            }
        }
    }
}

void App::maybeNotify() {
    if (!config_.alertsEnabled()) return;

    // User's local wall-clock (notifications follow the user, not the location).
    std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    int nowMin = lt.tm_hour * 60 + lt.tm_min;
    int yday = lt.tm_yday;
    NotifyMode mode = config_.notifyMode();

    std::vector<WeatherAlert> toPop;
    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        if (mode != NotifyMode::Digest && unpoppedKeys_.empty()) return;

        bool release = false;
        if (mode == NotifyMode::Immediate) {
            release = true;
        } else if (mode == NotifyMode::QuietHours) {
            release = !inQuietWindow(nowMin, config_.quietStartMinute(),
                                     config_.quietEndMinute());
        } else {  // Digest: once per day at/after the digest time
            if (nowMin >= config_.digestMinute() && lastDigestDay_ != yday &&
                !unpoppedKeys_.empty()) {
                release = true;
                lastDigestDay_ = yday;
            }
        }

        if (release) {
            for (auto& n : notifications_) {
                if (unpoppedKeys_.count(n.alert.deduplicationKey())) {
                    toPop.push_back(n.alert);
                }
            }
            unpoppedKeys_.clear();
        }
    }

    if (!toPop.empty()) notificationMgr_.showCombinedAlerts(toPop);
}

void App::processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT) {
            running_ = false;
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE) {
            minimizeToTray();
        }
    }

    // Handle tray actions
    auto action = systemTray_.pollAction();
    if (action) {
        switch (*action) {
        case SystemTray::MenuAction::Show:
            restoreFromTray();
            break;
        case SystemTray::MenuAction::Refresh:
            if (locationMgr_.hasLocations()) {
                requestWeatherUpdate(locationMgr_.activeLocation().geo, true);
            }
            break;
        case SystemTray::MenuAction::Quit:
            running_ = false;
            break;
        }
    }

    // Handle UI actions
    if (uiRenderer_) {
        UIActions actions = uiRenderer_->consumeActions();

        if (actions.refreshRequested && locationMgr_.hasLocations()) {
            requestWeatherUpdate(locationMgr_.activeLocation().geo, true);
        }
        if (actions.locationChanged && locationMgr_.hasLocations()) {
            requestWeatherUpdate(locationMgr_.activeLocation().geo);
        }
        if (!actions.searchQuery.empty()) {
            requestGeocode(actions.searchQuery);
        }
        if (actions.addLocationRequested && !actions.selectedLocation.name.empty()) {
            locationMgr_.addLocation(actions.selectedLocation);
            int newIdx = static_cast<int>(locationMgr_.locations().size()) - 1;
            locationMgr_.setActive(newIdx);
            requestWeatherUpdate(locationMgr_.activeLocation().geo);
        }
        if (actions.removeLocationRequested && locationMgr_.hasLocations()) {
            locationMgr_.removeLocation(locationMgr_.activeIndex());
            if (locationMgr_.hasLocations()) {
                requestWeatherUpdate(locationMgr_.activeLocation().geo);
            }
        }
        if (actions.settingsChanged) {
            config_.save();
        }
        if (!actions.acknowledgeKey.empty()) {
            std::lock_guard<std::mutex> lock(dataMutex_);
            acknowledged_.insert(actions.acknowledgeKey);
            for (auto& n : notifications_) {
                if (n.alert.deduplicationKey() == actions.acknowledgeKey) {
                    n.acknowledged = true;
                }
            }
        }
        if (actions.clearNotifications) {
            std::lock_guard<std::mutex> lock(dataMutex_);
            notifications_.clear();
            unpoppedKeys_.clear();
            // Keep acknowledged_ so dismissed banner alerts stay quiet.
        }
    }
}

void App::renderFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    {
        std::lock_guard<std::mutex> lock(dataMutex_);
        // Banner = currently-active alerts the user hasn't acknowledged.
        std::vector<WeatherAlert> banner;
        for (const auto& a : activeAlerts_) {
            if (!acknowledged_.count(a.deduplicationKey())) banner.push_back(a);
        }
        uiRenderer_->render(currentWeatherData_, banner, notifications_,
                            isLoading_, lastError_);
    }

    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
    glClearColor(theme::Bg.x, theme::Bg.y, theme::Bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}

void App::run() {
    lastPollTime_ = std::chrono::steady_clock::now();

    while (running_) {
        processEvents();
        updateFromBackground();
        maybeNotify();  // pop queued alerts per the scheduler mode

        // Auto-poll check
        auto now = std::chrono::steady_clock::now();
        int pollInterval = minimizedToTray_
            ? config_.backgroundPollMinutes()
            : config_.foregroundPollMinutes();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - lastPollTime_).count();
        if (elapsed >= pollInterval && locationMgr_.hasLocations()) {
            requestWeatherUpdate(locationMgr_.activeLocation().geo);
            lastPollTime_ = now;
        }

        if (!minimizedToTray_) {
            renderFrame();
        } else {
            SDL_Delay(100);
        }
    }
}

void App::shutdown() {
    // Called from both run()'s exit path and ~App(); make it idempotent so the
    // ImGui/SDL teardown below never runs twice on already-freed state.
    if (shutdownDone_) return;
    shutdownDone_ = true;

    // Stop background thread
    backgroundRunning_ = false;
    workQueue_.push(WorkRequest{}); // wake up the thread
    if (backgroundThread_.joinable()) {
        backgroundThread_.join();
    }

    // Cleanup
    systemTray_.shutdown();
    notificationMgr_.shutdown();

    if (uiRenderer_) {
        delete uiRenderer_;
        uiRenderer_ = nullptr;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (glContext_) {
        SDL_GL_DeleteContext(glContext_);
        glContext_ = nullptr;
    }
#ifdef _WIN32
    // Un-subclass the window before we destroy it, so no late WM_* is routed
    // through CustomWndProc after teardown.
    if (s_originalWndProc && hwnd_) {
        SetWindowLongPtr(hwnd_, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(s_originalWndProc));
        s_originalWndProc = nullptr;
    }
    if (appIcon_) {
        DestroyIcon(appIcon_);
        appIcon_ = nullptr;
    }
#endif
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    SDL_Quit();
}

std::string App::formatTemp(double celsius) const {
    char buf[32];
    if (config_.useFahrenheit()) {
        std::snprintf(buf, sizeof(buf), "%.0fF", celsiusToFahrenheit(celsius));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0fC", celsius);
    }
    return buf;
}

} // namespace wd
