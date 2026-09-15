#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Solar_System/BodyCatalog.generated.h"
#include "Solar_System/EclipseCaster.h"
#include "Solar_System/SatelliteOrbit.h"

namespace {

// Earth sits 1900 scene units from the Sun (the compressed-mode AU) with the Moon's art
// orbit at 25 units. For the static cases the exact direction does not matter, so the Sun
// is parked on +X and Earth at the origin.
const glm::vec3 kSunAt1Au(1900.0f, 0.0f, 0.0f);
const glm::vec3 kEarthAtOrigin(0.0f, 0.0f, 0.0f);

/**
 * Where the Sun sits relative to Earth in scene space at a given epoch.
 *
 * The ephemeris-driven case cannot park the Sun on +X: the Moon's offset comes back in real
 * ecliptic directions, so a fixed Sun would only ever line up at one point in the year and
 * the test would see no eclipses at all. Axes match OrbitLayout::GetOffset — ecliptic x to
 * scene X, ecliptic z to scene Y, ecliptic y to scene Z.
 */
glm::vec3 SunPositionRelativeToEarth(double julianDate) {
    const Ephemeris::HelioLB earth = Ephemeris::Position(3, julianDate);
    const double cosLat = std::cos(earth.latRad);
    const glm::vec3 earthDirection(static_cast<float>(std::cos(earth.lonRad) * cosLat),
                                   static_cast<float>(std::sin(earth.latRad)),
                                   static_cast<float>(std::sin(earth.lonRad) * cosLat));
    return -earthDirection * 1900.0f;
}

// Radii the shadow test actually uses: real kilometres converted into the Moon's own
// orbital scale, exactly as Application::ConfigureEclipseUmbra does it. The art radii the
// bodies are *drawn* at (2.0 and 0.545) are five times larger, which is why deciding an
// eclipse from them would put a shadow on nearly every new moon.
constexpr float kEarthRadiusKm = 6371.0f;
constexpr float kMoonRadiusKm = 1737.4f;
constexpr float kMoonSemiMajorKm = 384400.0f;
constexpr float kMoonSceneOrbitRadius = 25.0f;
constexpr float kUnitsPerKm = kMoonSceneOrbitRadius / kMoonSemiMajorKm;

constexpr float kEarthRadius = kEarthRadiusKm * kUnitsPerKm; // ~0.414 units
constexpr float kMoonRadius = kMoonRadiusKm * kUnitsPerKm;   // ~0.113 units

} // namespace

TEST(EclipseCasterTest, MoonBetweenPlanetAndStarCastsAShadow) {
    // Dead on the axis: the deepest possible eclipse.
    const glm::vec3 moon(25.0f, 0.0f, 0.0f);
    EXPECT_NEAR(EclipseCaster::ShadowAxisMissDistance(kEarthAtOrigin, kSunAt1Au, moon), 0.0f, 1e-4f);
    EXPECT_TRUE(EclipseCaster::CastsShadowOnPlanet(kEarthAtOrigin, kSunAt1Au, moon, kEarthRadius,
                                                   kMoonRadius));
}

TEST(EclipseCasterTest, MoonBehindThePlanetCastsNothing) {
    // New moon versus full moon. At full moon the shadow points away from Earth entirely,
    // which is exactly the case a sign error would turn into a permanent eclipse.
    const glm::vec3 fullMoon(-25.0f, 0.0f, 0.0f);
    EXPECT_LT(EclipseCaster::ShadowAxisMissDistance(kEarthAtOrigin, kSunAt1Au, fullMoon), 0.0f);
    EXPECT_FALSE(EclipseCaster::CastsShadowOnPlanet(kEarthAtOrigin, kSunAt1Au, fullMoon,
                                                    kEarthRadius, kMoonRadius));
}

TEST(EclipseCasterTest, MoonOffTheAxisMissesThePlanet) {
    // Same phase, but the Moon is well above the ecliptic — the ordinary new moon, which
    // must not produce an eclipse. This is the case the circular XZ offset made impossible
    // to represent, because it pinned every moon to y = 0.
    const glm::vec3 highNewMoon(25.0f, 2.2f, 0.0f);
    const float miss = EclipseCaster::ShadowAxisMissDistance(kEarthAtOrigin, kSunAt1Au, highNewMoon);
    EXPECT_NEAR(miss, 2.2f, 1e-3f);
    EXPECT_FALSE(EclipseCaster::CastsShadowOnPlanet(kEarthAtOrigin, kSunAt1Au, highNewMoon,
                                                    kEarthRadius, kMoonRadius));

    // Just inside the limb still counts; the shader's falloff decides how dark it gets.
    const glm::vec3 grazing(25.0f, kEarthRadius + kMoonRadius - 0.001f, 0.0f);
    EXPECT_TRUE(EclipseCaster::CastsShadowOnPlanet(kEarthAtOrigin, kSunAt1Au, grazing, kEarthRadius,
                                                   kMoonRadius));
}

TEST(EclipseCasterTest, DegenerateStarDistanceIsRefused) {
    EXPECT_LT(EclipseCaster::ShadowAxisMissDistance(kEarthAtOrigin, kEarthAtOrigin,
                                                    glm::vec3(25.0f, 0.0f, 0.0f)),
              0.0f);
}

TEST(EclipseCasterTest, EphemerisMoonEclipsesAFewTimesAYearNotEveryMonth) {
    // End to end over the scene geometry: place the Moon from the real ephemeris for four
    // years and count distinct shadow episodes. There are 2-5 solar eclipses a year, and
    // 49 new moons in four years — so anything near 49 would mean the orbit is effectively
    // flat, which is exactly what the old circular XZ offset produced.
    const BodyCatalog::Entry* moonRow = BodyCatalog::FindByIndex(12);
    ASSERT_NE(moonRow, nullptr);

    int episodes = 0;
    bool inShadow = false;
    for (double day = 0.0; day < 1461.0; day += 0.02) { // four years, ~29-minute steps
        const double julianDate = Ephemeris::kJ2000 + day;
        glm::vec3 offset(0.0f);
        ASSERT_TRUE(SatelliteOrbit::EphemerisOffset(*moonRow, julianDate, offset));
        const bool shadow =
            EclipseCaster::CastsShadowOnPlanet(kEarthAtOrigin, SunPositionRelativeToEarth(julianDate),
                                               offset, kEarthRadius, kMoonRadius);
        if (shadow && !inShadow) {
            ++episodes;
        }
        inShadow = shadow;
    }

    EXPECT_GE(episodes, 6) << "four years must hold at least a couple of eclipses per year";
    EXPECT_LE(episodes, 24) << "more than 6 a year means the orbit has gone coplanar";
}
