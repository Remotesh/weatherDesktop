#include "weatherdesktop/LocationManager.h"

namespace wd {

LocationManager::LocationManager(Config& config, WeatherService& weatherService)
    : config_(config), weatherService_(weatherService) {}

const std::vector<SavedLocation>& LocationManager::locations() const {
    return config_.locations();
}

bool LocationManager::hasLocations() const {
    return !config_.locations().empty();
}

const SavedLocation& LocationManager::activeLocation() const {
    return config_.locations()[config_.activeLocationIndex()];
}

int LocationManager::activeIndex() const {
    return config_.activeLocationIndex();
}

void LocationManager::cycleNext() {
    if (config_.locations().size() <= 1) return;
    int next = (config_.activeLocationIndex() + 1) % static_cast<int>(config_.locations().size());
    config_.setActiveLocationIndex(next);
}

void LocationManager::cyclePrev() {
    if (config_.locations().size() <= 1) return;
    int sz = static_cast<int>(config_.locations().size());
    int prev = (config_.activeLocationIndex() - 1 + sz) % sz;
    config_.setActiveLocationIndex(prev);
}

void LocationManager::setActive(int index) {
    config_.setActiveLocationIndex(index);
}

void LocationManager::addLocation(const GeoLocation& geo) {
    SavedLocation loc;
    loc.geo = geo;
    loc.isDefault = config_.locations().empty();
    config_.addLocation(loc);
}

void LocationManager::removeLocation(size_t index) {
    config_.removeLocation(index);
}

void LocationManager::updateActive(const GeoLocation& geo) {
    config_.updateLocation(static_cast<size_t>(config_.activeLocationIndex()), geo);
}

std::vector<GeoLocation> LocationManager::search(const std::string& query) {
    return weatherService_.geocode(query);
}

} // namespace wd
