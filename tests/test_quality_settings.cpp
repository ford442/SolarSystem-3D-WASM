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

void ExpectEffects(const QualityTierSettings& settings, bool pbrAtmosphere, int atmosphereSteps,
                   bool volumetricCorona, int coronaSlices) {
    EXPECT_EQ(settings.enablePbrAtmosphere, pbrAtmosphere) << settings.name;
    EXPECT_EQ(settings.atmosphereSteps, atmosphereSteps) << settings.name;
    EXPECT_EQ(settings.enableVolumetricCorona, volumetricCorona) << settings.name;
    EXPECT_EQ(settings.coronaSlices, coronaSlices) << settings.name;
}

} // namespace

TEST(QualitySettingsTest, ObserveStarCountGrowsWithTier) {
    EXPECT_EQ(GetQualitySettings(0, false).observeStarCount, 500);
    EXPECT_EQ(GetQualitySettings(1, false).observeStarCount, 2000);
    EXPECT_EQ(GetQualitySettings(2, false).observeStarCount, 5000);
    EXPECT_EQ(GetQualitySettings(0, true).observeStarCount, 500);
    EXPECT_LT(GetQualitySettings(2, true).observeStarCount, GetQualitySettings(2, false).observeStarCount);
    for (const bool mobile : {false, true}) {
        EXPECT_LT(GetQualitySettings(0, mobile).observeStarCount, GetQualitySettings(1, mobile).observeStarCount);
        EXPECT_LT(GetQualitySettings(1, mobile).observeStarCount, GetQualitySettings(2, mobile).observeStarCount);
    }
}

TEST(QualitySettingsTest, EveryTierLoadsTheNearestThreeSystemsEagerly) {
    for (const bool mobile : {false, true}) {
        EXPECT_EQ(GetQualitySettings(0, mobile).eagerPlanetSystems, 3);
        EXPECT_LE(GetQualitySettings(0, mobile).eagerPlanetSystems, GetQualitySettings(1, mobile).eagerPlanetSystems);
        EXPECT_LE(GetQualitySettings(1, mobile).eagerPlanetSystems, GetQualitySettings(2, mobile).eagerPlanetSystems);
    }
}

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

TEST(QualitySettingsTest, KuiperDiskIsOffOnLowAndSmallerThanTheBelt) {
    for (const bool mobile : {false, true}) {
        EXPECT_EQ(GetQualitySettings(0, mobile).kuiperInstanceCount, 0);
        for (int preset = 1; preset <= 2; ++preset) {
            const QualityTierSettings s = GetQualitySettings(preset, mobile);
            EXPECT_GT(s.kuiperInstanceCount, 0) << s.name;
            EXPECT_LT(s.kuiperInstanceCount, s.asteroidInstanceCount) << s.name;
        }
    }
}

TEST(QualitySettingsTest, OutOfRangePresetUsesFullTier) {
    ExpectTier(GetQualitySettings(99, false), 3000, 4, 4, true, true, 2, 4000, TextureLodTier::High, "full");
}

TEST(QualitySettingsTest, TextureLodPolicyFollowsPreset) {
    EXPECT_EQ(GetTextureLodPolicy(0, false).maxTier, TextureLodTier::Low);
    EXPECT_EQ(GetTextureLodPolicy(1, false).maxTier, TextureLodTier::Mid);
    EXPECT_EQ(GetTextureLodPolicy(2, false).maxTier, TextureLodTier::High);
    // The distance multiplier is a web Medium rule; native and the other tiers keep 1.
    EXPECT_FLOAT_EQ(GetTextureLodPolicy(2, false).planetThresholdScale, 1.0f);
#ifdef __EMSCRIPTEN__
    EXPECT_FLOAT_EQ(GetTextureLodPolicy(1, false).planetThresholdScale, 1.0f / 1.5f);
#else
    EXPECT_FLOAT_EQ(GetTextureLodPolicy(1, false).planetThresholdScale, 1.0f);
#endif
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

TEST(QualitySettingsTest, TiersWithHdrAskTheContextForNoSamples) {
    // Full's table row still says 4: the request is for the HDR path's benefit and the
    // context must not honour it, because the colour pass never draws into that buffer.
    EXPECT_EQ(GetQualitySettings(2, false).requestedMsaaSamples, 4);
    for (const bool mobile : {false, true}) {
        for (int preset = 0; preset <= 2; ++preset) {
            const auto settings = GetQualitySettings(preset, mobile);
            if (settings.enableHdr) {
                EXPECT_EQ(ContextMsaaSamples(settings), 0) << settings.name << (mobile ? " (mobile)" : "");
            } else {
                EXPECT_EQ(ContextMsaaSamples(settings), settings.requestedMsaaSamples) << settings.name;
            }
        }
    }
}

TEST(QualitySettingsTest, AntialiasPathFollowsWhatTheFrameDrawsInto) {
    // HDR on: the tone-map pass resolves, whatever the context has.
    EXPECT_EQ(ResolveAntialiasPath(true, 0), AntialiasPath::HdrResolve);
    EXPECT_EQ(ResolveAntialiasPath(true, 4), AntialiasPath::HdrResolve);
    // HDR off (low tier, or HDR::InitFBO refused): only a multisampled context antialiases.
    EXPECT_EQ(ResolveAntialiasPath(false, 4), AntialiasPath::ContextMsaa);
    EXPECT_EQ(ResolveAntialiasPath(false, 1), AntialiasPath::None);
    EXPECT_EQ(ResolveAntialiasPath(false, 0), AntialiasPath::None);
    EXPECT_STREQ(AntialiasPathName(AntialiasPath::HdrResolve), "FXAA in HDR tone-map");
}
