// Runtime-adjustable rendering settings: quality/shadow presets and the GPU resources
// they resize, the orbit distance scale mode, and the texture LOD manager that trades
// high-res textures in and out as the camera moves.
#include "Application.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include "Solar_System/OrbitLayout.h"
#include <algorithm>
#include <iostream>

void Application::ApplyQualityPreset(int preset) {
    gSimState->qualityPreset = std::clamp(preset, 0, 2);
    const auto settings = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);

    TextureLoadingQueue::GetInstance().SetMaxConcurrentLoads(settings.maxConcurrentTextureLoads);

    if (gSimState->shadowQuality > 0) {
        gSimState->shadowQuality = gSimState->qualityPreset + 1;
    }

    ApplyRenderResources(settings.shadowResolution, settings.enableHdr);
    if (_asteroidField) {
        _asteroidField->SetInstanceCount(settings.asteroidInstanceCount);
    }
    if (_magneticFieldsEnabled) {
        _magneticFieldsBuilt = false;
        EnsureMagneticFieldsBuilt();
    }
    LogQualityTier(settings, _hdrEnabled, gSimState->shadowQuality);
}

void Application::ApplyRenderResources(uint16_t shadowResolution, bool enableHdr) {
    _hdrEnabled = enableHdr;

    if (_shadowMapFBO && gSimState->shadowQuality > 0) {
        _shadowMapFBO->Resize(shadowResolution, shadowResolution);
    }

    if (_hdr) {
        _hdr->SetEnabled(enableHdr, _displayWidth, _displayHeight);
    }
    if (_magneticFieldBloom) {
        const auto bloomSettings = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);
        _magneticFieldBloom->SetEnabled(bloomSettings.enableMagneticBloom, _displayWidth, _displayHeight);
    }
}

void Application::ApplyShadowQuality(int quality) {
    gSimState->shadowQuality = std::clamp(quality, 0, 3);
    if (gSimState->shadowQuality == 0) {
        std::cout << "[Shadows] disabled" << std::endl;
        return;
    }

    const auto settings = GetQualitySettings(gSimState->shadowQuality - 1, gSimState->isMobileWeb);
    if (_shadowMapFBO) {
        _shadowMapFBO->Resize(settings.shadowResolution, settings.shadowResolution);
    }
    ApplyRenderResources(settings.shadowResolution, settings.enableHdr);
    LogQualityTier(settings, _hdrEnabled, gSimState->shadowQuality);
}

void Application::ApplyOrbitScaleMode(int mode) {
    const auto scaleMode = mode == 1 ? OrbitLayout::ScaleMode::Realistic : OrbitLayout::ScaleMode::Compressed;
    OrbitLayout::SetScaleMode(scaleMode);
    RefreshPlanetProxyPositions();
    if (_asteroidField) {
        _asteroidField->Update(0.0f); // rebuild instance matrices for new AU→scene mapping
    }
    std::cout << "[OrbitScale] " << (scaleMode == OrbitLayout::ScaleMode::Realistic ? "realistic" : "compressed")
              << " distances active" << std::endl;
}

void Application::UpdateLOD() {
#ifdef __EMSCRIPTEN__
    if (_renderableSceneComponents.empty()) return;

    const glm::vec3 camPos = _camera.GetPosition();

    if (gSimState->qualityPreset == 0) {
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
