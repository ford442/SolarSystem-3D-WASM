// Runtime-adjustable rendering settings: quality/shadow presets and the GPU resources
// they resize, the orbit distance scale mode, and the texture LOD manager that trades
// high-res textures in and out as the camera moves.
#include "Application.h"
#include "QualitySettings.h"
#include "assets/TextureLoadingQueue.h"
#include "Solar_System/OrbitLayout.h"
#include "core/Log.h"
#include <algorithm>

void Application::ApplyQualityPreset(int preset) {
    _simState.qualityPreset = std::clamp(preset, 0, 2);
    const auto settings = GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb);

    TextureLoadingQueue::GetInstance().SetMaxConcurrentLoads(settings.maxConcurrentTextureLoads);

    if (_simState.shadowQuality > 0) {
        _simState.shadowQuality = _simState.qualityPreset + 1;
    }

    _renderer.ApplyRenderResources(_simState, settings.shadowResolution, settings.enableHdr, DisplaySize());
    _renderer.ApplyEffectQuality(settings);
    if (_asteroidField) {
        _asteroidField->SetInstanceCount(settings.asteroidInstanceCount);
    }
    if (_kuiperField) {
        _kuiperField->SetInstanceCount(settings.kuiperInstanceCount);
    }
    _renderer.RebuildMissionPaths(_missionCatalog, _simState.qualityPreset);
    _renderer.RebuildMagneticFields(_simState.qualityPreset);
    LogQualityTier(settings, _renderer.HdrEnabled(), _simState.shadowQuality);
}

void Application::ApplyShadowQuality(int quality) {
    _simState.shadowQuality = std::clamp(quality, 0, 3);
    if (_simState.shadowQuality == 0) {
        LOG_INFO("Shadows", "disabled");
        return;
    }

    const auto settings = GetQualitySettings(_simState.shadowQuality - 1, _simState.isMobileWeb);
    _renderer.ResizeShadowMap(settings.shadowResolution);
    _renderer.ApplyRenderResources(_simState, settings.shadowResolution, settings.enableHdr, DisplaySize());
    LogQualityTier(settings, _renderer.HdrEnabled(), _simState.shadowQuality);
}

void Application::ApplyOrbitScaleMode(int mode) {
    const auto scaleMode = mode == 1 ? OrbitLayout::ScaleMode::Realistic : OrbitLayout::ScaleMode::Compressed;
    // The camera keeps its place relative to the body it is nearest, in that body's radii, so
    // the view survives the bodies resizing and moving under it (docs/ARCHITECTURE.md §11.1).
    const CameraBodyFrame frame = CaptureCameraBodyFrame();
    OrbitLayout::SetScaleMode(scaleMode);
    PlaceBodies();
    RestoreCameraBodyFrame(frame);
    UpdateRenderOrigin();
    RefreshPlanetProxyPositions();
    // Belt instance matrices follow the new AU→scene mapping at the next UpdateScene.
    _renderer.RebuildMissionPaths(_missionCatalog, _simState.qualityPreset);
    LOG_INFO("OrbitScale", "%s distances active",
             scaleMode == OrbitLayout::ScaleMode::Realistic ? "realistic" : "compressed");
}

void Application::UpdateLOD() {
#ifdef __EMSCRIPTEN__
    if (_renderableSceneComponents.empty()) return;

    const glm::vec3 camPos = CameraRenderPosition();
    const TextureLodPolicy policy = GetTextureLodPolicy(_simState.qualityPreset, _simState.isMobileWeb);

    if (_simState.qualityPreset == 0) {
        // Low preset: force downgrade/cancel every high-res scene texture and skip upgrades.
        const glm::vec3 fakeFar = camPos + glm::vec3(100000.0f, 0.0f, 0.0f);
        for (auto& rc : _renderableSceneComponents) {
            if (rc.planet) rc.planet->LoadHighResIfClose(fakeFar, policy);
            for (auto& satellite : rc.satellites) satellite->LoadHighResIfClose(fakeFar, policy);
            if (rc.planetaryRing) rc.planetaryRing->LoadHighResIfClose(fakeFar, policy);
            if (rc.clouds) rc.clouds->LoadHighResIfClose(fakeFar, policy);
        }
        return;
    }

    // Call on *all* ready planets: far ones will downgrade if loaded + past hysteresis;
    // near ones will upgrade if appropriate.
    for (auto& rc : _renderableSceneComponents) {
        if (rc.planet) {
            rc.planet->LoadHighResIfClose(camPos, policy);
        }
        for (auto& satellite : rc.satellites) {
            satellite->LoadHighResIfClose(camPos, policy);
        }
        if (rc.planetaryRing) {
            rc.planetaryRing->LoadHighResIfClose(camPos, policy);
        }
        if (rc.clouds) {
            rc.clouds->LoadHighResIfClose(camPos, policy);
        }
    }
#endif
}
