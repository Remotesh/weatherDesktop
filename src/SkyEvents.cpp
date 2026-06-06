#include "weatherdesktop/SkyEvents.h"

#include <algorithm>
#include <cmath>

namespace wd {

namespace {
constexpr double kPi = 3.14159265358979323846;
// Synodic month (new moon -> new moon).
constexpr double kSynodicSec = 29.530588853 * 86400.0;
// A known new moon: 2000-01-06 18:14 UTC (unix seconds).
constexpr double kKnownNewMoon = 947182440.0;
}  // namespace

MoonInfo computeMoon(std::time_t when) {
    MoonInfo m;
    double diff = static_cast<double>(when) - kKnownNewMoon;
    double p = std::fmod(diff, kSynodicSec);
    if (p < 0) p += kSynodicSec;
    p /= kSynodicSec;  // 0..1

    m.phase = p;
    m.illumination = (1.0 - std::cos(2.0 * kPi * p)) * 0.5;
    m.waxing = p < 0.5;

    // Eight named phases centered on their fractions.
    static const char* names[] = {
        "New Moon",       "Waxing Crescent", "First Quarter", "Waxing Gibbous",
        "Full Moon",      "Waning Gibbous",  "Last Quarter",  "Waning Crescent"};
    int idx = static_cast<int>(std::lround(p * 8.0)) % 8;
    m.index = idx;
    m.name = names[idx];
    return m;
}

namespace {
struct ShowerDef {
    const char* name;
    int startMD;  // month*100 + day
    int peakMD;
    int endMD;
    int zhr;
};

// Major annual showers. Quadrantids wrap the new year (handled below).
const ShowerDef kShowers[] = {
    {"Quadrantids", 1228, 104, 112, 110},
    {"Lyrids", 416, 422, 425, 18},
    {"Eta Aquariids", 419, 506, 528, 50},
    {"Delta Aquariids", 712, 730, 823, 25},
    {"Perseids", 717, 812, 824, 100},
    {"Draconids", 1006, 1008, 1010, 10},
    {"Orionids", 1002, 1021, 1107, 20},
    {"Leonids", 1106, 1117, 1130, 15},
    {"Geminids", 1204, 1214, 1217, 120},
    {"Ursids", 1217, 1222, 1226, 10},
};

bool inRange(int startMD, int endMD, int todayMD) {
    if (startMD <= endMD) return todayMD >= startMD && todayMD <= endMD;
    return todayMD >= startMD || todayMD <= endMD;  // wraps the year
}

// Approx day distance between two month/day points (ignoring year length nuance).
int mdToOrdinal(int md) {
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int mo = md / 100, day = md % 100;
    if (mo < 1) mo = 1;
    if (mo > 12) mo = 12;
    return cum[mo - 1] + day;
}
}  // namespace

std::vector<ActiveShower> activeMeteorShowers(std::time_t when) {
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &when);
#else
    localtime_r(&when, &lt);
#endif
    int todayMD = (lt.tm_mon + 1) * 100 + lt.tm_mday;
    int todayOrd = mdToOrdinal(todayMD);

    std::vector<ActiveShower> out;
    for (const auto& s : kShowers) {
        if (!inRange(s.startMD, s.endMD, todayMD)) continue;
        int peakOrd = mdToOrdinal(s.peakMD);
        int d = peakOrd - todayOrd;
        // Handle year-wrap for the day delta (e.g. peak in Jan, today in Dec).
        if (d > 182) d -= 365;
        if (d < -182) d += 365;
        ActiveShower a;
        a.name = s.name;
        a.daysToPeak = d;
        a.nearPeak = std::abs(d) <= 2;
        a.zhr = s.zhr;
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const ActiveShower& a, const ActiveShower& b) {
        return std::abs(a.daysToPeak) < std::abs(b.daysToPeak);
    });
    return out;
}

AuroraChance auroraChance(double kp, double latitudeDeg) {
    if (kp < 0) return AuroraChance::Unknown;
    // Equatorward edge of the auroral oval (geomagnetic lat, rough). Using
    // geographic latitude as a proxy - good enough for a "maybe tonight" hint.
    double boundary = 67.0 - 2.0 * kp;
    double lat = std::fabs(latitudeDeg);
    if (lat >= boundary) return AuroraChance::Likely;
    if (lat >= boundary - 3.0) return AuroraChance::Possible;
    return AuroraChance::Unlikely;
}

const char* auroraChanceLabel(AuroraChance c) {
    switch (c) {
        case AuroraChance::Likely: return "Likely";
        case AuroraChance::Possible: return "Possible";
        case AuroraChance::Unlikely: return "Unlikely";
        default: return "--";
    }
}

}  // namespace wd
