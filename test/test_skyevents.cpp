#include "test_framework.h"
#include "weatherdesktop/SkyEvents.h"

#include <cmath>

using namespace wd;

// A known new moon used by the implementation: 2000-01-06 18:14 UTC.
static const std::time_t kKnownNewMoon = 947182440;
static const double kSynodicSec = 29.530588853 * 86400.0;

TEST_CASE(moon_new_at_reference) {
    MoonInfo m = computeMoon(kKnownNewMoon);
    CHECK_NEAR(m.phase, 0.0, 1e-3);
    CHECK_NEAR(m.illumination, 0.0, 1e-3);
    CHECK_EQ(m.index, 0);
    CHECK(m.waxing);
    CHECK_STR_EQ(m.name, "New Moon");
}

TEST_CASE(moon_full_at_half_cycle) {
    MoonInfo m = computeMoon(kKnownNewMoon + (std::time_t)(kSynodicSec / 2.0));
    CHECK_NEAR(m.phase, 0.5, 1e-2);
    CHECK_NEAR(m.illumination, 1.0, 1e-2);
    CHECK_EQ(m.index, 4);
    CHECK_STR_EQ(m.name, "Full Moon");
}

TEST_CASE(moon_invariants_across_cycle) {
    for (int i = 0; i < 30; ++i) {
        MoonInfo m = computeMoon(kKnownNewMoon + (std::time_t)(i * 86400));
        CHECK(m.phase >= 0.0 && m.phase < 1.0);
        CHECK(m.illumination >= 0.0 && m.illumination <= 1.0);
        CHECK(m.index >= 0 && m.index <= 7);
    }
}

TEST_CASE(aurora_unknown_when_no_kp) {
    CHECK(auroraChance(-1.0, 60.0) == AuroraChance::Unknown);
}

TEST_CASE(aurora_thresholds) {
    // boundary = 67 - 2*kp.  kp=0 -> boundary 67.
    CHECK(auroraChance(0.0, 0.0) == AuroraChance::Unlikely);   // far south
    CHECK(auroraChance(0.0, 65.0) == AuroraChance::Possible);  // within 3 deg of boundary
    CHECK(auroraChance(0.0, 67.0) == AuroraChance::Likely);    // at/above boundary
    // kp=5 -> boundary 57; lat 60 is above it.
    CHECK(auroraChance(5.0, 60.0) == AuroraChance::Likely);
    // Latitude sign shouldn't matter (southern hemisphere).
    CHECK(auroraChance(5.0, -60.0) == AuroraChance::Likely);
}

TEST_CASE(aurora_labels) {
    CHECK_STR_EQ(auroraChanceLabel(AuroraChance::Likely), "Likely");
    CHECK_STR_EQ(auroraChanceLabel(AuroraChance::Possible), "Possible");
    CHECK_STR_EQ(auroraChanceLabel(AuroraChance::Unlikely), "Unlikely");
    CHECK_STR_EQ(auroraChanceLabel(AuroraChance::Unknown), "--");
}

TEST_CASE(meteor_perseids_in_august) {
    // 2021-08-12 12:00 UTC -- squarely inside the Perseids window (peak Aug 12),
    // and far enough from a day boundary that any local timezone still lands on
    // Aug 11-13, all within range.
    std::time_t aug12 = 1628769600;
    auto showers = activeMeteorShowers(aug12);

    bool foundPerseids = false;
    for (const auto& s : showers) {
        CHECK(s.name != nullptr);
        CHECK(s.zhr > 0);
        if (std::string(s.name) == "Perseids") {
            foundPerseids = true;
            CHECK(s.nearPeak);  // within a couple days of Aug 12
        }
    }
    CHECK(foundPerseids);

    // Results are sorted by proximity to peak (|daysToPeak| ascending).
    for (size_t i = 1; i < showers.size(); ++i) {
        CHECK(std::abs(showers[i - 1].daysToPeak) <= std::abs(showers[i].daysToPeak));
    }
}
