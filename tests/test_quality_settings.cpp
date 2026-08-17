#include <gtest/gtest.h>

#include "QualitySettings.h"

namespace {

void ExpectTier(const QualityTierSettings& settings,
                uint16_t shadowResolution,
                int maxConcurrentTextureLoads,
                int requestedMsaaSamples,
                bool enableHdr,
                bool enableMagneticBloom,
                int magneticBloomPasses,
                int asteroidInstanceCount,
                TextureLodTier maxTextureLodTier,
                const char* name) {
    EXPECT_EQ(settings.shadowResolution, shadowResolution);
    EXPECT_EQ(settings.maxConcurrentTextureLoads, maxConcurrentTextureLoads);
    EXPECT_EQ(settings.requestedMsaaSamples, requestedMsaaSamples);
    EXPECT_EQ(settings.enableHdr, enableHdr);
    EXPECT_EQ(settings.enableMagneticBloom, enableMagneticBloom);
    EXPECT_EQ(settings.magneticBloomPasses, magneticBloomPasses);
    EXPECT_EQ(settings.asteroidInstanceCount, asteroidInstanceCount);
    EXPECT_EQ(settings.maxTextureLodTier, maxTextureLodTier);
    EXPECT_STREQ(settings.name, name);
}

} // namespace

TEST(QualitySettingsTest, DesktopPresetMapping) {
    ExpectTier(GetQualitySettings(0, false), 1024, 0, 0, false, false, 0, 600, TextureLodTier::Low, "low");
    ExpectTier(GetQualitySettings(1, false), 2048, 2, 0, true, true, 1, 1800, TextureLodTier::Mid, "medium");
    ExpectTier(GetQualitySettings(2, false), 3000, 4, 4, true, true, 2, 4000, TextureLodTier::High, "full");
}

TEST(QualitySettingsTest, MobileDowngradesMsaaOnFullPreset) {
    ExpectTier(GetQualitySettings(2, true), 3000, 2, 0, true, true, 1, 1400, TextureLodTier::High, "full");
}

TEST(QualitySettingsTest, MobileScalesAsteroidCountsDown) {
    ExpectTier(GetQualitySettings(0, true), 1024, 0, 0, false, false, 0, 400, TextureLodTier::Low, "low");
    ExpectTier(GetQualitySettings(1, true), 2048, 2, 0, true, true, 1, 900, TextureLodTier::Mid, "medium");
}

TEST(QualitySettingsTest, OutOfRangePresetUsesFullTier) {
    ExpectTier(GetQualitySettings(99, false), 3000, 4, 4, true, true, 2, 4000, TextureLodTier::High, "full");
}

TEST(QualitySettingsTest, GetMaxTextureLodTierFollowsPreset) {
    g_qualityPreset = 0;
    g_isMobileWeb = false;
    EXPECT_EQ(GetMaxTextureLodTier(), TextureLodTier::Low);
    g_qualityPreset = 1;
    EXPECT_EQ(GetMaxTextureLodTier(), TextureLodTier::Mid);
    g_qualityPreset = 2;
    EXPECT_EQ(GetMaxTextureLodTier(), TextureLodTier::High);
}

#ifndef __EMSCRIPTEN__
TEST(QualitySettingsTest, NativeTexturePathPrefersHighRes) {
    EXPECT_EQ(GetTexturePath("textures_low/Earth.dds", "textures/Earth.dds"), "textures/Earth.dds");
}
#endif
