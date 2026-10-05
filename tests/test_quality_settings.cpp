#include <gtest/gtest.h>

#include "QualitySettings.h"
#include "SimState.h"

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

void ExpectEffects(const QualityTierSettings& settings, bool pbrAtmosphere, int atmosphereSteps,
                   bool volumetricCorona, int coronaSlices) {
    EXPECT_EQ(settings.enablePbrAtmosphere, pbrAtmosphere) << settings.name;
    EXPECT_EQ(settings.atmosphereSteps, atmosphereSteps) << settings.name;
    EXPECT_EQ(settings.enableVolumetricCorona, volumetricCorona) << settings.name;
    EXPECT_EQ(settings.coronaSlices, coronaSlices) << settings.name;
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
    gSimState->qualityPreset = 0;
    gSimState->isMobileWeb = false;
    EXPECT_EQ(GetMaxTextureLodTier(), TextureLodTier::Low);
    gSimState->qualityPreset = 1;
    EXPECT_EQ(GetMaxTextureLodTier(), TextureLodTier::Mid);
    gSimState->qualityPreset = 2;
    EXPECT_EQ(GetMaxTextureLodTier(), TextureLodTier::High);
}

#ifndef __EMSCRIPTEN__
TEST(QualitySettingsTest, NativeTexturePathPrefersHighRes) {
    EXPECT_EQ(GetTexturePath("textures_low/Earth.dds", "textures/Earth.dds"), "textures/Earth.dds");
}
#endif

// Low must keep the cheap atmosphere and corona on every platform (no float LUT sampling
// loop, no full-screen corona slices); mobile Medium skips the corona for fill rate.
TEST(QualitySettingsTest, AtmosphereAndCoronaEffectsPerTier) {
    ExpectEffects(GetQualitySettings(0, false), false, 0, false, 0);
    ExpectEffects(GetQualitySettings(1, false), true, 8, true, 16);
    ExpectEffects(GetQualitySettings(2, false), true, 16, true, 32);
    ExpectEffects(GetQualitySettings(0, true), false, 0, false, 0);
    ExpectEffects(GetQualitySettings(1, true), true, 8, false, 0);
    ExpectEffects(GetQualitySettings(2, true), true, 12, true, 16);
}
