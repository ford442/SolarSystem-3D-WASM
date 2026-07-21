#include <gtest/gtest.h>

#include "QualitySettings.h"

namespace {

void ExpectTier(const QualityTierSettings& settings,
                uint16_t shadowResolution,
                int maxConcurrentTextureLoads,
                int requestedMsaaSamples,
                bool enableHdr,
                const char* name) {
    EXPECT_EQ(settings.shadowResolution, shadowResolution);
    EXPECT_EQ(settings.maxConcurrentTextureLoads, maxConcurrentTextureLoads);
    EXPECT_EQ(settings.requestedMsaaSamples, requestedMsaaSamples);
    EXPECT_EQ(settings.enableHdr, enableHdr);
    EXPECT_STREQ(settings.name, name);
}

} // namespace

TEST(QualitySettingsTest, DesktopPresetMapping) {
    ExpectTier(GetQualitySettings(0, false), 1024, 0, 0, false, "low");
    ExpectTier(GetQualitySettings(1, false), 2048, 2, 0, true, "medium");
    ExpectTier(GetQualitySettings(2, false), 3000, 4, 4, true, "full");
}

TEST(QualitySettingsTest, MobileDowngradesMsaaOnFullPreset) {
    ExpectTier(GetQualitySettings(2, true), 3000, 2, 0, true, "full");
}

TEST(QualitySettingsTest, OutOfRangePresetUsesFullTier) {
    ExpectTier(GetQualitySettings(99, false), 3000, 4, 4, true, "full");
}

#ifndef __EMSCRIPTEN__
TEST(QualitySettingsTest, NativeTexturePathPrefersHighRes) {
    EXPECT_EQ(GetTexturePath("textures_low/Earth.dds", "textures/Earth.dds"), "textures/Earth.dds");
}
#endif
