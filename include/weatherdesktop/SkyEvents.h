#pragma once

#include <ctime>
#include <string>
#include <vector>

// Night-sky helpers: moon phase, annual meteor showers, and a rough aurora
// visibility estimate. All but the Kp value (fetched elsewhere) are computed
// locally with no network dependency.
namespace wd {

struct MoonInfo {
    double phase = 0.0;         // 0..1 through the synodic month (0 = new, .5 = full)
    double illumination = 0.0;  // 0..1 lit fraction
    bool waxing = true;         // growing toward full?
    int index = 0;              // 0..7 phase bucket (matches the moon atlas cell)
    const char* name = "New Moon";
};

MoonInfo computeMoon(std::time_t when);

struct ActiveShower {
    const char* name;
    bool nearPeak;       // within a couple days of the peak
    int daysToPeak;      // signed; 0 = peak today/now
    int zhr;             // approx peak rate (meteors/hr)
};

// Meteor showers active on the given date (any year), peak-sorted.
std::vector<ActiveShower> activeMeteorShowers(std::time_t when);

enum class AuroraChance { Unknown, Unlikely, Possible, Likely };

// Estimate aurora visibility from the planetary Kp index and the observer's
// latitude. kp < 0 means "no data".
AuroraChance auroraChance(double kp, double latitudeDeg);
const char* auroraChanceLabel(AuroraChance c);

}  // namespace wd
