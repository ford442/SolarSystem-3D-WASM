#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <vector>

#include "Solar_System/BodyCatalog.generated.h"
#include "Solar_System/CatalogMaterial.h"
#include "Solar_System/OrbitLayout.h"

TEST(BodyCatalogTest, FocusIndicesUnchanged) {
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Sun), 0);
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Mercury), 1);
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Pluto), 9);
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Ceres), 10);
    EXPECT_EQ(static_cast<int>(OrbitLayout::Body::Vesta), 11);
    EXPECT_EQ(OrbitLayout::kBodyCount, 12);
}

TEST(BodyCatalogTest, MercuryThroughPlutoHaveCatalogRows) {
    for (int i = 1; i <= 11; ++i) {
        const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(i);
        ASSERT_NE(entry, nullptr) << "missing catalog row for focus index " << i;
        EXPECT_EQ(entry->index, i);
        EXPECT_NE(entry->kind, BodyCatalog::Kind::Satellite);
        EXPECT_NE(entry->lod.diffuse[0], '\0');
        EXPECT_NE(entry->lod.normal[0], '\0');
    }
}

TEST(BodyCatalogTest, AxialTiltMatchesCatalogSSot) {
    EXPECT_FLOAT_EQ(BodyCatalog::FindById("venus")->axialTiltDegrees, 177.3f);
    EXPECT_FLOAT_EQ(BodyCatalog::FindById("earth")->axialTiltDegrees, -23.4f);
    EXPECT_FLOAT_EQ(BodyCatalog::FindById("uranus")->axialTiltDegrees, -97.8f);
    EXPECT_FLOAT_EQ(BodyCatalog::FindById("neptune")->axialTiltDegrees, -28.3f);
    EXPECT_FLOAT_EQ(BodyCatalog::FindById("pluto")->axialTiltDegrees, -122.5f);
    EXPECT_FLOAT_EQ(BodyCatalog::FindById("saturn")->artTiltXDegrees, -15.0f);
}

TEST(BodyCatalogTest, MoonAndIoAreCatalogSatellites) {
    const BodyCatalog::Entry* moon = BodyCatalog::FindById("moon");
    ASSERT_NE(moon, nullptr);
    EXPECT_EQ(moon->kind, BodyCatalog::Kind::Satellite);
    EXPECT_STREQ(moon->parentId, "earth");
    EXPECT_STREQ(moon->initTag, "EarthSystem");
    EXPECT_FLOAT_EQ(moon->sceneOrbitRadius, 25.0f);
    EXPECT_GT(moon->keplerian.aKm, 0.0f);
    EXPECT_EQ(moon->index, 12);

    const BodyCatalog::Entry* io = BodyCatalog::FindById("io");
    ASSERT_NE(io, nullptr);
    EXPECT_EQ(io->kind, BodyCatalog::Kind::Satellite);
    EXPECT_STREQ(io->parentId, "jupiter");
    EXPECT_STREQ(io->initTag, "JupiterSystem");
    EXPECT_FLOAT_EQ(io->sceneOrbitRadius, 65.0f);
    EXPECT_GT(io->keplerian.nDegPerDay, 0.0f);
}

TEST(BodyCatalogTest, InitTagsResolveToPrimaries) {
    EXPECT_STREQ(BodyCatalog::FindPrimaryByInitTag("Mercury")->id, "mercury");
    EXPECT_STREQ(BodyCatalog::FindPrimaryByInitTag("EarthSystem")->id, "earth");
    EXPECT_STREQ(BodyCatalog::FindPrimaryByInitTag("Ceres")->id, "ceres");
    EXPECT_EQ(BodyCatalog::FindPrimaryByInitTag("NotAPlanet"), nullptr);
}

TEST(BodyCatalogTest, EarthKeepsNightAndCloudMaps) {
    const BodyCatalog::Entry* earth = BodyCatalog::FindById("earth");
    ASSERT_NE(earth, nullptr);
    EXPECT_TRUE(earth->shaderFlags.hasNightTexture);
    EXPECT_TRUE(earth->shaderFlags.hasClouds);
    EXPECT_STREQ(earth->lod.night, "Earth_Night_Diffuse");
    EXPECT_STREQ(earth->lod.clouds, "Earth_Clouds_Diffuse");
    EXPECT_TRUE(earth->hasCloudLayer);
    EXPECT_STREQ(earth->cloudLayer.diffuse, "Earth_Clouds_Diffuse");
}

TEST(BodyCatalogTest, MaterialFollowsShaderFlagsAndLodIds) {
    // CatalogMaterial reads shaderFlags only; the generator guarantees they match the lod ids
    // that decide which textures get loaded. Moons never carry night or cloud maps.
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        const CatalogMaterial::Material material = CatalogMaterial::Material::FromEntry(entry);
        EXPECT_EQ(material.hasSpecular, entry.lod.specular != nullptr) << entry.id;
        EXPECT_EQ(material.hasNight, entry.lod.night != nullptr) << entry.id;
        EXPECT_EQ(material.hasClouds, entry.lod.clouds != nullptr) << entry.id;
        EXPECT_EQ(material.useSphereIntersect, entry.useSphereIntersect) << entry.id;
        EXPECT_FLOAT_EQ(material.ambientFactor, entry.ambientFactor) << entry.id;
        if (entry.kind == BodyCatalog::Kind::Satellite) {
            EXPECT_FALSE(material.hasNight) << entry.id;
            EXPECT_FALSE(material.hasClouds) << entry.id;
        }
    }
}

// render.atmosphere replaced the hand-maintained SystemVisuals::kAtmospheres table. The
// O'Neil numbers must have moved across unchanged, or the Low preset would look different.
TEST(BodyCatalogTest, OneilAtmosphereNumbersMovedFromSystemVisualsUnchanged) {
    struct Expected {
        const char* id;
        float shellScale;
        float r, g, b;
        bool innerMinusEpsilon;
        float outerRadius;
        float mieR, mieG, mieB;
        float hScale;
        bool toneMapping;
    };
    const Expected expected[] = {
        {"venus", 1.1f, 203.0f / 255.0f, 158.0f / 255.0f, 69.0f / 255.0f, true, 1.995f, 1, 1, 1, 6.0f, false},
        {"earth", 1.1f, 0.3f, 0.7f, 1.0f, true, 2.1f, 1, 1, 1, 6.0f, false},
        {"mars", 0.583f, 0.976f, 0.302f, 0.208f, true, 1.113f, 1, 1, 1, 6.0f, false},
        {"jupiter", 11.4f, 153.0f / 255.0f, 139.0f / 255.0f, 120.0f / 255.0f, true, 23.35f, 1, 1, 1, 26.0f, true},
        {"saturn", 9.34f, 84.0f / 255.0f, 132.0f / 255.0f, 176.0f / 255.0f, true, 18.6f, 1, 1, 1, 27.0f, true},
        {"uranus", 4.0f, 45.0f / 255.0f, 101.0f / 255.0f, 114.0f / 255.0f, true, 8.1f, 1, 1, 1, 24.0f, true},
        {"neptune", 3.9f, 62.0f / 255.0f, 92.0f / 255.0f, 169.0f / 255.0f, true, 7.9f, 1, 1, 1, 23.0f, true},
        {"pluto", 0.45f, 92.0f / 255.0f, 120.0f / 255.0f, 141.0f / 255.0f, false, 1.0f,
         35.0f / 255.0f, 52.0f / 255.0f, 220.0f / 255.0f, 16.0f, true},
        {"titan", 0.504136f, 40.0f / 255.0f, 33.0f / 255.0f, 72.0f / 255.0f, true, 0.8429210f,
         0.36862745f, 0.0666667f, 0.0196078f, 4.8f, false},
    };
    EXPECT_EQ(std::size(BodyCatalog::kAtmospheres), std::size(expected));
    for (const Expected& e : expected) {
        const BodyCatalog::AtmosphereRow* row = BodyCatalog::FindAtmosphere(e.id);
        ASSERT_NE(row, nullptr) << e.id;
        const BodyCatalog::AtmosphereOneil& o = row->oneil;
        EXPECT_FLOAT_EQ(o.shellScale, e.shellScale) << e.id;
        EXPECT_FLOAT_EQ(o.color.r, e.r) << e.id;
        EXPECT_FLOAT_EQ(o.color.g, e.g) << e.id;
        EXPECT_FLOAT_EQ(o.color.b, e.b) << e.id;
        EXPECT_EQ(o.innerRadiusMinusEpsilon, e.innerMinusEpsilon) << e.id;
        EXPECT_FLOAT_EQ(o.outerRadius, e.outerRadius) << e.id;
        EXPECT_FLOAT_EQ(o.mieTint.r, e.mieR) << e.id;
        EXPECT_FLOAT_EQ(o.mieTint.g, e.mieG) << e.id;
        EXPECT_FLOAT_EQ(o.mieTint.b, e.mieB) << e.id;
        EXPECT_FLOAT_EQ(o.hScaleFactor, e.hScale) << e.id;
        EXPECT_EQ(o.toneMapping, e.toneMapping) << e.id;
    }
}

TEST(BodyCatalogTest, PhysicalAtmospheresAreEarthVenusMarsTitan) {
    std::vector<std::string> physical;
    for (const BodyCatalog::AtmosphereRow& row : BodyCatalog::kAtmospheres) {
        if (!row.physical.enabled) {
            continue;
        }
        physical.emplace_back(row.bodyId);
        const BodyCatalog::AtmospherePhysical& p = row.physical;
        EXPECT_GT(p.topRadiusKm, p.groundRadiusKm) << row.bodyId;
        EXPECT_GT(p.thicknessScale, 0.0f) << row.bodyId;
        EXPECT_LT(std::abs(p.miePhaseG), 1.0f) << row.bodyId;
        EXPECT_LE(p.mieScattering.r, p.mieExtinction.r) << row.bodyId;
        EXPECT_LE(p.mieScattering.g, p.mieExtinction.g) << row.bodyId;
        EXPECT_LE(p.mieScattering.b, p.mieExtinction.b) << row.bodyId;
    }
    std::sort(physical.begin(), physical.end());
    EXPECT_EQ(physical, (std::vector<std::string>{"earth", "mars", "titan", "venus"}));
}

TEST(BodyCatalogTest, TitanAtmosphereIsAnOrdinaryCatalogRow) {
    const BodyCatalog::Entry* titan = BodyCatalog::FindById("titan");
    ASSERT_NE(titan, nullptr);
    EXPECT_EQ(titan->kind, BodyCatalog::Kind::Satellite);
    ASSERT_NE(BodyCatalog::FindAtmosphere("titan"), nullptr);
    EXPECT_EQ(BodyCatalog::FindAtmosphere("moon"), nullptr);
}
