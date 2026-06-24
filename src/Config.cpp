#include "weatherdesktop/Config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>

namespace wd {

using json = nlohmann::json;
namespace fs = std::filesystem;

std::string Config::configDir() const {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (!appdata) return "./";
    return std::string(appdata) + "/WeatherDesktop";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) return std::string(xdg) + "/WeatherDesktop";
    const char* home = std::getenv("HOME");
    if (home) return std::string(home) + "/.config/WeatherDesktop";
    return "./";
#endif
}

std::string Config::configPath() const {
    return configDir() + "/config.json";
}

bool Config::load() {
    std::string path = configPath();
    if (!fs::exists(path)) return true; // first run, use defaults

    std::ifstream file(path);
    if (!file.is_open()) return true;  // unreadable - start with defaults

    try {
        json j;
        file >> j;

        if (j.contains("settings")) {
            auto& s = j["settings"];
            useFahrenheit_ = s.value("useFahrenheit", true);
            useMph_ = s.value("useMph", true);
            foregroundPollMin_ = s.value("foregroundPollMinutes", 15);
            backgroundPollMin_ = s.value("backgroundPollMinutes", 30);
            alertsEnabled_ = s.value("alertsEnabled", true);
            rainAlertsEnabled_ = s.value("rainAlertsEnabled", true);
            startMinimized_ = s.value("startMinimized", false);
            carouselSeconds_ = s.value("carouselSeconds", 10);
            themeId_ = s.value("themeId", 0);
            int mode = s.value("notifyMode", 0);
            if (mode < 0 || mode > 2) mode = 0;
            notifyMode_ = static_cast<NotifyMode>(mode);
            quietStartMin_ = s.value("quietStartMinute", 22 * 60);
            quietEndMin_ = s.value("quietEndMinute", 7 * 60);
            digestMin_ = s.value("digestMinute", 8 * 60);
        }

        activeIndex_ = j.value("activeLocationIndex", 0);

        if (j.contains("locations")) {
            locations_.clear();
            for (auto& locJ : j["locations"]) {
                SavedLocation loc;
                loc.geo.name = locJ.value("name", "");
                loc.geo.country = locJ.value("country", "");
                loc.geo.admin1 = locJ.value("admin1", "");
                loc.geo.latitude = locJ.value("latitude", 0.0);
                loc.geo.longitude = locJ.value("longitude", 0.0);
                loc.geo.timezone = locJ.value("timezone", "");
                loc.geo.customLabel = locJ.value("customLabel", false);
                loc.isDefault = locJ.value("isDefault", false);
                locations_.push_back(std::move(loc));
            }
        }

        if (activeIndex_ >= static_cast<int>(locations_.size())) {
            activeIndex_ = locations_.empty() ? 0 : static_cast<int>(locations_.size()) - 1;
        }

    } catch (const json::exception&) {
        // Corrupt config file: don't refuse to start - reset to a clean default
        // state and carry on (the next save() will overwrite the bad file).
        locations_.clear();
        activeIndex_ = 0;
        return true;
    }

    return true;
}

bool Config::save() const {
    std::string dir = configDir();
    fs::create_directories(dir);

    json j;
    j["version"] = 1;
    j["settings"] = {
        {"useFahrenheit", useFahrenheit_},
        {"useMph", useMph_},
        {"foregroundPollMinutes", foregroundPollMin_},
        {"backgroundPollMinutes", backgroundPollMin_},
        {"alertsEnabled", alertsEnabled_},
        {"rainAlertsEnabled", rainAlertsEnabled_},
        {"startMinimized", startMinimized_},
        {"carouselSeconds", carouselSeconds_},
        {"themeId", themeId_},
        {"notifyMode", static_cast<int>(notifyMode_)},
        {"quietStartMinute", quietStartMin_},
        {"quietEndMinute", quietEndMin_},
        {"digestMinute", digestMin_}
    };
    j["activeLocationIndex"] = activeIndex_;

    json locs = json::array();
    for (auto& loc : locations_) {
        locs.push_back({
            {"name", loc.geo.name},
            {"country", loc.geo.country},
            {"admin1", loc.geo.admin1},
            {"latitude", loc.geo.latitude},
            {"longitude", loc.geo.longitude},
            {"timezone", loc.geo.timezone},
            {"customLabel", loc.geo.customLabel},
            {"isDefault", loc.isDefault}
        });
    }
    j["locations"] = locs;

    std::ofstream file(configPath());
    if (!file.is_open()) return false;
    file << j.dump(2);
    return true;
}

void Config::addLocation(const SavedLocation& loc) {
    locations_.push_back(loc);
    if (locations_.size() == 1) activeIndex_ = 0;
    save();
}

void Config::updateLocation(size_t index, const GeoLocation& geo) {
    if (index >= locations_.size()) return;
    locations_[index].geo = geo;
    save();
}

void Config::removeLocation(size_t index) {
    if (index >= locations_.size()) return;
    locations_.erase(locations_.begin() + index);
    if (activeIndex_ >= static_cast<int>(locations_.size())) {
        activeIndex_ = locations_.empty() ? 0 : static_cast<int>(locations_.size()) - 1;
    }
    save();
}

void Config::setActiveLocationIndex(int idx) {
    if (idx >= 0 && idx < static_cast<int>(locations_.size())) {
        activeIndex_ = idx;
        save();
    }
}

} // namespace wd
