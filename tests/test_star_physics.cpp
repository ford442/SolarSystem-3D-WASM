#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "astro/StarPhysics.h"
#include "Solar_System/BodyCatalog.h"

namespace {

// The glow tint StarSystemFactory has always given the Sun (StarInfo::glowTintMult).
constexpr float kSolGlowTint[3] = {0.99607843f, 0.890196078f, 0.725490196f};

} // namespace

TEST(StarPhysicsTest, SunTemperatureGivesTheCurrentSunColourWithinOnePercent) {
    // The lighting tint the engine has always used for the Sun is white: lit shaders multiply
    // by RelativeTint, so 5772 K must be (1, 1, 1) — exactly, which keeps Sol byte-identical.
    const glm::vec3 tint = StarPhysics::RelativeTint(StarPhysics::kSunEffectiveTemperatureK);
    EXPECT_EQ(tint.x, 1.0f);
    EXPECT_EQ(tint.y, 1.0f);
    EXPECT_EQ(tint.z, 1.0f);

    // The corona/glow tint is the Sun's art tint times RelativeTint: within 1% at 5772 K, and
    // within 1% for the 5778 K the glow spectrum was tuned at.
    for (const double kelvin : {5772.0, 5778.0}) {
        const glm::vec3 t = StarPhysics::RelativeTint(kelvin);
        for (int c = 0; c < 3; ++c) {
            const float glow = kSolGlowTint[c] * t[c];
            EXPECT_NEAR(glow, kSolGlowTint[c], 0.01f * kSolGlowTint[c]) << kelvin << " K channel " << c;
        }
    }
}

TEST(StarPhysicsTest, SunBlackbodyIsNearWhite) {
    // A 5772 K blackbody in linear sRGB, peak-normalised, is a slightly warm white.
    glm::dvec3 rgb = StarPhysics::BlackbodyLinearRgb(StarPhysics::kSunEffectiveTemperatureK);
    rgb /= std::max({rgb.x, rgb.y, rgb.z});
    EXPECT_DOUBLE_EQ(rgb.x, 1.0);
    EXPECT_GT(rgb.y, 0.85);
    EXPECT_GT(rgb.z, 0.75);
}

TEST(StarPhysicsTest, CoolStarsAreRedAndHotStarsBlue) {
    // TRAPPIST-1 (M8 V, 2566 K): red light — green well below red, blue far below green.
    const glm::vec3 m8 = StarPhysics::RelativeTint(2566.0);
    EXPECT_FLOAT_EQ(m8.x, 1.0f);
    EXPECT_LT(m8.y, 0.75f);
    EXPECT_LT(m8.z, 0.45f);
    EXPECT_LT(m8.z, m8.y);

    // Proxima (2900 K) is between it and the Sun.
    const glm::vec3 m55 = StarPhysics::RelativeTint(2900.0);
    EXPECT_GT(m55.y, m8.y);
    EXPECT_GT(m55.z, m8.z);

    // An A star (9000 K): blue is the peak channel.
    const glm::vec3 a = StarPhysics::RelativeTint(9000.0);
    EXPECT_FLOAT_EQ(a.z, 1.0f);
    EXPECT_LT(a.x, 1.0f);
}

TEST(StarPhysicsTest, TintIsMonotonicInTemperature) {
    float previousBlueOverRed = 0.0f;
    for (double kelvin = 2000.0; kelvin <= 12000.0; kelvin += 250.0) {
        const glm::vec3 t = StarPhysics::RelativeTint(kelvin);
        const float blueOverRed = t.z / t.x;
        EXPECT_GT(blueOverRed, previousBlueOverRed) << kelvin;
        previousBlueOverRed = blueOverRed;
    }
}

TEST(StarPhysicsTest, IrradianceAndEquilibriumTemperature) {
    EXPECT_DOUBLE_EQ(StarPhysics::IrradianceEarthUnits(1.0, 1.0), 1.0);
    EXPECT_DOUBLE_EQ(StarPhysics::IrradianceEarthUnits(1.0, 2.0), 0.25);
    EXPECT_NEAR(StarPhysics::EquilibriumTemperatureK(1.0, 1.0), 278.6, 1e-9);

    // TRAPPIST-1 e: L = 10^-3.257 L☉, a = 0.02925 AU → ≈ 0.65 S⊕, Teq ≈ 250 K (archive: 249.7 K).
    const double l = std::pow(10.0, -3.25727);
    EXPECT_NEAR(StarPhysics::IrradianceEarthUnits(l, 0.02925), 0.646, 0.01);
    EXPECT_NEAR(StarPhysics::EquilibriumTemperatureK(l, 0.02925), 249.7, 2.0);
}

TEST(StarPhysicsTest, LuminosityFromRadiusAndTemperature) {
    EXPECT_DOUBLE_EQ(StarPhysics::LuminosityFromRadius(1.0, StarPhysics::kSunEffectiveTemperatureK), 1.0);
    // TRAPPIST-1: 0.1192 R☉, 2566 K → ≈ 5.5e-4 L☉ (archive log L = -3.257).
    EXPECT_NEAR(std::log10(StarPhysics::LuminosityFromRadius(0.1192, 2566.0)), -3.257, 0.03);
}

TEST(StarPhysicsTest, HabitableZoneBracketsEarth) {
    const StarPhysics::HabitableZone hz = StarPhysics::HabitableZoneAu(1.0);
    EXPECT_LT(hz.innerAu, 1.0);
    EXPECT_GT(hz.outerAu, 1.0);
    // Zero at the window's centre flux, symmetric in log flux about it.
    const double centreAu = 1.0 / std::sqrt(StarPhysics::HabitableZoneCentreFlux());
    EXPECT_NEAR(StarPhysics::HabitabilityDistance(1.0, centreAu), 0.0, 1e-12);
    EXPECT_NEAR(StarPhysics::HabitabilityDistance(1.0, centreAu * std::sqrt(2.0)),
                StarPhysics::HabitabilityDistance(1.0, centreAu / std::sqrt(2.0)), 1e-12);
    // Earth scores better than Venus and Mars.
    EXPECT_LT(StarPhysics::HabitabilityDistance(1.0, 1.0), StarPhysics::HabitabilityDistance(1.0, 0.723));
    EXPECT_LT(StarPhysics::HabitabilityDistance(1.0, 1.0), StarPhysics::HabitabilityDistance(1.0, 1.524));
}

TEST(StarPhysicsTest, AngularRadiusOfTheSunFromEarth) {
    const double rad = StarPhysics::AngularRadiusRad(StarPhysics::kSunRadiusKm, StarPhysics::kKmPerAu);
    EXPECT_NEAR(rad * 180.0 / 3.14159265358979323846 * 60.0, 15.99, 0.02); // arcminutes
    EXPECT_NEAR(StarPhysics::AngularRadiusRad(1.0, 0.5), 1.5707963267948966, 1e-12);
}

TEST(StarPhysicsTest, SolStarRowIsTheIauSun) {
    EXPECT_FLOAT_EQ(BodyCatalog::kSolStar.effectiveTemperatureK, 5772.0f);
    EXPECT_FLOAT_EQ(BodyCatalog::kSolStar.glowTemperatureK, 5778.0f);
    EXPECT_DOUBLE_EQ(BodyCatalog::kSolStar.radiusKm, StarPhysics::kSunRadiusKm);
    EXPECT_FLOAT_EQ(BodyCatalog::kSolStar.luminositySolar, 1.0f);
    EXPECT_FLOAT_EQ(BodyCatalog::kSolStar.activity, 0.0f);
}
