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

QualityTierSettings GetQualitySettings(int preset, bool mobile) {
    if (mobile) {
        switch (preset) {
            case 0: return {1024, 0, 0, false, "low"};
            case 1: return {2048, 2, 0, true, "medium"};
            default: return {3000, 2, 0, true, "full"};
        }
    }
    switch (preset) {
        case 0: return {1024, 0, 0, false, "low"};
        case 1: return {2048, 2, 0, true, "medium"};
        default: return {3000, 4, 4, true, "full"};
    }
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
              << " | LOD distance multiplier=" << (std::strcmp(settings.name, "medium") == 0 ? 1.5f : 1.0f)
              << " | MSAA=" << settings.requestedMsaaSamples << "x"
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
            const ua = navigator.userAgent || '';
            const mobileUa = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(ua);
            const coarsePointer = window.matchMedia('(pointer: coarse)').matches;
            const smallScreen = Math.min(window.screen.width, window.screen.height) <= 768;
            return (mobileUa || (coarsePointer && smallScreen)) ? 1 : 0;
        } catch (error) {
            console.warn('[Quality] Could not detect mobile device:', error);
            return 0;
        }
    }) != 0;
}

int ReadInitialQualityPreset() {
    return EM_ASM_INT({
        try {
            const params = new URLSearchParams(window.location.search);
            const quality = params.get('quality') || params.get('q');
            if (quality) {
                const normalized = quality.toLowerCase();
                if (normalized === 'low' || normalized === '0') return 0;
                if (normalized === 'medium' || normalized === 'med' || normalized === '1') return 1;
                if (normalized === 'full' || normalized === '2') return 2;
            }

            const ua = navigator.userAgent || '';
            const mobileUa = /Android|webOS|iPhone|iPad|iPod|BlackBerry|IEMobile|Opera Mini/i.test(ua);
            const coarsePointer = window.matchMedia('(pointer: coarse)').matches;
            const smallScreen = Math.min(window.screen.width, window.screen.height) <= 768;
            if (mobileUa || (coarsePointer && smallScreen)) {
                console.log('[Quality] Mobile device detected; defaulting to low preset');
                return 0;
            }
        } catch (error) {
            console.warn('[Quality] Could not read URL preset:', error);
        }
        return 2;
    });
}
#endif

