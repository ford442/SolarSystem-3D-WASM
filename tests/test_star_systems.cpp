#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <iterator>
#include <string>

#include <glm/geometric.hpp>

#include "astro/Ephemeris.h"
#include "astro/StarPhysics.h"
#include "core/Json.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SceneScale.h"
#include "Solar_System/StarSystemCatalog.h"

namespace {

std::string RepoPath(const std::string& relative) {
    return std::string(SOLARSYSTEM_SOURCE_DIR) + "/" + relative;
}

std::unique_ptr<StarSystemCatalog::System> Load(const std::string& id) {
    std::string error;
    auto system = StarSystemCatalog::LoadRuntimeJson(RepoPath(StarSystemCatalog::RuntimeJsonPath(id)), error);
    EXPECT_NE(system, nullptr) << id << ": " << error;
    return system;
}

bool SameString(const char* a, const char* b) {
    if (!a || !b) {
        return a == b;
    }
    return std::strcmp(a, b) == 0;
}

bool SameWide(const wchar_t* a, const wchar_t* b) {
    return std::wstring(a ? a : L"") == std::wstring(b ? b : L"");
}

void ExpectSameRgb(const BodyCatalog::Rgb& a, const BodyCatalog::Rgb& b, const std::string& what) {
    EXPECT_FLOAT_EQ(a.r, b.r) << what;
    EXPECT_FLOAT_EQ(a.g, b.g) << what;
    EXPECT_FLOAT_EQ(a.b, b.b) << what;
}

double WrapDeg(double deg) {
    double w = std::fmod(deg, 360.0);
    return w < 0.0 ? w + 360.0 : w;
}

class StarSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        OrbitLayout::ResetForTests();
        StarSystemCatalog::ActivateSol();
    }
    void TearDown() override {
        StarSystemCatalog::ActivateSol();
        OrbitLayout::ResetForTests();
    }
};

} // namespace

// The loader and the compiled table come from the same generator rows (entryRow in
// generate-planet-metadata.mjs). Sol's runtime JSON must therefore read back as exactly the
// compiled kSolEntries: the proof that a system loaded from JSON is drawn from the same data
// a compiled one would be.
TEST_F(StarSystemTest, SolRuntimeJsonMatchesTheCompiledTable) {
    auto sol = Load("sol");
    ASSERT_NE(sol, nullptr);
    ASSERT_EQ(sol->entries.size(), std::size(BodyCatalog::kSolEntries));
    for (std::size_t i = 0; i < sol->entries.size(); ++i) {
        const BodyCatalog::Entry& a = sol->entries[i];
        const BodyCatalog::Entry& b = BodyCatalog::kSolEntries[i];
        const std::string id = b.id;
        EXPECT_EQ(a.index, b.index) << id;
        EXPECT_TRUE(SameString(a.id, b.id)) << id;
        EXPECT_TRUE(SameString(a.parentId, b.parentId)) << id;
        EXPECT_TRUE(SameString(a.initTag, b.initTag)) << id;
        EXPECT_EQ(a.kind, b.kind) << id;
        EXPECT_TRUE(SameWide(a.displayNameEn, b.displayNameEn)) << id;
        EXPECT_TRUE(SameWide(a.displayNameRu, b.displayNameRu)) << id;
        EXPECT_FLOAT_EQ(a.auDistance, b.auDistance) << id;
        EXPECT_FLOAT_EQ(a.orbitalPeriodDays, b.orbitalPeriodDays) << id;
        EXPECT_FLOAT_EQ(a.earthRadiusScale, b.earthRadiusScale) << id;
        EXPECT_FLOAT_EQ(a.radiusKm, b.radiusKm) << id;
        EXPECT_FLOAT_EQ(a.axialTiltDegrees, b.axialTiltDegrees) << id;
        EXPECT_FLOAT_EQ(a.artTiltXDegrees, b.artTiltXDegrees) << id;
        EXPECT_FLOAT_EQ(a.oblateness, b.oblateness) << id;
        EXPECT_FLOAT_EQ(a.yawOffsetDegrees, b.yawOffsetDegrees) << id;
        EXPECT_FLOAT_EQ(a.artRollDegrees, b.artRollDegrees) << id;
        EXPECT_EQ(a.shaderFlags.hasNightTexture, b.shaderFlags.hasNightTexture) << id;
        EXPECT_EQ(a.shaderFlags.hasSpecularMap, b.shaderFlags.hasSpecularMap) << id;
        EXPECT_EQ(a.shaderFlags.hasClouds, b.shaderFlags.hasClouds) << id;
        EXPECT_EQ(a.useSphereIntersect, b.useSphereIntersect) << id;
        EXPECT_FLOAT_EQ(a.ambientFactor, b.ambientFactor) << id;
        EXPECT_TRUE(SameString(a.lod.diffuse, b.lod.diffuse)) << id;
        EXPECT_TRUE(SameString(a.lod.normal, b.lod.normal)) << id;
        EXPECT_TRUE(SameString(a.lod.specular, b.lod.specular)) << id;
        EXPECT_TRUE(SameString(a.lod.night, b.lod.night)) << id;
        EXPECT_TRUE(SameString(a.lod.clouds, b.lod.clouds)) << id;
        EXPECT_TRUE(SameString(a.meshPath, b.meshPath)) << id;
        EXPECT_FLOAT_EQ(a.sceneOrbitRadius, b.sceneOrbitRadius) << id;
        EXPECT_EQ(a.orbitPlane, b.orbitPlane) << id;
        EXPECT_FLOAT_EQ(a.initialAnomalyRad, b.initialAnomalyRad) << id;
        EXPECT_FLOAT_EQ(a.spinDegPerSimSecond, b.spinDegPerSimSecond) << id;
        EXPECT_FLOAT_EQ(a.keplerian.aKm, b.keplerian.aKm) << id;
        EXPECT_FLOAT_EQ(a.keplerian.e, b.keplerian.e) << id;
        EXPECT_FLOAT_EQ(a.keplerian.iDeg, b.keplerian.iDeg) << id;
        EXPECT_FLOAT_EQ(a.keplerian.OmegaDeg, b.keplerian.OmegaDeg) << id;
        EXPECT_FLOAT_EQ(a.keplerian.omegaDeg, b.keplerian.omegaDeg) << id;
        EXPECT_FLOAT_EQ(a.keplerian.M0Deg, b.keplerian.M0Deg) << id;
        EXPECT_FLOAT_EQ(a.keplerian.nDegPerDay, b.keplerian.nDegPerDay) << id;
        EXPECT_FLOAT_EQ(a.keplerian.OmegaDotDegPerDay, b.keplerian.OmegaDotDegPerDay) << id;
        EXPECT_FLOAT_EQ(a.keplerian.omegaDotDegPerDay, b.keplerian.omegaDotDegPerDay) << id;
        EXPECT_TRUE(SameString(a.cloudLayer.diffuse, b.cloudLayer.diffuse)) << id;
        EXPECT_TRUE(SameString(a.cloudLayer.normal, b.cloudLayer.normal)) << id;
        EXPECT_FLOAT_EQ(a.cloudLayer.scaleFactor, b.cloudLayer.scaleFactor) << id;
        EXPECT_FLOAT_EQ(a.cloudLayer.spinDegPerSimSecond, b.cloudLayer.spinDegPerSimSecond) << id;
        EXPECT_FLOAT_EQ(a.cloudLayer.ambientFactor, b.cloudLayer.ambientFactor) << id;
        EXPECT_EQ(a.hasCloudLayer, b.hasCloudLayer) << id;
        EXPECT_EQ(a.hasRings, b.hasRings) << id;
        EXPECT_EQ(a.hasTerrain, b.hasTerrain) << id;
        EXPECT_TRUE(SameString(a.terrainAttribution, b.terrainAttribution)) << id;
        EXPECT_EQ(a.procedural.enabled, b.procedural.enabled) << id;
        EXPECT_EQ(a.placeFromElements, b.placeFromElements) << id;
    }

    ASSERT_EQ(sol->atmospheres.size(), std::size(BodyCatalog::kSolAtmospheres));
    for (std::size_t i = 0; i < sol->atmospheres.size(); ++i) {
        const BodyCatalog::AtmosphereRow& a = sol->atmospheres[i];
        const BodyCatalog::AtmosphereRow& b = BodyCatalog::kSolAtmospheres[i];
        const std::string id = b.bodyId;
        EXPECT_TRUE(SameString(a.bodyId, b.bodyId)) << id;
        EXPECT_FLOAT_EQ(a.oneil.shellScale, b.oneil.shellScale) << id;
        ExpectSameRgb(a.oneil.color, b.oneil.color, id);
        EXPECT_EQ(a.oneil.innerRadiusMinusEpsilon, b.oneil.innerRadiusMinusEpsilon) << id;
        EXPECT_FLOAT_EQ(a.oneil.outerRadius, b.oneil.outerRadius) << id;
        ExpectSameRgb(a.oneil.mieTint, b.oneil.mieTint, id);
        EXPECT_FLOAT_EQ(a.oneil.hScaleFactor, b.oneil.hScaleFactor) << id;
        EXPECT_EQ(a.oneil.toneMapping, b.oneil.toneMapping) << id;
        EXPECT_EQ(a.physical.enabled, b.physical.enabled) << id;
        EXPECT_FLOAT_EQ(a.physical.groundRadiusKm, b.physical.groundRadiusKm) << id;
        EXPECT_FLOAT_EQ(a.physical.topRadiusKm, b.physical.topRadiusKm) << id;
        EXPECT_FLOAT_EQ(a.physical.thicknessScale, b.physical.thicknessScale) << id;
        ExpectSameRgb(a.physical.rayleighScattering, b.physical.rayleighScattering, id);
        EXPECT_FLOAT_EQ(a.physical.rayleighScaleHeightKm, b.physical.rayleighScaleHeightKm) << id;
        ExpectSameRgb(a.physical.mieScattering, b.physical.mieScattering, id);
        ExpectSameRgb(a.physical.mieExtinction, b.physical.mieExtinction, id);
        EXPECT_FLOAT_EQ(a.physical.mieScaleHeightKm, b.physical.mieScaleHeightKm) << id;
        EXPECT_FLOAT_EQ(a.physical.miePhaseG, b.physical.miePhaseG) << id;
        ExpectSameRgb(a.physical.absorptionExtinction, b.physical.absorptionExtinction, id);
        EXPECT_FLOAT_EQ(a.physical.absorptionCenterKm, b.physical.absorptionCenterKm) << id;
        EXPECT_FLOAT_EQ(a.physical.absorptionHalfWidthKm, b.physical.absorptionHalfWidthKm) << id;
        ExpectSameRgb(a.physical.groundAlbedo, b.physical.groundAlbedo, id);
        EXPECT_FLOAT_EQ(a.physical.exposure, b.physical.exposure) << id;
    }

    EXPECT_STREQ(sol->star.id, BodyCatalog::kSolStar.id);
    EXPECT_FLOAT_EQ(sol->star.effectiveTemperatureK, BodyCatalog::kSolStar.effectiveTemperatureK);
    EXPECT_FLOAT_EQ(sol->star.glowTemperatureK, BodyCatalog::kSolStar.glowTemperatureK);
    EXPECT_DOUBLE_EQ(sol->star.radiusKm, BodyCatalog::kSolStar.radiusKm);

    // The layout rows read back as OrbitLayout's compiled Sol table.
    ASSERT_EQ(static_cast<int>(sol->layout.size()), OrbitLayout::kBodyCount);
    for (int i = 0; i < OrbitLayout::kBodyCount; ++i) {
        const auto body = static_cast<OrbitLayout::Body>(i);
        const OrbitLayout::BodyRow& row = sol->layout[static_cast<std::size_t>(i)];
        EXPECT_FLOAT_EQ(row.auDistance, OrbitLayout::GetAuDistance(body)) << i;
        EXPECT_FLOAT_EQ(row.orbitalPeriodDays, OrbitLayout::GetOrbitalPeriodDays(body)) << i;
        EXPECT_FLOAT_EQ(row.inclinationDeg, OrbitLayout::GetInclinationDegrees(body)) << i;
        EXPECT_FLOAT_EQ(row.siderealRotationDays, OrbitLayout::GetSiderealRotationDays(body)) << i;
        EXPECT_FLOAT_EQ(row.compressedOffset.x, OrbitLayout::GetCompressedOffset(body).x) << i;
        EXPECT_FLOAT_EQ(row.compressedOffset.z, OrbitLayout::GetCompressedOffset(body).z) << i;
    }
}

TEST_F(StarSystemTest, EverySystemInTheIndexLoads) {
    Json::Value index;
    std::string error;
    ASSERT_TRUE(Json::ParseFile(RepoPath("resource/systems/index.json"), index, error)) << error;
    ASSERT_GE(index["systems"].Size(), 9u);
    EXPECT_EQ(index["systems"][std::size_t{0}]["id"].AsString(), "sol");
    for (const Json::Value& entry : index["systems"].Items()) {
        const std::string id = entry["id"].AsString();
        auto system = Load(id);
        ASSERT_NE(system, nullptr) << id;
        EXPECT_EQ(system->id, id);
        // Every focus body but the star has a row, and the layout has one per focus index.
        const int planets = static_cast<int>(entry["bodyCount"].AsNumber());
        EXPECT_EQ(static_cast<int>(system->layout.size()), planets + 1) << id;
        for (const BodyCatalog::Entry& e : system->entries) {
            EXPECT_GT(e.radiusKm, 0.0f) << e.id;
            if (id != "sol") {
                // No exoplanet has a texture: they draw PROCEDURAL_SURFACE.
                EXPECT_TRUE(e.procedural.enabled) << e.id;
                EXPECT_STREQ(e.lod.diffuse, "") << e.id;
                EXPECT_TRUE(e.placeFromElements) << e.id;
                EXPECT_STREQ(e.parentId, system->star.id) << e.id;
            }
        }
    }
}

TEST_F(StarSystemTest, Trappist1HasSevenResonantPlanetsAndARedStar) {
    auto system = Load("trappist-1");
    ASSERT_NE(system, nullptr);
    StarSystemCatalog::Activate(std::move(system));
    EXPECT_FALSE(StarSystemCatalog::IsSol());
    EXPECT_EQ(StarSystemCatalog::ActiveId(), "trappist-1");
    EXPECT_FALSE(OrbitLayout::IsSolLayout());
    ASSERT_EQ(OrbitLayout::BodyCount(), 8);
    ASSERT_EQ(BodyCatalog::Entries().size(), 7u);

    // Periods (archive, days), b … h, and the 8:5, 5:3, 3:2, 3:2, 4:3, 3:2 resonant chain.
    const double periods[] = {1.510826, 2.421937, 4.049219, 6.101013, 9.20754, 12.352446, 18.772866};
    const double ratios[][2] = {{8, 5}, {5, 3}, {3, 2}, {3, 2}, {4, 3}, {3, 2}};
    for (int i = 1; i <= 7; ++i) {
        EXPECT_NEAR(OrbitLayout::GetOrbitalPeriodDays(static_cast<OrbitLayout::Body>(i)), periods[i - 1], 1e-4) << i;
    }
    for (int i = 0; i < 6; ++i) {
        const double observed = periods[i + 1] / periods[i];
        // Near-resonant, as measured: g:h is 1.520 (Luger et al. 2017 call the chain "near" 3:2).
        EXPECT_NEAR(observed, ratios[i][0] / ratios[i][1], 0.02 * observed) << "pair " << i;
    }

    const char* letters = "bcdefgh";
    for (int i = 0; i < 7; ++i) {
        const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(i + 1);
        ASSERT_NE(entry, nullptr);
        EXPECT_EQ(std::string(entry->id), std::string("trappist_1_") + letters[i]);
        EXPECT_EQ(OrbitLayout::BodyFromName(entry->id), static_cast<OrbitLayout::Body>(i + 1));
    }
    EXPECT_EQ(OrbitLayout::BodyFromName("TRAPPIST-1 e"), static_cast<OrbitLayout::Body>(4));

    // A 2566 K M8 V star: its light is red.
    EXPECT_FLOAT_EQ(BodyCatalog::Star().effectiveTemperatureK, 2566.0f);
    const glm::vec3 tint = StarPhysics::RelativeTint(BodyCatalog::Star().effectiveTemperatureK);
    EXPECT_FLOAT_EQ(tint.x, 1.0f);
    EXPECT_LT(tint.y, 0.75f);
    EXPECT_LT(tint.z, 0.45f);

    // The camera lands on e, the planet closest to the middle of the habitable zone.
    EXPECT_EQ(StarSystemCatalog::EarthLikeFocusIndex(), 4);
}

TEST_F(StarSystemTest, Trappist1PlanetsTransitAtTheirArchiveEpochs) {
    auto system = Load("trappist-1");
    ASSERT_NE(system, nullptr);
    StarSystemCatalog::Activate(std::move(system));
    // pl_tranmid (BJD) of b and e: mid-transit puts the planet on the line of sight, which the
    // generator fixes at longitude 90° for every planet so their relative phases are measured.
    const struct {
        int index;
        double tranmid;
    } transits[] = {{1, 2457322.51419300}, {4, 2457660.36766210}};
    for (const auto& t : transits) {
        OrbitLayout::SetJulianDate(t.tranmid);
        const glm::dvec3 p = OrbitLayout::GetHeliocentricScenePosition(static_cast<OrbitLayout::Body>(t.index));
        const double lonDeg = WrapDeg(std::atan2(p.z, p.x) * 180.0 / 3.14159265358979323846);
        EXPECT_NEAR(lonDeg, 90.0, 0.5) << t.index;
        // Pure function of the date: one period later it is back.
        const double period = OrbitLayout::GetOrbitalPeriodDays(static_cast<OrbitLayout::Body>(t.index));
        OrbitLayout::SetJulianDate(t.tranmid + 50.0 * period);
        const glm::dvec3 q = OrbitLayout::GetHeliocentricScenePosition(static_cast<OrbitLayout::Body>(t.index));
        EXPECT_NEAR(glm::length(p - q) / glm::length(p), 0.0, 1e-3) << t.index;
    }
}

TEST_F(StarSystemTest, RealisticScalePlacesPlanetsAtTheirSemiMajorAxis) {
    auto system = Load("trappist-1");
    ASSERT_NE(system, nullptr);
    StarSystemCatalog::Activate(std::move(system));
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    OrbitLayout::SetJulianDate(Ephemeris::kJ2000 + 1234.5);
    for (int i = 1; i < OrbitLayout::BodyCount(); ++i) {
        const auto body = static_cast<OrbitLayout::Body>(i);
        const double r = glm::length(OrbitLayout::GetHeliocentricScenePosition(body));
        const double expected = static_cast<double>(OrbitLayout::GetAuDistance(body)) * OrbitLayout::kAuToSceneUnits;
        EXPECT_NEAR(r, expected, 0.02 * expected) << i; // e < 0.011 for every TRAPPIST-1 planet
        EXPECT_NEAR(OrbitLayout::GetHeliocentricRadiusAu(body), OrbitLayout::GetAuDistance(body),
                    0.02 * OrbitLayout::GetAuDistance(body));
    }
    // The star is drawn at its own radius: 0.1192 R☉.
    EXPECT_NEAR(SceneScale::SunRadius(), SceneScale::KmToScene(0.1192 * StarPhysics::kSunRadiusKm), 1e-6);

    // Compressed scale keeps the art orbit radii, inner to outer.
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Compressed);
    float previous = 0.0f;
    for (int i = 1; i < OrbitLayout::BodyCount(); ++i) {
        const float r = OrbitLayout::GetOrbitRadius(static_cast<OrbitLayout::Body>(i));
        EXPECT_GT(r, previous) << i;
        previous = r;
    }
}

TEST_F(StarSystemTest, SwitchingBackToSolRestoresItExactly) {
    const double jd = Ephemeris::kJ2000 + 9000.25;
    OrbitLayout::SetJulianDate(jd);
    const glm::dvec3 earth = OrbitLayout::GetHeliocentricScenePosition(OrbitLayout::Body::Earth);
    const double earthSpin = OrbitLayout::AxialSpinDegreesAt(OrbitLayout::Body::Earth, jd);
    const float belt = OrbitLayout::AuToSceneDistance(2.7f);

    auto system = Load("proxima-cen");
    ASSERT_NE(system, nullptr);
    StarSystemCatalog::Activate(std::move(system));
    EXPECT_EQ(OrbitLayout::BodyCount(), 3);
    EXPECT_EQ(BodyCatalog::FindById("earth"), nullptr);

    StarSystemCatalog::ActivateSol();
    EXPECT_TRUE(StarSystemCatalog::IsSol());
    EXPECT_EQ(OrbitLayout::BodyCount(), OrbitLayout::kBodyCount);
    const glm::dvec3 again = OrbitLayout::GetHeliocentricScenePosition(OrbitLayout::Body::Earth);
    EXPECT_EQ(again.x, earth.x);
    EXPECT_EQ(again.y, earth.y);
    EXPECT_EQ(again.z, earth.z);
    EXPECT_EQ(OrbitLayout::AxialSpinDegreesAt(OrbitLayout::Body::Earth, jd), earthSpin);
    EXPECT_EQ(OrbitLayout::AuToSceneDistance(2.7f), belt);
    EXPECT_NE(BodyCatalog::FindById("earth"), nullptr);
    EXPECT_FLOAT_EQ(SceneScale::SunArtRadius(), 1.0f);
}

TEST_F(StarSystemTest, DeepLinkPlanetNamesResolveToFocusIndices) {
    auto system = Load("trappist-1");
    ASSERT_NE(system, nullptr);
    StarSystemCatalog::Activate(std::move(system));

    // ?system=trappist-1&planet=e, and the other spellings a link might carry.
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("e"), 4);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("E"), 4);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("trappist_1_e"), 4);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("trappist-1-b"), 1);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("trappist_1"), 0);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("7"), 7);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("8"), -1);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("z"), -1);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName(""), -1);
    EXPECT_EQ(StarSystemCatalog::EarthLikeFocusIndex(), 4);

    // In Sol the same lookup takes the catalog ids ("earth"), so the native env override and
    // the page's PLANET_IDS agree.
    StarSystemCatalog::ActivateSol();
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("earth"), 3);
    EXPECT_EQ(StarSystemCatalog::FocusIndexForName("Mars"), 4);
}

TEST_F(StarSystemTest, MalformedDocumentsAreRejected) {
    std::string error;
    Json::Value document;
    ASSERT_TRUE(Json::Parse(R"({"format": "something-else", "version": 1})", document, error));
    EXPECT_EQ(StarSystemCatalog::ParseRuntimeJson(document, error), nullptr);
    ASSERT_TRUE(
        Json::Parse(R"({"format": "solarsystem-runtime-catalog", "version": 1, "id": "../etc"})", document, error));
    EXPECT_EQ(StarSystemCatalog::ParseRuntimeJson(document, error), nullptr);
    EXPECT_FALSE(StarSystemCatalog::IsValidId("../x"));
    EXPECT_FALSE(StarSystemCatalog::IsValidId(""));
    EXPECT_TRUE(StarSystemCatalog::IsValidId("trappist-1"));
    EXPECT_EQ(StarSystemCatalog::LoadRuntimeJson(RepoPath("resource/systems/generated/missing.runtime.json"), error),
              nullptr);
}

// fit.rotation "tidally-locked": spin period = orbital period and, as for the Moon, the same
// face toward the star all orbit. The body frame is the model matrix's: R_y(spin) (the
// exoplanet rows have no tilt), so the star's direction in it must not move.
TEST_F(StarSystemTest, TidallyLockedPlanetsKeepOneFaceToTheirStar) {
    auto system = Load("trappist-1");
    ASSERT_NE(system, nullptr);
    StarSystemCatalog::Activate(std::move(system));
    const auto e = static_cast<OrbitLayout::Body>(4);
    auto starInBodyFrame = [&](double jd) {
        OrbitLayout::SetJulianDate(jd);
        const glm::dvec3 toStar = -glm::normalize(OrbitLayout::GetHeliocentricScenePosition(e));
        const double spin = OrbitLayout::AxialSpinDegreesAt(e, jd) * 3.14159265358979323846 / 180.0;
        // R_y(-spin) applied to toStar (the inverse of the body's rotation about +Y).
        const double c = std::cos(spin);
        const double s = std::sin(spin);
        return glm::dvec3(c * toStar.x - s * toStar.z, toStar.y, s * toStar.x + c * toStar.z);
    };
    const double jd0 = Ephemeris::kJ2000 + 9000.0;
    const glm::dvec3 first = starInBodyFrame(jd0);
    // The generator's W0 puts the sub-stellar point at map longitude 0: body +X (u = 0.5).
    EXPECT_GT(first.x, std::cos(3.0 * 3.14159265358979323846 / 180.0));
    for (double dt : {0.7, 1.5, 3.05, 4.6}) {
        const glm::dvec3 later = starInBodyFrame(jd0 + dt);
        // e = 0.005: the libration in longitude is ~2e = 0.6 deg.
        EXPECT_GT(glm::dot(first, later), std::cos(2.0 * 3.14159265358979323846 / 180.0)) << dt;
    }
}
