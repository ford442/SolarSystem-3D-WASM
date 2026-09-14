#include <gtest/gtest.h>

#include "Solar_System/BodyCatalog.generated.h"
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
