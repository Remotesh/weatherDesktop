#pragma once

#include "WeatherTypes.h"
#include <string>

namespace wd {

class SystemTray;

class NotificationManager {
public:
    bool initialize(SystemTray* tray);
    void shutdown();

    void showAlert(const WeatherAlert& alert);
    void showCombinedAlerts(const std::vector<WeatherAlert>& alerts);

#ifdef _WIN32
    void showNotification(const std::wstring& title, const std::wstring& body);
#else
    void showNotification(const std::string& title, const std::string& body);
#endif

    bool isSupported() const;

private:
    bool initialized_ = false;
    SystemTray* systemTray_ = nullptr;
};

} // namespace wd
