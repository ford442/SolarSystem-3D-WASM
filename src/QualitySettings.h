#ifndef SOLARSYSTEM_QUALITY_SETTINGS_H
#define SOLARSYSTEM_QUALITY_SETTINGS_H

#include <cstdint>
#include <string>

extern int g_qualityPreset;
extern bool g_isMobileWeb;

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

#ifdef __EMSCRIPTEN__
bool ReadIsMobileWeb();
int ReadInitialQualityPreset();
#endif

#endif // SOLARSYSTEM_QUALITY_SETTINGS_H
