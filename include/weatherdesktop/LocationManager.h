#pragma once

#include "WeatherTypes.h"
#include "Config.h"
#include "WeatherService.h"
#include <vector>
#include <string>

namespace wd {

class LocationManager {
public:
    LocationManager(Config& config, WeatherService& weatherService);

    const std::vector<SavedLocation>& locations() const;
    bool hasLocations() const;
    const SavedLocation& activeLocation() const;
    int activeIndex() const;

    void cycleNext();
    void cyclePrev();
    void setActive(int index);
    void addLocation(const GeoLocation& geo);
    void removeLocation(size_t index);
    void updateActive(const GeoLocation& geo);

    // Blocking -- call from background thread
    std::vector<GeoLocation> search(const std::string& query);

private:
    Config& config_;
    WeatherService& weatherService_;
};

} // namespace wd
