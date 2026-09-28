#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Solar_System/BodyCatalog.generated.h"
#include "Solar_System/SatelliteOrbit.h"

namespace {

constexpr int kMoonIndex = 12;
constexpr int kIoIndex = 15;
constexpr int kMimasIndex = 19; // placeholder Keplerian row — stays on the circular path

const BodyCatalog::Entry& Row(int index) {
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(index);
    EXPECT_NE(entry, nullptr);
    return *entry;
}

} // namespace

TEST(SatelliteOrbitTest, EphemerisOffsetPlacesMoonsWithASolution) {
    glm::vec3 offset(0.0f);
    EXPECT_TRUE(SatelliteOrbit::EphemerisOffset(Row(kMoonIndex), Ephemeris::kJ2000, offset));
    EXPECT_GT(glm::length(offset), 0.0f);

    EXPECT_TRUE(SatelliteOrbit::EphemerisOffset(Row(kIoIndex), Ephemeris::kJ2000, offset));
    EXPECT_GT(glm::length(offset), 0.0f);
}

TEST(SatelliteOrbitTest, PlaceholderRowsKeepTheCircularOffset) {
    // Mimas' catalog row still carries zeroed node and periapsis angles, so the backend must
    // refuse it and CatalogSatellite must fall back rather than tilt it into a wrong plane.
    glm::vec3 offset(1.0f);
    EXPECT_FALSE(SatelliteOrbit::EphemerisOffset(Row(kMimasIndex), Ephemeris::kJ2000, offset));
}

TEST(SatelliteOrbitTest, OffsetRadiusStaysNearTheCatalogArtRadius) {
    // The AU vector is rescaled so semi-major axis maps onto sceneOrbitRadius. Eccentricity
    // survives that (the Moon's e = 0.055), so the radius should hover within a few percent
    // of the art radius rather than sitting exactly on it.
    const BodyCatalog::Entry& moon = Row(kMoonIndex);
    float minRadius = 1.0e9f;
    float maxRadius = 0.0f;

    for (double day = 0.0; day < 28.0; day += 0.1) {
        glm::vec3 offset(0.0f);
        ASSERT_TRUE(SatelliteOrbit::EphemerisOffset(moon, Ephemeris::kJ2000 + day, offset));
        const float radius = glm::length(offset);
        minRadius = std::min(minRadius, radius);
        maxRadius = std::max(maxRadius, radius);
    }

    EXPECT_NEAR(minRadius, moon.sceneOrbitRadius * (1.0f - moon.keplerian.e), 0.2f);
    EXPECT_NEAR(maxRadius, moon.sceneOrbitRadius * (1.0f + moon.keplerian.e), 0.2f);
}

TEST(SatelliteOrbitTest, OffsetLeavesTheParentEquatorialPlane) {
    // Scene Y is the ecliptic pole, so a non-zero Y is the whole reason this path exists:
    // the old circular Offset() pinned every moon to y = 0 and no umbra could ever miss.
    const BodyCatalog::Entry& moon = Row(kMoonIndex);
    float maxAbsY = 0.0f;
    for (double day = 0.0; day < 28.0; day += 0.1) {
        glm::vec3 offset(0.0f);
        ASSERT_TRUE(SatelliteOrbit::EphemerisOffset(moon, Ephemeris::kJ2000 + day, offset));
        maxAbsY = std::max(maxAbsY, std::fabs(offset.y));
    }
    // 5.15 deg of inclination on a 25-unit art orbit is ~2.2 units of swing.
    EXPECT_GT(maxAbsY, 1.5f);
}

TEST(SatelliteOrbitTest, OffsetIsAFunctionOfEpochNotOfCallCount) {
    // Placement must be reproducible from the Julian date alone; otherwise scrubbing time
    // backwards would not put a moon back where it was.
    glm::vec3 first(0.0f);
    glm::vec3 second(0.0f);
    const double jd = Ephemeris::kJ2000 + 6431.5;
    ASSERT_TRUE(SatelliteOrbit::EphemerisOffset(Row(kIoIndex), jd, first));
    ASSERT_TRUE(SatelliteOrbit::EphemerisOffset(Row(kIoIndex), jd + 900.0, second));
    ASSERT_TRUE(SatelliteOrbit::EphemerisOffset(Row(kIoIndex), jd, second));
    EXPECT_FLOAT_EQ(first.x, second.x);
    EXPECT_FLOAT_EQ(first.y, second.y);
    EXPECT_FLOAT_EQ(first.z, second.z);
}

TEST(SatelliteOrbitTest, CircularAnomalyAndSpinAreFunctionsOfEpoch) {
    // Mimas has no Keplerian solution, so it runs on MeanAnomalyAt/SpinDegreesAt. A date
    // jump and a round trip through another date must land on the same pose.
    const BodyCatalog::Entry& mimas = Row(kMimasIndex);
    const double jd = Ephemeris::kJ2000 + 6440.5; // 2017-08-21
    const float anomaly = SatelliteOrbit::MeanAnomalyAt(mimas, jd);
    const float spin = SatelliteOrbit::SpinDegreesAt(mimas.spinDegPerSimSecond, jd);

    (void)SatelliteOrbit::MeanAnomalyAt(mimas, Ephemeris::kJ2000 - 3653.0);
    EXPECT_FLOAT_EQ(SatelliteOrbit::MeanAnomalyAt(mimas, jd), anomaly);
    EXPECT_FLOAT_EQ(SatelliteOrbit::SpinDegreesAt(mimas.spinDegPerSimSecond, jd), spin);

    // J2000 is the catalog's starting pose; one full period later is the same place.
    EXPECT_NEAR(SatelliteOrbit::MeanAnomalyAt(mimas, Ephemeris::kJ2000),
                std::fmod(mimas.initialAnomalyRad + SatelliteOrbit::kTwoPi, SatelliteOrbit::kTwoPi), 1e-5f);
    EXPECT_NEAR(SatelliteOrbit::MeanAnomalyAt(mimas, jd + mimas.orbitalPeriodDays), anomaly, 1e-3f);
    EXPECT_GE(anomaly, 0.0f);
    EXPECT_LT(anomaly, SatelliteOrbit::kTwoPi);
    EXPECT_GE(spin, 0.0f);
    EXPECT_LT(spin, 360.0f);
}

TEST(SatelliteOrbitTest, CircularAnomalyMatchesTheOldPerFrameRate) {
    // The retired AdvanceAnomaly integrated 2*pi/period per (120 s * P/365.25) of sim time;
    // the closed form must move at that same rate so moons keep their on-screen speed.
    const BodyCatalog::Entry& mimas = Row(kMimasIndex);
    const double quarterPeriodDays = mimas.orbitalPeriodDays / 4.0;
    const float a0 = SatelliteOrbit::MeanAnomalyAt(mimas, Ephemeris::kJ2000);
    const float a1 = SatelliteOrbit::MeanAnomalyAt(mimas, Ephemeris::kJ2000 + quarterPeriodDays);
    float delta = a1 - a0;
    if (delta < 0.0f) {
        delta += SatelliteOrbit::kTwoPi;
    }
    EXPECT_NEAR(delta, SatelliteOrbit::kTwoPi / 4.0f, 1e-4f);

    // Spin: one Earth year of sim time is kEarthOrbitSecondsAt1x sim-seconds.
    const float spinYear = SatelliteOrbit::SpinDegreesAt(1.0f, Ephemeris::kJ2000 + 365.25);
    EXPECT_NEAR(spinYear, std::fmod(OrbitLayout::kEarthOrbitSecondsAt1x, 360.0f), 1e-3f);
}
