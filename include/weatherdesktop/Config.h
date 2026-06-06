#pragma once

#include "WeatherTypes.h"
#include <string>
#include <vector>
#include <mutex>

namespace wd {

// How weather-alert pop-up (tray balloon) notifications are delivered.
enum class NotifyMode {
    Immediate = 0,   // pop up as alerts are found
    QuietHours = 1,  // suppress pop-ups during a nightly window
    Digest = 2       // one summary pop-up per day at a set time
};

class Config {
public:
    bool load();
    bool save() const;

    // Locations
    const std::vector<SavedLocation>& locations() const { return locations_; }
    void addLocation(const SavedLocation& loc);
    void removeLocation(size_t index);
    int activeLocationIndex() const { return activeIndex_; }
    void setActiveLocationIndex(int idx);

    // Settings
    bool useFahrenheit() const { return useFahrenheit_; }
    void setUseFahrenheit(bool v) { useFahrenheit_ = v; }
    bool useMph() const { return useMph_; }
    void setUseMph(bool v) { useMph_ = v; }
    int foregroundPollMinutes() const { return foregroundPollMin_; }
    int backgroundPollMinutes() const { return backgroundPollMin_; }
    bool alertsEnabled() const { return alertsEnabled_; }
    void setAlertsEnabled(bool v) { alertsEnabled_ = v; }
    bool startMinimized() const { return startMinimized_; }
    void setStartMinimized(bool v) { startMinimized_ = v; }

    // Notification scheduling. Times are minutes since local midnight.
    NotifyMode notifyMode() const { return notifyMode_; }
    void setNotifyMode(NotifyMode m) { notifyMode_ = m; }
    int quietStartMinute() const { return quietStartMin_; }
    void setQuietStartMinute(int m) { quietStartMin_ = m; }
    int quietEndMinute() const { return quietEndMin_; }
    void setQuietEndMinute(int m) { quietEndMin_ = m; }
    int digestMinute() const { return digestMin_; }
    void setDigestMinute(int m) { digestMin_ = m; }

    std::string configDir() const;

private:
    std::string configPath() const;

    std::vector<SavedLocation> locations_;
    int activeIndex_ = 0;
    bool useFahrenheit_ = true;
    bool useMph_ = true;
    int foregroundPollMin_ = 15;
    int backgroundPollMin_ = 30;
    bool alertsEnabled_ = true;
    bool startMinimized_ = false;

    NotifyMode notifyMode_ = NotifyMode::Immediate;
    int quietStartMin_ = 22 * 60;  // 22:00
    int quietEndMin_ = 7 * 60;     // 07:00
    int digestMin_ = 8 * 60;       // 08:00
};

} // namespace wd
