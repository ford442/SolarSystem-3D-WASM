#include "QualitySettings.h"
#include <cstring>
#include <iomanip>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

int g_qualityPreset = 2;
bool g_isMobileWeb = false;

std::string GetTexturePath(const std::string& lowRes, const std::string& highRes) {
#ifdef __EMSCRIPTEN__
    return lowRes;
#else
    return highRes;
#endif
}

const char* TextureLodTierName(TextureLodTier tier) {
    switch (tier) {
        case TextureLodTier::Low: return "low";
        case TextureLodTier::Mid: return "mid";
        case TextureLodTier::High: return "high";
    }
    return "unknown";
}

QualityTierSettings GetQualitySettings(int preset, bool mobile) {
    if (mobile) {
        switch (preset) {
            case 0: return {1024, 0, 0, false, 400, TextureLodTier::Low, "low"};
            case 1: return {2048, 2, 0, true, 900, TextureLodTier::Mid, "medium"};
            default: return {3000, 2, 0, true, 1400, TextureLodTier::High, "full"};
        }
    }
    switch (preset) {
        case 0: return {1024, 0, 0, false, 600, TextureLodTier::Low, "low"};
        case 1: return {2048, 2, 0, true, 1800, TextureLodTier::Mid, "medium"};
        default: return {3000, 4, 4, true, 4000, TextureLodTier::High, "full"};
    }
}

TextureLodTier GetMaxTextureLodTier() {
    return GetQualitySettings(g_qualityPreset, g_isMobileWeb).maxTextureLodTier;
}

void LogQualityTier(const QualityTierSettings& settings, bool hdrEnabled, int shadowQuality) {
    const double shadowMemoryMiB =
        static_cast<double>(settings.shadowResolution) * settings.shadowResolution * 4.0 / (1024.0 * 1024.0);
    std::cout << "[Quality] Active tier: " << settings.name
              << " | shadows="
              << (shadowQuality > 0 ? std::to_string(settings.shadowResolution) + "x" + std::to_string(settings.shadowResolution)
                                  : "off")
              << " (~" << std::fixed << std::setprecision(1) << shadowMemoryMiB << " MiB depth)"
              << " | HDR=" << (hdrEnabled ? "on" : "off (direct composite)")
              << " | high-res concurrency=" << settings.maxConcurrentTextureLoads
              << " | max texture LOD=" << TextureLodTierName(settings.maxTextureLodTier)
              << " | LOD distance multiplier=" << (std::strcmp(settings.name, "medium") == 0 ? 1.5f : 1.0f)
              << " | MSAA=" << settings.requestedMsaaSamples << "x"
              << " | asteroids=" << settings.asteroidInstanceCount
              << std::defaultfloat << std::endl;
#ifdef __EMSCRIPTEN__
    std::cout << "[Quality] WebGL MSAA is fixed when the context is created; reload with ?quality="
              << settings.name << " to change it." << std::endl;
#endif
}

#ifdef __EMSCRIPTEN__
bool ReadIsMobileWeb() {
    return EM_ASM_INT({
        try {
            const init = window.__solarSystemInit;
            if (init && typeof init.isMobileWeb === 'boolean') {
                return init.isMobileWeb ? 1 : 0;
            }
        } catch (error) {
            console.warn('[Quality] Could not read init config:', error);
        }
        return 0;
    }) != 0;
}

int ReadInitialQualityPreset() {
    return EM_ASM_INT({
        try {
            const init = window.__solarSystemInit;
            if (init && typeof init.qualityPreset === 'number') {
                const preset = Math.max(0, Math.min(2, init.qualityPreset | 0));
                return preset;
            }
        } catch (error) {
            console.warn('[Quality] Could not read init config:', error);
        }
        return 2;
    });
}
#endif
