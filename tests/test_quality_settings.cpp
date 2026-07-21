#include <gtest/gtest.h>

#include "QualitySettings.h"

namespace {

void ExpectTier(const QualityTierSettings& settings,
                uint16_t shadowResolution,
                int maxConcurrentTextureLoads,
                int requestedMsaaSamples,
                bool enableHdr,
                int asteroidInstanceCount,
                const char* name) {
    EXPECT_EQ(settings.shadowResolution, shadowResolution);
    EXPECT_EQ(settings.maxConcurrentTextureLoads, maxConcurrentTextureLoads);
    EXPECT_EQ(settings.requestedMsaaSamples, requestedMsaaSamples);
    EXPECT_EQ(settings.enableHdr, enableHdr);
    EXPECT_EQ(settings.asteroidInstanceCount, asteroidInstanceCount);
    EXPECT_STREQ(settings.name, name);
}

} // namespace

TEST(QualitySettingsTest, DesktopPresetMapping) {
    ExpectTier(GetQualitySettings(0, false), 1024, 0, 0, false, 600, "low");
    ExpectTier(GetQualitySettings(1, false), 2048, 2, 0, true, 1800, "medium");
    ExpectTier(GetQualitySettings(2, false), 3000, 4, 4, true, 4000, "full");
}

TEST(QualitySettingsTest, MobileDowngradesMsaaOnFullPreset) {
    ExpectTier(GetQualitySettings(2, true), 3000, 2, 0, true, 1400, "full");
}

TEST(QualitySettingsTest, MobileScalesAsteroidCountsDown) {
    ExpectTier(GetQualitySettings(0, true), 1024, 0, 0, false, 400, "low");
    ExpectTier(GetQualitySettings(1, true), 2048, 2, 0, true, 900, "medium");
}

TEST(QualitySettingsTest, OutOfRangePresetUsesFullTier) {
    ExpectTier(GetQualitySettings(99, false), 3000, 4, 4, true, 4000, "full");
}

#ifndef __EMSCRIPTEN__
TEST(QualitySettingsTest, NativeTexturePathPrefersHighRes) {
    EXPECT_EQ(GetTexturePath("textures_low/Earth.dds", "textures/Earth.dds"), "textures/Earth.dds");
}
#endif
