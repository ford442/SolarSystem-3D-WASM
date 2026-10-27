#include "QualitySettings.h"
#include "core/Log.h"
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

std::string GetTexturePath(const std::string& lowRes, const std::string& highRes) {
    // Only one of the two is live per target; name the other away from -Wunused-parameter.
#ifdef __EMSCRIPTEN__
    (void)highRes;
    return lowRes;
#else
    (void)lowRes;
    return highRes;
#endif
}

int ContextMsaaSamples(const QualityTierSettings& settings) {
    return settings.enableHdr ? 0 : settings.requestedMsaaSamples;
}

AntialiasPath ResolveAntialiasPath(bool hdrEnabled, int contextSamples) {
    if (hdrEnabled) {
        return AntialiasPath::HdrResolve;
    }
    return contextSamples > 1 ? AntialiasPath::ContextMsaa : AntialiasPath::None;
}

const char* AntialiasPathName(AntialiasPath path) {
    switch (path) {
        case AntialiasPath::None: return "none";
        case AntialiasPath::ContextMsaa: return "context MSAA";
        case AntialiasPath::HdrResolve: return "FXAA in HDR tone-map";
    }
    return "unknown";
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
    // Low keeps the cheap paths everywhere (O'Neil shell, flat corona). Mobile Medium skips
    // the corona slices: they are close to full-screen when the Sun is near, so fill rate
    // rather than shader cost decides.
    if (mobile) {
        switch (preset) {
            case 0: return {1024, 0, 0, false, false, 0, 400, 0, TextureLodTier::Low, false, 0, false, 0, 500, 3, "low"};
            case 1: return {2048, 2, 0, true, true, 1, 900, 400, TextureLodTier::Mid, true, 8, false, 0, 1500, 3, "medium"};
            default: return {3000, 2, 0, true, true, 1, 1400, 800, TextureLodTier::High, true, 12, true, 16, 2500, 4, "full"};
        }
    }
    switch (preset) {
        case 0: return {1024, 0, 0, false, false, 0, 600, 0, TextureLodTier::Low, false, 0, false, 0, 500, 3, "low"};
        case 1: return {2048, 2, 0, true, true, 1, 1800, 800, TextureLodTier::Mid, true, 8, true, 16, 2000, 4, "medium"};
        default: return {3000, 4, 4, true, true, 2, 4000, 2000, TextureLodTier::High, true, 16, true, 32, 5000, 5, "full"};
    }
}

TextureLodPolicy GetTextureLodPolicy(int preset, bool mobile) {
    TextureLodPolicy policy;
    policy.maxTier = GetQualitySettings(preset, mobile).maxTextureLodTier;
#ifdef __EMSCRIPTEN__
    // Medium uses a 1.5x distance multiplier. Expressed as a threshold, this
    // means the camera must be 1.5x closer than full quality before upgrading.
    if (preset == 1) {
        policy.planetThresholdScale = 1.0f / 1.5f;
    }
#endif
    return policy;
}

void LogQualityTier(const QualityTierSettings& settings, bool hdrEnabled, int shadowQuality, int contextSamples) {
    const double shadowMemoryMiB =
        static_cast<double>(settings.shadowResolution) * settings.shadowResolution * 4.0 / (1024.0 * 1024.0);
    const std::string shadows =
        shadowQuality > 0 ? std::to_string(settings.shadowResolution) + "x" + std::to_string(settings.shadowResolution)
                          : "off";
    const std::string bloom =
        settings.enableMagneticBloom ? std::to_string(settings.magneticBloomPasses) + " pass" : "off";
    const std::string atmosphere =
        settings.enablePbrAtmosphere ? "LUT " + std::to_string(settings.atmosphereSteps) + " steps" : "O'Neil";
    const std::string corona =
        settings.enableVolumetricCorona ? std::to_string(settings.coronaSlices) + " slices" : "billboard";
    LOG_INFO("Quality",
             "Active tier: %s | shadows=%s (~%.1f MiB depth) | HDR=%s | field bloom=%s | high-res concurrency=%d"
             " | max texture LOD=%s | LOD distance multiplier=%g | AA=%s (context samples=%d) | asteroids=%d | kuiper=%d"
             " | atmosphere=%s | corona=%s | observe stars=%d",
             settings.name, shadows.c_str(), shadowMemoryMiB, hdrEnabled ? "on" : "off (direct composite)",
             bloom.c_str(), settings.maxConcurrentTextureLoads,
             TextureLodTierName(settings.maxTextureLodTier), std::strcmp(settings.name, "medium") == 0 ? 1.5 : 1.0,
             AntialiasPathName(ResolveAntialiasPath(hdrEnabled, contextSamples)), contextSamples,
             settings.asteroidInstanceCount,
             settings.kuiperInstanceCount, atmosphere.c_str(), corona.c_str(),
             settings.observeStarCount);
#ifdef __EMSCRIPTEN__
    LOG_INFO("Quality", "WebGL context samples are fixed when the context is created; reload with ?quality=%s to change them.",
             settings.name);
#else
    LOG_INFO("Quality", "Context samples are fixed when the GL context is created; restart with "
                        "SOLARSYSTEM_QUALITY=low|medium|full to change the sample count.");
#endif
}

#ifdef __EMSCRIPTEN__
bool ReadIsMobileWeb() {
    return EM_ASM_INT({
        try {
            const init = window['__solarSystemInit'];
            if (init && typeof init['isMobileWeb'] === 'boolean') {
                return init['isMobileWeb'] ? 1 : 0;
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
            const init = window['__solarSystemInit'];
            if (init && typeof init['qualityPreset'] === 'number') {
                const preset = Math.max(0, Math.min(2, init['qualityPreset'] | 0));
                return preset;
            }
        } catch (error) {
            console.warn('[Quality] Could not read init config:', error);
        }
        return 2;
    });
}

float ReadBackingStoreScale() {
    return static_cast<float>(EM_ASM_DOUBLE({
        try {
            const init = window['__solarSystemInit'];
            if (init && typeof init['backingStoreScale'] === 'number' && init['backingStoreScale'] > 0) {
                return init['backingStoreScale'];
            }
        } catch (error) {
            console.warn('[Quality] Could not read init config:', error);
        }
        return 1.0;
    }));
}
#endif

#ifndef __EMSCRIPTEN__
int ReadInitialQualityPreset() {
    const char* requested = std::getenv("SOLARSYSTEM_QUALITY");
    if (requested == nullptr) {
        return 2;
    }
    if (std::strcmp(requested, "low") == 0 || std::strcmp(requested, "0") == 0) {
        return 0;
    }
    if (std::strcmp(requested, "medium") == 0 || std::strcmp(requested, "1") == 0) {
        return 1;
    }
    if (std::strcmp(requested, "full") == 0 || std::strcmp(requested, "2") == 0) {
        return 2;
    }
    LOG_WARN("Quality", "Unrecognized SOLARSYSTEM_QUALITY=\"%s\"; using \"full\".", requested);
    return 2;
}
#endif
