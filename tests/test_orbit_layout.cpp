#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/Observer.h"
#include "Auxiliary_Modules/SkyEvents.h"
#include "Solar_System/BodyCatalog.generated.h"
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
    EXPECT_EQ(OrbitLayout::BodyFromName("Sedna"), OrbitLayout::Body::Sun);
    EXPECT_EQ(OrbitLayout::BodyFromName("ceres"), OrbitLayout::Body::Sun); // manifest names are capitalised
    EXPECT_EQ(OrbitLayout::BodyFromName(""), OrbitLayout::Body::Sun);
}

TEST_F(OrbitLayoutTest, BodyFromNameResolvesCatalogDwarfs) {
    // PlanetSystemLoader::RefreshPlanetProxyPositions maps manifest system names this way,
    // so a miss here silently parks the proxy marker on the Sun.
    EXPECT_EQ(OrbitLayout::BodyFromName("Ceres"), OrbitLayout::Body::Ceres);
    EXPECT_EQ(OrbitLayout::BodyFromName("Vesta"), OrbitLayout::Body::Vesta);
}

TEST_F(OrbitLayoutTest, BodyFromNameResolvesOuterSystem) {
    EXPECT_EQ(OrbitLayout::BodyFromName("Eris"), OrbitLayout::Body::Eris);
    EXPECT_EQ(OrbitLayout::BodyFromName("Haumea"), OrbitLayout::Body::Haumea);
    EXPECT_EQ(OrbitLayout::BodyFromName("Makemake"), OrbitLayout::Body::Makemake);
    EXPECT_EQ(OrbitLayout::BodyFromName("Halley"), OrbitLayout::Body::Halley);
    // Focus indices 0–11 are ABI; the outer system appends after them.
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Vesta), 11);
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Eris), 12);
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Halley), 15);
    EXPECT_EQ(OrbitLayout::kBodyCount, 16);
}

TEST_F(OrbitLayoutTest, HalleyFollowsItsEllipseInBothScaleModes) {
    const double perihelion1986 = Ephemeris::JulianDateFromYmd(1986, 2, 9);
    const double aphelion2023 = Ephemeris::JulianDateFromYmd(2023, 12, 9);
    for (const auto mode : {OrbitLayout::ScaleMode::Compressed, OrbitLayout::ScaleMode::Realistic}) {
        OrbitLayout::SetScaleMode(mode);
        OrbitLayout::SetJulianDate(perihelion1986);
        const float nearSun = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Halley);
        EXPECT_LT(nearSun, OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Earth));
        EXPECT_NEAR(glm::length(OrbitLayout::GetOffset(OrbitLayout::Body::Halley)), nearSun, 0.01f * nearSun);

        OrbitLayout::SetJulianDate(aphelion2023);
        EXPECT_GT(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Halley),
                  OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Neptune));
    }
}

TEST_F(OrbitLayoutTest, OuterDwarfsSitBeyondNeptuneInRealisticScale) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    OrbitLayout::SetJulianDate(Ephemeris::JulianDateFromYmd(2026, 1, 1));
    const float neptune = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Neptune);
    for (const auto body : {OrbitLayout::Body::Eris, OrbitLayout::Body::Haumea, OrbitLayout::Body::Makemake}) {
        EXPECT_GT(OrbitLayout::GetOrbitRadius(body), neptune) << static_cast<int>(body);
    }
    EXPECT_GT(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Eris),
              OrbitLayout::AuToSceneDistance(OrbitLayout::kKuiperBeltOuterAu));
}

TEST_F(OrbitLayoutTest, BeltDwarfsOrbitBetweenMarsAndJupiter) {
    const float mars = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Mars);
    const float jupiter = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter);
    for (const auto body : {OrbitLayout::Body::Ceres, OrbitLayout::Body::Vesta}) {
        const float radius = OrbitLayout::GetOrbitRadius(body);
        EXPECT_GT(radius, mars);
        EXPECT_LT(radius, jupiter);
        // Compressed art radius must agree with the AU→scene map the asteroid belt uses,
        // or the dwarfs would float off the belt they belong to.
        EXPECT_NEAR(radius, OrbitLayout::AuToSceneDistance(OrbitLayout::GetAuDistance(body)), 0.5f);
    }
}

TEST_F(OrbitLayoutTest, BeltDwarfsSpinFasterThanEarth) {
    // Ceres ~9.1 h, Vesta ~5.3 h; a zero here means the catalog row lost its rotation period.
    EXPECT_LT(OrbitLayout::GetSiderealRotationDays(OrbitLayout::Body::Ceres), 0.5f);
    EXPECT_GT(OrbitLayout::GetSiderealRotationDays(OrbitLayout::Body::Ceres), 0.0f);
    EXPECT_LT(OrbitLayout::GetSiderealRotationDays(OrbitLayout::Body::Vesta),
              OrbitLayout::GetSiderealRotationDays(OrbitLayout::Body::Ceres));
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

    const float expectedJupiterRadius = 5.203f * OrbitLayout::kAuToSceneUnits;
    EXPECT_TRUE(Near(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter), expectedJupiterRadius));
    EXPECT_TRUE(Near(glm::length(OrbitLayout::GetOffset(OrbitLayout::Body::Jupiter)), expectedJupiterRadius, 1.0f));
    EXPECT_GT(OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter),
              glm::length(OrbitLayout::GetCompressedOffset(OrbitLayout::Body::Jupiter)));
}

TEST_F(OrbitLayoutTest, AuToSceneDistanceRealisticIsLinear) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    EXPECT_TRUE(Near(OrbitLayout::AuToSceneDistance(1.0f), OrbitLayout::kAuToSceneUnits));
    EXPECT_TRUE(Near(OrbitLayout::AuToSceneDistance(2.5f), 2.5f * OrbitLayout::kAuToSceneUnits));
}

TEST_F(OrbitLayoutTest, HelioAuToScenePreservesDirection) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    const glm::vec3 au(3.0f, 0.0f, 4.0f);
    const glm::vec3 scene = OrbitLayout::HelioAuToScene(au);
    EXPECT_NEAR(glm::length(scene), 5.0f * OrbitLayout::kAuToSceneUnits, 0.01f);
    EXPECT_NEAR(scene.x / scene.z, 3.0f / 4.0f, 1.0e-4f);
    EXPECT_TRUE(Near(OrbitLayout::HelioAuToScene(glm::vec3(0.0f)), glm::vec3(0.0f)));
}

TEST_F(OrbitLayoutTest, AuToSceneDistanceCompressedPlacesBeltBetweenMarsAndJupiter) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Compressed);
    const float marsR = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Mars);
    const float jupiterR = OrbitLayout::GetOrbitRadius(OrbitLayout::Body::Jupiter);
    const float beltInner = OrbitLayout::AuToSceneDistance(OrbitLayout::kMainBeltInnerAu);
    const float beltOuter = OrbitLayout::AuToSceneDistance(OrbitLayout::kMainBeltOuterAu);
    EXPECT_GT(beltInner, marsR);
    EXPECT_LT(beltOuter, jupiterR);
    EXPECT_GT(beltOuter, beltInner);
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

    // Spins are wrapped to [0, 360), so compare a step small enough not to wrap.
    const float venusBefore = OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Venus);
    const float marsBefore = OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Mars);
    OrbitLayout::SetJulianDate(OrbitLayout::GetJulianDate() + 1.0);
    const float venusStep = OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Venus) - venusBefore;
    const float marsStep = OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Mars) - marsBefore;
    EXPECT_NEAR(venusStep, 360.0f / -243.025f, 1e-3f);
    EXPECT_NEAR(std::remainder(marsStep, 360.0f), std::remainder(360.0f / 1.025957f, 360.0f), 1e-3f);
}

TEST_F(OrbitLayoutTest, AdvanceUpdatesJulianDate) {
    const double before = OrbitLayout::GetJulianDate();
    // At 1x, 120 wall seconds = 365.25 sim days.
    OrbitLayout::Advance(OrbitLayout::kEarthOrbitSecondsAt1x);
    EXPECT_NEAR(OrbitLayout::GetJulianDate(), before + 365.25, 1e-4);
}

namespace {

struct LonLat {
    double lonDeg;
    double latDeg;
};

/**
 * Where the Sun is overhead on Earth's *drawn* texture: CatalogBody::AdjustToParent's
 * Rz(axialTilt) [Rx(artTiltX)] Ry(spin) chain (scale does not change directions), and the
 * Blender sphere.obj UVs, whose map longitude is -atan2(z, x) in model space (u = 0.5 on
 * +X; Greenwich-centred Earth maps). Keep in step with CatalogBody.cpp.
 */
LonLat RenderedSubSolarPoint() {
    const BodyCatalog::Entry* earth = BodyCatalog::FindById("earth");
    glm::mat4 model(1.0f);
    model = glm::rotate(model, glm::radians(earth->axialTiltDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    if (earth->artTiltXDegrees != 0.0f) {
        model = glm::rotate(model, glm::radians(earth->artTiltXDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    model = glm::rotate(model, glm::radians(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Earth)),
                        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::dvec3 toSun = glm::normalize(-OrbitLayout::GetHeliocentricScenePosition(OrbitLayout::Body::Earth));
    const glm::vec3 local = glm::transpose(glm::mat3(model)) * glm::vec3(toSun);
    return {glm::degrees(-std::atan2(static_cast<double>(local.z), static_cast<double>(local.x))),
            glm::degrees(std::asin(static_cast<double>(local.y)))};
}

/** The real sub-solar point: Sun direction in the Earth-fixed frame via Observer (GMST). */
LonLat TrueSubSolarPoint(double jd) {
    double sunEcl[3];
    SkyEvents::GeocentricEclipticKm(0, jd, sunEcl);
    double h[3];
    Observer::HorizonFromEclipticJ2000(jd, Observer::Site{}).Apply(sunEcl, h); // E, Up, -N at 0/0
    const double len = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    return {glm::degrees(std::atan2(h[0], h[1])), glm::degrees(std::asin(-h[2] / len))};
}

double AngleDiffDeg(double a, double b) {
    return std::remainder(a - b, 360.0);
}

} // namespace

TEST_F(OrbitLayoutTest, EarthTextureShowsTheTrueSubSolarLongitudeAllYear) {
    // A constant prime-meridian offset on GMST could not do this: the scene is a mirror of
    // the ecliptic, so the old spin's residual swung by ~180 deg over a year.
    for (const auto mode : {OrbitLayout::ScaleMode::Compressed, OrbitLayout::ScaleMode::Realistic}) {
        OrbitLayout::SetScaleMode(mode);
        for (int step = 0; step < 8; ++step) {
            const double jd = Ephemeris::JulianDateFromYmd(2024, 1, 1) + 45.7 * step + 0.13 * step;
            OrbitLayout::SetJulianDate(jd);
            const LonLat drawn = RenderedSubSolarPoint();
            const LonLat truth = TrueSubSolarPoint(jd);
            EXPECT_NEAR(AngleDiffDeg(drawn.lonDeg, truth.lonDeg), 0.0, 0.5) << "jd " << jd;
        }
    }
}

TEST_F(OrbitLayoutTest, EarthSubSolarLongitudeMatchesAlmanacAtNoonUtc) {
    // Independent of Observer: at 12:00 UTC the Sun is over longitude -EoT * 0.25 deg/min
    // (apparent minus mean solar time). Almanac equation of time: 2025-02-11 -14.2 min,
    // 2025-11-03 +16.4 min. Zeroing or breaking the calibration lands tens of degrees off.
    struct Anchor { int y, m, d; double eotMinutes; };
    for (const Anchor& anchor : {Anchor{2025, 2, 11, -14.2}, Anchor{2025, 11, 3, 16.4}}) {
        OrbitLayout::SetJulianDate(Ephemeris::JulianDateFromYmd(anchor.y, anchor.m, anchor.d) + 0.5);
        EXPECT_NEAR(AngleDiffDeg(RenderedSubSolarPoint().lonDeg, -anchor.eotMinutes * 0.25), 0.0, 0.5)
            << anchor.y << "-" << anchor.m << "-" << anchor.d;
    }
}

TEST_F(OrbitLayoutTest, EverySpinIsAFunctionOfTheDate) {
    // Scrub forward with Advance (the frame loop's path) and jump back: nothing accumulates,
    // so every body, Jupiter included, is exactly where it was.
    const double epoch = Ephemeris::JulianDateFromYmd(2017, 8, 21) + 0.77;
    OrbitLayout::SetJulianDate(epoch);
    float before[OrbitLayout::kBodyCount];
    for (int i = 0; i < OrbitLayout::kBodyCount; ++i) {
        before[i] = OrbitLayout::GetAxialSpinDegrees(static_cast<OrbitLayout::Body>(i));
    }
    for (int frame = 0; frame < 500; ++frame) {
        OrbitLayout::Advance(0.37f);
    }
    EXPECT_NE(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Jupiter), before[static_cast<int>(OrbitLayout::Body::Jupiter)]);
    OrbitLayout::SetJulianDate(epoch);
    for (int i = 0; i < OrbitLayout::kBodyCount; ++i) {
        EXPECT_FLOAT_EQ(OrbitLayout::GetAxialSpinDegrees(static_cast<OrbitLayout::Body>(i)), before[i]) << "body " << i;
    }
}

TEST_F(OrbitLayoutTest, SpinsStartFromTheCatalogPrimeMeridianAtJ2000) {
    OrbitLayout::SetJulianDate(Ephemeris::kJ2000);
    EXPECT_NEAR(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Jupiter), 284.95f, 1e-3f); // IAU System III W0
    EXPECT_NEAR(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Mars), 176.049863f, 1e-3f);
}
