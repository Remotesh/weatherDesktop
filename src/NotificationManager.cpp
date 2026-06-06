#include "weatherdesktop/NotificationManager.h"
#include "weatherdesktop/SystemTray.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::wstring toWide(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (sz <= 0) return L"";  // invalid UTF-8 - don't underflow sz - 1
    std::wstring result(sz - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], sz);
    return result;
}
#endif

namespace wd {

bool NotificationManager::initialize(SystemTray* tray) {
    systemTray_ = tray;
    initialized_ = true;
    return true;
}

void NotificationManager::shutdown() {
    initialized_ = false;
    systemTray_ = nullptr;
}

bool NotificationManager::isSupported() const {
    return initialized_ && systemTray_ != nullptr;
}

void NotificationManager::showAlert(const WeatherAlert& alert) {
#ifdef _WIN32
    showNotification(toWide(alert.title), toWide(alert.message));
#else
    showNotification(alert.title, alert.message);
#endif
}

void NotificationManager::showCombinedAlerts(const std::vector<WeatherAlert>& alerts) {
    if (alerts.empty()) return;
    if (alerts.size() == 1) {
        showAlert(alerts[0]);
        return;
    }

    std::string title = "Weather Alerts (" + std::to_string(alerts.size()) + ")";
    std::string body;
    for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) body += "\n";
        body += "- " + alerts[i].message;
    }

#ifdef _WIN32
    showNotification(toWide(title), toWide(body));
#else
    showNotification(title, body);
#endif
}

#ifdef _WIN32
void NotificationManager::showNotification(const std::wstring& title, const std::wstring& body) {
    if (!initialized_ || !systemTray_) return;
    systemTray_->showBalloon(title, body);
}
#else
void NotificationManager::showNotification(const std::string&, const std::string&) {
    // No-op on Linux (can add libnotify support later)
}
#endif

} // namespace wd
