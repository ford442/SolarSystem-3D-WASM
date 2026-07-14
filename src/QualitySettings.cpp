#include "Application.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include <algorithm>
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

void Application::UpdateLOD() {
#ifdef __EMSCRIPTEN__
    if (_renderableSceneComponents.empty()) return;

    const glm::vec3 camPos = camera.GetPosition();

    if (g_qualityPreset == 0) {
        // Low preset: force downgrade/cancel every high-res scene texture and skip upgrades.
        const glm::vec3 fakeFar = camPos + glm::vec3(100000.0f, 0.0f, 0.0f);
        for (auto& rc : _renderableSceneComponents) {
            if (rc.planet) rc.planet->LoadHighResIfClose(fakeFar);
            for (auto& satellite : rc.satellites) satellite->LoadHighResIfClose(fakeFar);
            if (rc.planetaryRing) rc.planetaryRing->LoadHighResIfClose(fakeFar);
            if (rc.clouds) rc.clouds->LoadHighResIfClose(fakeFar);
        }
        return;
    }

    // Call on *all* ready planets: far ones will downgrade if loaded + past hysteresis;
    // near ones will upgrade if appropriate.
    for (auto& rc : _renderableSceneComponents) {
        if (rc.planet) {
            rc.planet->LoadHighResIfClose(camPos);
        }
        for (auto& satellite : rc.satellites) {
            satellite->LoadHighResIfClose(camPos);
        }
        if (rc.planetaryRing) {
            rc.planetaryRing->LoadHighResIfClose(camPos);
        }
        if (rc.clouds) {
            rc.clouds->LoadHighResIfClose(camPos);
        }
    }
#endif
}

void Application::ApplyQualityPreset(int preset) {
    g_qualityPreset = std::clamp(preset, 0, 2);
    const auto settings = GetQualitySettings(g_qualityPreset, g_isMobileWeb);

    TextureLoadingQueue::GetInstance().SetMaxConcurrentLoads(settings.maxConcurrentTextureLoads);

    if (gShadowQuality > 0) {
        gShadowQuality = g_qualityPreset + 1;
    }

    ApplyRenderResources(settings.shadowResolution, settings.enableHdr);
    LogQualityTier(settings, _hdrEnabled, gShadowQuality);
}

void Application::ApplyRenderResources(uint16_t shadowResolution, bool enableHdr) {
    _hdrEnabled = enableHdr;

    if (_shadowMapFBO && gShadowQuality > 0) {
        _shadowMapFBO->Resize(shadowResolution, shadowResolution);
    }

    if (_hdr) {
        _hdr->SetEnabled(enableHdr, _displayWidth, _displayHeight);
    }
}

void Application::ApplyShadowQuality(int quality) {
    gShadowQuality = std::clamp(quality, 0, 3);
    if (gShadowQuality == 0) {
        std::cout << "[Shadows] disabled" << std::endl;
        return;
    }

    const auto settings = GetQualitySettings(gShadowQuality - 1, g_isMobileWeb);
    if (_shadowMapFBO) {
        _shadowMapFBO->Resize(settings.shadowResolution, settings.shadowResolution);
    }
    ApplyRenderResources(settings.shadowResolution, settings.enableHdr);
    LogQualityTier(settings, _hdrEnabled, gShadowQuality);
}
