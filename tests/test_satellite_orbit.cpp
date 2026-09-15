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
