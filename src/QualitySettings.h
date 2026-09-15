#ifndef SOLARSYSTEM_QUALITY_SETTINGS_H
#define SOLARSYSTEM_QUALITY_SETTINGS_H

#include <cstdint>
#include <string>

enum class TextureLodTier : uint8_t {
    Low = 0,
    Mid = 1,
    High = 2
};

struct QualityTierSettings {
    uint16_t shadowResolution;
    int maxConcurrentTextureLoads;
    int requestedMsaaSamples;
    bool enableHdr;
    bool enableMagneticBloom;
    int magneticBloomPasses;
    int asteroidInstanceCount;
    TextureLodTier maxTextureLodTier;
    const char* name;
};

std::string GetTexturePath(const std::string& lowRes, const std::string& highRes);
QualityTierSettings GetQualitySettings(int preset, bool mobile);
TextureLodTier GetMaxTextureLodTier();
const char* TextureLodTierName(TextureLodTier tier);
void LogQualityTier(const QualityTierSettings& settings, bool hdrEnabled, int shadowQuality);

/**
 * Quality tier to build the GL context from, before any UI exists. Web reads
 * window.__solarSystemInit (set from the ?quality= query parameter); native reads the
 * SOLARSYSTEM_QUALITY environment variable ("low"/"medium"/"full" or 0/1/2). Defaults to
 * 2 ("full"). This is the only point where MSAA can still be chosen — see LogQualityTier.
 */
int ReadInitialQualityPreset();

#ifdef __EMSCRIPTEN__
bool ReadIsMobileWeb();
float ReadBackingStoreScale();
#endif

#endif // SOLARSYSTEM_QUALITY_SETTINGS_H
