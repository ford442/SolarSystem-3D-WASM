#include <gtest/gtest.h>

#include <cmath>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Solar_System/AsteroidOrbit.h"
#include "Solar_System/OrbitLayout.h"

namespace {

bool Near(float a, float b, float eps = 0.05f) {
    return std::fabs(a - b) <= eps;
}

} // namespace

class AsteroidOrbitTest : public ::testing::Test {
protected:
    void SetUp() override {
        OrbitLayout::ResetForTests();
    }
};

TEST_F(AsteroidOrbitTest, GenerateBeltIsDeterministic) {
    const auto a = AsteroidOrbit::GenerateBelt(AsteroidOrbit::kDefaultSeed, 128);
    const auto b = AsteroidOrbit::GenerateBelt(AsteroidOrbit::kDefaultSeed, 128);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i].semiMajorAu, b[i].semiMajorAu);
        EXPECT_FLOAT_EQ(a[i].meanAnomalyRad, b[i].meanAnomalyRad);
        EXPECT_FLOAT_EQ(a[i].visualRadius, b[i].visualRadius);
    }
}

TEST_F(AsteroidOrbitTest, GenerateBeltRespectsMainBeltAuRange) {
    const auto belt = AsteroidOrbit::GenerateBelt(42u, 500);
    ASSERT_EQ(belt.size(), 500u);
    for (const auto& el : belt) {
        EXPECT_GE(el.semiMajorAu, OrbitLayout::kMainBeltInnerAu - 1e-4f);
        EXPECT_LE(el.semiMajorAu, OrbitLayout::kMainBeltOuterAu + 1e-4f);
        EXPECT_GE(el.eccentricity, 0.0f);
        EXPECT_LT(el.eccentricity, 0.25f);
        EXPECT_GT(el.meanMotionRadPerSec, 0.0f);
    }
}

TEST_F(AsteroidOrbitTest, GenerateBeltClampsToMaxInstances) {
    const auto belt = AsteroidOrbit::GenerateBelt(1u, AsteroidOrbit::kMaxInstances + 500);
    EXPECT_EQ(belt.size(), static_cast<size_t>(AsteroidOrbit::kMaxInstances));
}

TEST_F(AsteroidOrbitTest, RealisticPositionsSitBetweenMarsAndJupiter) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    const float marsR = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Mars);
    const float jupiterR = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter);

    const auto belt = AsteroidOrbit::GenerateBelt(7u, 64);
    for (const auto& el : belt) {
        // Near-circular approximation: scene radius ≈ a mapped through AuToSceneDistance.
        const float sceneA = OrbitLayout::AuToSceneDistance(el.semiMajorAu);
        EXPECT_GT(sceneA, marsR);
        EXPECT_LT(sceneA, jupiterR);

        const glm::vec3 pos = AsteroidOrbit::HeliocentricPosition(el);
        const float r = glm::length(pos);
        // Allow eccentricity to pull slightly inside/outside the a-band.
        EXPECT_GT(r, marsR * 0.75f);
        EXPECT_LT(r, jupiterR * 1.15f);
    }
}

TEST_F(AsteroidOrbitTest, CompressedBeltFitsMarsJupiterArtGap) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Compressed);
    const float marsR = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Mars);
    const float jupiterR = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter);

    const float inner = OrbitLayout::AuToSceneDistance(OrbitLayout::kMainBeltInnerAu);
    const float outer = OrbitLayout::AuToSceneDistance(OrbitLayout::kMainBeltOuterAu);
    EXPECT_GT(inner, marsR);
    EXPECT_LT(outer, jupiterR);
    EXPECT_GT(outer, inner);
}

TEST_F(AsteroidOrbitTest, NamedCometsHaveHighEccentricity) {
    const auto comets = AsteroidOrbit::GenerateComets();
    ASSERT_EQ(comets.size(), static_cast<size_t>(AsteroidOrbit::kCometCount));
    for (const auto& c : comets) {
        EXPECT_GT(c.eccentricity, 0.7f);
        EXPECT_GT(c.semiMajorAu, 0.0f);
        EXPECT_GT(c.meanMotionRadPerSec, 0.0f);
        const float peri = c.semiMajorAu * (1.0f - c.eccentricity);
        EXPECT_LT(peri, 1.5f);
    }
}

TEST_F(AsteroidOrbitTest, KeplerCircularOrbitRadiusMatchesSemiMajor) {
    AsteroidOrbit::OrbitalElements el;
    el.semiMajorAu = 2.5f;
    el.eccentricity = 0.0f;
    el.meanAnomalyRad = 1.234f;
    EXPECT_TRUE(Near(AsteroidOrbit::CurrentRadiusAu(el), 2.5f, 1e-4f));
}

TEST_F(AsteroidOrbitTest, KuiperDiskIsDistinctFromTheMainBelt) {
    const auto disk = AsteroidOrbit::GenerateKuiperDisk(AsteroidOrbit::kDefaultSeed, 400);
    ASSERT_EQ(disk.size(), 400u);
    int plutinos = 0;
    for (const auto& el : disk) {
        EXPECT_GE(el.semiMajorAu, OrbitLayout::kKuiperBeltInnerAu);
        EXPECT_LE(el.semiMajorAu, OrbitLayout::kKuiperBeltOuterAu);
        EXPECT_GT(el.semiMajorAu, OrbitLayout::kMainBeltOuterAu * 5.0f);
        EXPECT_LT(el.eccentricity, 0.26f);
        EXPECT_EQ(el.ephemerisBody, -1);
        plutinos += (el.semiMajorAu > 39.0f && el.semiMajorAu < 39.8f) ? 1 : 0;
    }
    EXPECT_GT(plutinos, 40); // ~1/4 sit in the 3:2 resonance
}

TEST_F(AsteroidOrbitTest, HalleyTailFollowsTheCatalogNucleus) {
    const auto comets = AsteroidOrbit::GenerateComets();
    ASSERT_FALSE(comets.empty());
    const auto& halley = comets.front();
    ASSERT_EQ(halley.ephemerisBody, static_cast<int>(OrbitLayout::Body::Halley));
    OrbitLayout::SetJulianDate(Ephemeris::JulianDateFromYmd(1986, 2, 9));
    const glm::vec3 tail = AsteroidOrbit::HeliocentricPosition(halley);
    const glm::vec3 nucleus = OrbitLayout::GetOffset(OrbitLayout::Body::Halley);
    EXPECT_LT(glm::length(tail - nucleus), 1e-3f);
    EXPECT_NEAR(AsteroidOrbit::CurrentRadiusAu(halley), 0.586f, 0.01f);
}
