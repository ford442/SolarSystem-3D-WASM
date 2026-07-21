#ifndef SOLARSYSTEM_QUALITY_SETTINGS_H
#define SOLARSYSTEM_QUALITY_SETTINGS_H

#include <cstdint>
#include <string>

extern int g_qualityPreset;
extern bool g_isMobileWeb;

struct QualityTierSettings {
    uint16_t shadowResolution;
    int maxConcurrentTextureLoads;
    int requestedMsaaSamples;
    bool enableHdr;
    int asteroidInstanceCount;
    const char* name;
};

std::string GetTexturePath(const std::string& lowRes, const std::string& highRes);
QualityTierSettings GetQualitySettings(int preset, bool mobile);
void LogQualityTier(const QualityTierSettings& settings, bool hdrEnabled, int shadowQuality);

#ifdef __EMSCRIPTEN__
bool ReadIsMobileWeb();
int ReadInitialQualityPreset();
#endif

#endif // SOLARSYSTEM_QUALITY_SETTINGS_H
