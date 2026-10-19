// The giant planets' LUT atmospheres: their catalog rows, the oblate figure the surface,
// cloud and atmosphere meshes share (BodyShape.h), and the ring-shadow geometry that
// atmospherePbr.fs evaluates per view-ray sample on Saturn and Uranus.

#include "Solar_System/AtmosphereModel.h"
#include "Solar_System/BodyShape.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SystemVisuals.h"

#include <gtest/gtest.h>

#include <cmath>

namespace {

constexpr const char* kGiants[] = {"jupiter", "saturn", "uranus", "neptune"};

// Julian dates of the documented ring epochs (docs/ARCHITECTURE.md §9.2). The scene's ring
// planes come from the catalog tilt plus Saturn's art tilt, so these are the scene's own
// solstices and equinoxes, not the real ones.
constexpr double kJ2000 = 2451545.0;
constexpr double kSaturnRingsOpen = kJ2000 + 8.0 * 365.25;      // 2008-01-01: B ~ +29 deg
constexpr double kSaturnRingsEdgeOn = kJ2000 + 30.25 * 365.25;  // 2030-04: B ~ 0
constexpr double kUranusRingsOpen = kJ2000 + 24.0 * 365.25;     // 2024-01-01: B ~ -38 deg
constexpr double kUranusRingsEdgeOn = kJ2000 + 33.15 * 365.25;  // 2033-02: B ~ 0

void ExpectNear(const glm::mat3& a, const glm::mat3& b, float epsilon, const char* what) {
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            EXPECT_NEAR(a[c][r], b[c][r], epsilon) << what << " [" << c << "][" << r << "]";
        }
    }
}

TEST(BodyShape, GiantsAreFlattenedAndEverythingElseIsASphere) {
    EXPECT_NEAR(BodyCatalog::FindById("jupiter")->oblateness, 0.0649f, 1e-3f);
    EXPECT_NEAR(BodyCatalog::FindById("saturn")->oblateness, 0.0980f, 1e-3f);
    EXPECT_GT(BodyCatalog::FindById("uranus")->oblateness, 0.0f);
    EXPECT_GT(BodyCatalog::FindById("neptune")->oblateness, 0.0f);
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        if (entry.kind == BodyCatalog::Kind::Satellite) {
            EXPECT_EQ(entry.oblateness, 0.0f) << entry.id << ": satellites are spheres";
        }
    }
    EXPECT_FALSE(BodyShape::IsOblate(*BodyCatalog::FindById("earth")));
}

TEST(BodyShape, SphereFrameRoundTripsAndIsIdentityForASphere) {
    for (const char* id : kGiants) {
        const BodyCatalog::Entry& entry = *BodyCatalog::FindById(id);
        ExpectNear(BodyShape::BodyToSphere(entry) * BodyShape::SphereToBody(entry), glm::mat3(1.0f), 1e-5f, id);
    }
    // Earth is tilted but round: the tilt cancels.
    const BodyCatalog::Entry& earth = *BodyCatalog::FindById("earth");
    ExpectNear(BodyShape::BodyToSphere(earth), glm::mat3(1.0f), 1e-6f, "earth");
}

TEST(BodyShape, OblateSurfaceMapsOntoTheEquatorialSphere) {
    for (const char* id : kGiants) {
        const BodyCatalog::Entry& entry = *BodyCatalog::FindById(id);
        const glm::mat3 frame = BodyShape::PoleFrame(entry);
        const glm::mat3 toSphere = BodyShape::BodyToSphere(entry);
        const float equatorial = 2.0f * entry.earthRadiusScale;  // sphere.obj radius 2
        const float polar = equatorial * (1.0f - entry.oblateness);

        // The mesh's pole (what CatalogBody draws) lands on the sphere the LUTs assume...
        EXPECT_NEAR(glm::length(toSphere * (frame * glm::vec3(0.0f, polar, 0.0f))), equatorial, 1e-4f) << id;
        // ...the equator is left alone...
        EXPECT_NEAR(glm::length(toSphere * (frame * glm::vec3(equatorial, 0.0f, 0.0f))), equatorial, 1e-4f) << id;
        // ...and so is any point at mid-latitude on the ellipsoid.
        const float c = std::cos(glm::radians(40.0f));
        const float s = std::sin(glm::radians(40.0f));
        const glm::vec3 midLatitude = frame * glm::vec3(equatorial * c, polar * s, 0.0f);
        EXPECT_NEAR(glm::length(toSphere * midLatitude), equatorial, 1e-4f) << id;
    }
}

TEST(GiantAtmospheres, AllFourGiantsCarryAPhysicalRowOverTheirOneilShell) {
    for (const char* id : kGiants) {
        const BodyCatalog::AtmosphereRow* row = BodyCatalog::FindAtmosphere(id);
        ASSERT_NE(row, nullptr) << id;
        EXPECT_TRUE(row->physical.enabled) << id << ": Medium/Full take the LUT path";
        // The O'Neil block stays: it is the Low preset and the fallback when a LUT fails.
        EXPECT_GT(row->oneil.shellScale, 0.0f) << id;
        EXPECT_GT(row->oneil.outerRadius, 0.0f) << id;
    }
}

TEST(GiantAtmospheres, ShellIsVisibleAtOrreryScale) {
    for (const char* id : kGiants) {
        const AtmosphereModel::Params params = AtmosphereModel::FromCatalog(BodyCatalog::FindAtmosphere(id)->physical);
        const double relative = (params.topRadius - params.bottomRadius) / params.bottomRadius;
        // A real giant's air is ~0.5% of its radius; thicknessScale widens it to a limb a
        // visitor can see without turning it into a halo.
        EXPECT_GT(relative, 0.015) << id;
        EXPECT_LT(relative, 0.04) << id;
    }
}

TEST(GiantAtmospheres, IceGiantLimbIsPaleNotRayleighBlue) {
    // A pure Rayleigh sky scatters blue over red by lambda^-4 (Earth's row: ~5.7x). The
    // ice giants' limb is the pale cyan of methane-tinted haze: the haze scatters green and
    // blue alike, and methane absorbs red, so the scattering ratio stays well below that.
    const BodyCatalog::AtmospherePhysical& earth = BodyCatalog::FindAtmosphere("earth")->physical;
    const double rayleighRatio = earth.rayleighScattering.b / earth.rayleighScattering.r;
    for (const char* id : {"uranus", "neptune"}) {
        const AtmosphereModel::Params params = AtmosphereModel::FromCatalog(BodyCatalog::FindAtmosphere(id)->physical);
        const AtmosphereModel::Medium deck = AtmosphereModel::SampleMedium(params, 0.0);
        EXPECT_LT(deck.scattering.z / deck.scattering.x, 0.7 * rayleighRatio) << id;
        // Single-scattering albedo: red is absorbed, green and blue survive.
        const glm::dvec3 albedo = deck.scattering / deck.extinction;
        EXPECT_LT(albedo.x, albedo.y) << id;
        EXPECT_GT(albedo.y, 0.8) << id;
        EXPECT_GT(albedo.z, 0.8) << id;
    }
    // Jupiter's and Saturn's chromophore haze does the opposite: it absorbs blue.
    for (const char* id : {"jupiter", "saturn"}) {
        const BodyCatalog::AtmospherePhysical& p = BodyCatalog::FindAtmosphere(id)->physical;
        EXPECT_GT(p.mieScattering.r / p.mieExtinction.r, p.mieScattering.b / p.mieExtinction.b) << id;
    }
}

// ---------------------------------------------------------------------------------------
// Ring shadow on the air. A CPU mirror of RingVisibilityAt in atmospherePbr.fs (and
// RingCrossing / intersectDisk in common/ring.glsl, common/raytrace.glsl).

struct RingShadow {
    float sunElevationDegrees = 0.0f;  // the ring opening angle B toward the Sun
    int litSamples = 0;
    int shadowedSamples = 0;
    int shadowedOnSunSide = 0;  // must stay 0: the ring shades the hemisphere facing away
};

bool CrossesRing(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& ringNormal, float inner, float outer) {
    const glm::vec3 n = glm::dot(ringNormal, dir) < 0.0f ? -ringNormal : ringNormal;
    const float denom = glm::dot(n, dir);
    if (denom <= 1e-6f) {
        return false;
    }
    const float t = glm::dot(-origin, n) / denom;  // ring centred on the planet
    if (t < 0.0f) {
        return false;
    }
    const float distance = glm::length(origin + dir * t);
    return distance > inner && distance <= outer;
}

// Samples the top of the cloud deck (the shell's ground) on the oblate figure and tests the
// sun ray from each lit point against the ring annulus.
RingShadow SampleRingShadow(const char* id, double julianDate) {
    const BodyCatalog::Entry& entry = *BodyCatalog::FindById(id);
    const SystemVisuals::RingSpec& ring = *SystemVisuals::FindRing(id);
    OrbitLayout::SetJulianDate(julianDate);
    const glm::vec3 toSun = -glm::normalize(OrbitLayout::GetOffset(static_cast<OrbitLayout::Body>(entry.index)));
    const glm::vec3 pole = BodyShape::PoleFrame(entry) * glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::mat3 toBody = BodyShape::SphereToBody(entry);
    const float radius = 2.0f * entry.earthRadiusScale * 1.005f;  // just above the deck

    RingShadow result;
    result.sunElevationDegrees = glm::degrees(std::asin(glm::dot(pole, toSun)));
    constexpr int kSteps = 96;
    for (int i = 0; i < kSteps; ++i) {
        const float z = 1.0f - 2.0f * (i + 0.5f) / kSteps;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        for (int j = 0; j < 2 * kSteps; ++j) {
            const float phi = glm::two_pi<float>() * (j + 0.5f) / (2 * kSteps);
            const glm::vec3 sphereNormal(r * std::cos(phi), z, r * std::sin(phi));
            const glm::vec3 point = toBody * (sphereNormal * radius);
            if (glm::dot(glm::transpose(toBody) * sphereNormal, toSun) <= 0.0f) {
                continue;  // night side: the planet's own shadow, handled by the LUT
            }
            ++result.litSamples;
            if (CrossesRing(point, toSun, pole, ring.innerRadius, ring.outerRadius)) {
                ++result.shadowedSamples;
                if (glm::dot(point, pole) * glm::dot(toSun, pole) > 0.0f) {
                    ++result.shadowedOnSunSide;
                }
            }
        }
    }
    return result;
}

class GiantRingShadow : public ::testing::Test {
protected:
    void SetUp() override { OrbitLayout::ResetForTests(); }
    void TearDown() override { OrbitLayout::ResetForTests(); }
};

TEST_F(GiantRingShadow, SaturnRingsDarkenTheShadowedSideWhenOpen) {
    const RingShadow open = SampleRingShadow("saturn", kSaturnRingsOpen);
    EXPECT_GT(std::abs(open.sunElevationDegrees), 25.0f);
    ASSERT_GT(open.litSamples, 0);
    // A broad band across the lit disc, entirely on the hemisphere away from the Sun.
    EXPECT_GT(static_cast<float>(open.shadowedSamples) / open.litSamples, 0.05f);
    EXPECT_EQ(open.shadowedOnSunSide, 0);

    const RingShadow edgeOn = SampleRingShadow("saturn", kSaturnRingsEdgeOn);
    EXPECT_LT(std::abs(edgeOn.sunElevationDegrees), 2.0f);
    EXPECT_LT(static_cast<float>(edgeOn.shadowedSamples) / edgeOn.litSamples,
              0.25f * static_cast<float>(open.shadowedSamples) / open.litSamples)
        << "edge-on rings cast only a thin line";
}

TEST_F(GiantRingShadow, UranusRingsDarkenTheShadowedSideWhenOpen) {
    const RingShadow open = SampleRingShadow("uranus", kUranusRingsOpen);
    EXPECT_GT(std::abs(open.sunElevationDegrees), 30.0f);
    ASSERT_GT(open.litSamples, 0);
    // Uranus's rings are narrow, so the band is too.
    EXPECT_GT(open.shadowedSamples, 0);
    EXPECT_EQ(open.shadowedOnSunSide, 0);

    const RingShadow edgeOn = SampleRingShadow("uranus", kUranusRingsEdgeOn);
    EXPECT_LT(std::abs(edgeOn.sunElevationDegrees), 2.0f);
    EXPECT_LT(edgeOn.shadowedSamples, open.shadowedSamples);
}

TEST_F(GiantRingShadow, OpeningAngleFollowsTheDate) {
    // The ring normal is fixed in the scene; the Sun moves around it with the orbit. Over
    // half a Saturn year the opening angle swings from one side of the rings to the other.
    const float early = SampleRingShadow("saturn", kSaturnRingsOpen).sunElevationDegrees;
    const float later = SampleRingShadow("saturn", kSaturnRingsOpen + 0.5 * 10759.0).sunElevationDegrees;
    EXPECT_LT(early * later, 0.0f);
}

} // namespace
