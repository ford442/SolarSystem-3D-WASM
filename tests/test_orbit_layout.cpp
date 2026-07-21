#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Solar_System/OrbitLayout.h"

namespace {

constexpr float kEpsilon = 0.01f;

bool Near(float a, float b, float epsilon = kEpsilon) {
    return std::fabs(a - b) <= epsilon;
}

bool Near(const glm::vec3& a, const glm::vec3& b, float epsilon = kEpsilon) {
    return Near(a.x, b.x, epsilon) && Near(a.y, b.y, epsilon) && Near(a.z, b.z, epsilon);
}

glm::vec3 ExpectedOffsetFromEphemeris(OrbitLayout::Body body) {
    const auto pos = Ephemeris::Position(static_cast<int>(body), OrbitLayout::GetJulianDate());
    const float radius = OrbitLayout::GetOrbitRadius(body);
    const float lon = static_cast<float>(pos.lonRad);
    const float lat = static_cast<float>(pos.latRad);
    const float cosLat = std::cos(lat);
    return glm::vec3(
        radius * std::cos(lon) * cosLat,
        radius * std::sin(lat),
        radius * std::sin(lon) * cosLat);
}

} // namespace

class OrbitLayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        OrbitLayout::ResetForTests();
    }
};

TEST_F(OrbitLayoutTest, BodyFromNameMapsKnownPlanets) {
    EXPECT_EQ(OrbitLayout::BodyFromName("Earth"), OrbitLayout::Body::Earth);
    EXPECT_EQ(OrbitLayout::BodyFromName("Pluto"), OrbitLayout::Body::Pluto);
}

TEST_F(OrbitLayoutTest, BodyFromNameFallsBackToSun) {
    EXPECT_EQ(OrbitLayout::BodyFromName("Ceres"), OrbitLayout::Body::Sun);
    EXPECT_EQ(OrbitLayout::BodyFromName(""), OrbitLayout::Body::Sun);
}

TEST_F(OrbitLayoutTest, ResetForTestsUsesJ2000) {
    EXPECT_DOUBLE_EQ(OrbitLayout::GetJulianDate(), Ephemeris::kJ2000);
}

TEST_F(OrbitLayoutTest, CompressedModePlacesEarthFromEphemerisAtJ2000) {
    const glm::vec3 earthOffset = OrbitLayout::GetOffset(OrbitLayout::Body::Earth);
    const glm::vec3 expected = ExpectedOffsetFromEphemeris(OrbitLayout::Body::Earth);
    EXPECT_TRUE(Near(earthOffset, expected, 0.5f));
    EXPECT_TRUE(Near(OrbitLayout::GetSceneDistance(OrbitLayout::Body::Earth), 1900.0f));
    EXPECT_TRUE(Near(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Earth), 1900.0f));
    // Earth at J2000 is near lon ≈ 100°, so mostly +Z with some −X — not the old art +X.
    EXPECT_GT(std::fabs(earthOffset.z), std::fabs(earthOffset.x));
}

TEST_F(OrbitLayoutTest, RealisticModeScalesOrbitRadius) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);

    EXPECT_TRUE(Near(OrbitLayout::GetAuDistance(OrbitLayout::Body::Earth), 1.0f));

    const float expectedJupiterRadius = 5.203f * 1900.0f;
    EXPECT_TRUE(Near(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter), expectedJupiterRadius));
    EXPECT_TRUE(Near(glm::length(OrbitLayout::GetOffset(OrbitLayout::Body::Jupiter)), expectedJupiterRadius, 1.0f));
    EXPECT_GT(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter),
              glm::length(OrbitLayout::GetCompressedOffset(OrbitLayout::Body::Jupiter)));
}

TEST_F(OrbitLayoutTest, SunOffsetIsZeroInBothModes) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    EXPECT_TRUE(Near(OrbitLayout::GetOffset(OrbitLayout::Body::Sun), glm::vec3(0.0f)));
    EXPECT_FLOAT_EQ(OrbitLayout::GetSceneDistance(OrbitLayout::Body::Sun), 0.0f);
}

TEST_F(OrbitLayoutTest, SceneDistanceStableAcrossAdvance) {
    const float before = OrbitLayout::GetSceneDistance(OrbitLayout::Body::Mars);
    OrbitLayout::Advance(OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Mars) * 0.25f);
    const float after = OrbitLayout::GetSceneDistance(OrbitLayout::Body::Mars);
    EXPECT_TRUE(Near(before, after));
    EXPECT_TRUE(Near(glm::length(OrbitLayout::GetOffset(OrbitLayout::Body::Mars)), after, 1.0f));
}

TEST_F(OrbitLayoutTest, MercuryCompletesOrbitFasterThanEarth) {
    const float mercuryPeriod = OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Mercury);
    const float earthPeriod = OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Earth);
    ASSERT_GT(mercuryPeriod, 0.0f);
    ASSERT_GT(earthPeriod, 0.0f);
    EXPECT_LT(mercuryPeriod, earthPeriod);

    const glm::vec3 mercuryStart = OrbitLayout::GetOffset(OrbitLayout::Body::Mercury);
    const glm::vec3 earthStart = OrbitLayout::GetOffset(OrbitLayout::Body::Earth);
    OrbitLayout::Advance(mercuryPeriod);
    const glm::vec3 mercuryAfter = OrbitLayout::GetOffset(OrbitLayout::Body::Mercury);
    const glm::vec3 earthAfter = OrbitLayout::GetOffset(OrbitLayout::Body::Earth);

    EXPECT_TRUE(Near(mercuryAfter, mercuryStart, 5.0f));
    EXPECT_GT(glm::length(earthAfter - earthStart), 50.0f);
}

TEST_F(OrbitLayoutTest, SetJulianDateMovesPlanets) {
    const glm::vec3 earthJ2000 = OrbitLayout::GetOffset(OrbitLayout::Body::Earth);
    OrbitLayout::SetJulianDate(Ephemeris::kJ2000 + 90.0);
    const glm::vec3 earthPlus90 = OrbitLayout::GetOffset(OrbitLayout::Body::Earth);
    EXPECT_GT(glm::length(earthPlus90 - earthJ2000), 100.0f);
    EXPECT_NEAR(OrbitLayout::GetJulianDate(), Ephemeris::kJ2000 + 90.0, 1e-9);
}

TEST_F(OrbitLayoutTest, RelativeAngularSpeedOrdering) {
    const float mercury = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Mercury);
    const float venus = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Venus);
    const float earth = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Earth);
    const float mars = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Mars);
    const float jupiter = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Jupiter);
    const float saturn = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Saturn);
    const float uranus = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Uranus);
    const float neptune = 1.0f / OrbitLayout::GetOrbitPeriodSecondsAt1x(OrbitLayout::Body::Neptune);

    EXPECT_GT(mercury, venus);
    EXPECT_GT(venus, earth);
    EXPECT_GT(earth, mars);
    EXPECT_GT(mars, jupiter);
    EXPECT_GT(jupiter, saturn);
    EXPECT_GT(saturn, uranus);
    EXPECT_GT(uranus, neptune);
}

TEST_F(OrbitLayoutTest, AxialSpinIsLinearAndRetrogradeForVenus) {
    EXPECT_LT(OrbitLayout::GetSiderealRotationDays(OrbitLayout::Body::Venus), 0.0f);
    EXPECT_LT(OrbitLayout::GetSiderealRotationDays(OrbitLayout::Body::Uranus), 0.0f);

    OrbitLayout::Advance(1.0f);
    const float venusSpin = OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Venus);
    const float earthSpin = OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Earth);
    EXPECT_LT(venusSpin, 0.0f);
    EXPECT_GT(earthSpin, 0.0f);
}

TEST_F(OrbitLayoutTest, AdvanceUpdatesJulianDate) {
    const double before = OrbitLayout::GetJulianDate();
    // At 1x, 120 wall seconds = 365.25 sim days.
    OrbitLayout::Advance(OrbitLayout::kEarthOrbitSecondsAt1x);
    EXPECT_NEAR(OrbitLayout::GetJulianDate(), before + 365.25, 1e-4);
}
