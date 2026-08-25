#include "WasmExports.h"
#include "Application.h"
#include "JsBridge.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace {
    Application* activeApplication = nullptr;

    float g_touchForward = 0.0f;
    float g_touchRight = 0.0f;
    float g_touchVertical = 0.0f;

    void ApplyMagneticFieldMode(int enabled) {
        if (activeApplication) {
            activeApplication->SetMagneticFieldsEnabled(enabled != 0);
        }
        NotifySettingsChanged("magneticFields");
        NotifySettingsChanged("magneticFieldMode");
    }
}

Application* GetActiveApplication() {
    return activeApplication;
}

void SetActiveApplication(Application* application) {
    activeApplication = application;
}

#ifdef __EMSCRIPTEN__
extern "C" {
    // Keep this small control surface as an explicit C ABI consumed through
    // cwrap. If the web API grows to return planet lists or settings structs,
    // migrate those structured values to embind instead of adding pointer-based
    // C exports; these scalar settings remain simpler as cwrap calls.
    EMSCRIPTEN_KEEPALIVE void SetCameraPose(float x, float y, float z, float yaw, float pitch) {
        if (!activeApplication) return;
        Camera& cam = activeApplication->GetCamera();
        cam.SetPosition(glm::vec3(x, y, z));
        cam.SetYawPitch(yaw, pitch);
    }
    EMSCRIPTEN_KEEPALIVE void SetQualityPreset(int preset) {
        gSimState->qualityPreset = (preset < 0 ? 0 : preset > 2 ? 2 : preset);
        if (activeApplication) {
            activeApplication->ApplyQualityPreset(gSimState->qualityPreset);
        }
        NotifySettingsChanged("quality");
    }
    EMSCRIPTEN_KEEPALIVE int GetQualityPreset() {
        return gSimState->qualityPreset;
    }
    EMSCRIPTEN_KEEPALIVE void SetTimeScale(float scale) {
        gSimState->timeScale = glm::clamp(scale, 0.01f, 10000.0f);
        NotifySettingsChanged("timeScale");
    }
    EMSCRIPTEN_KEEPALIVE float GetTimeScale() {
        return gSimState->timeScale;
    }
    EMSCRIPTEN_KEEPALIVE void SetPaused(bool paused) {
        gSimState->timePaused = paused;
        NotifySettingsChanged("paused");
    }
    EMSCRIPTEN_KEEPALIVE int GetPaused() {
        return gSimState->timePaused ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void SetSimulationEpoch(double julianDate) {
        OrbitLayout::SetJulianDate(julianDate);
        NotifySettingsChanged("simulationEpoch");
    }
    EMSCRIPTEN_KEEPALIVE double GetSimulationEpoch() {
        return OrbitLayout::GetJulianDate();
    }
    EMSCRIPTEN_KEEPALIVE void SetShadowQuality(int quality) {
        if (activeApplication) {
            activeApplication->ApplyShadowQuality(quality);
        } else {
            gSimState->shadowQuality = std::clamp(quality, 0, 3);
        }
        NotifySettingsChanged("shadowQuality");
    }
    EMSCRIPTEN_KEEPALIVE int GetShadowQuality() {
        return gSimState->shadowQuality;
    }
    EMSCRIPTEN_KEEPALIVE void SetTouchMovement(float forward, float right, float vertical) {
        g_touchForward = glm::clamp(forward, -1.0f, 1.0f);
        g_touchRight = glm::clamp(right, -1.0f, 1.0f);
        g_touchVertical = glm::clamp(vertical, -1.0f, 1.0f);
    }
    EMSCRIPTEN_KEEPALIVE void AddTouchLook(float deltaX, float deltaY) {
        if (activeApplication) {
            activeApplication->GetCamera().ProcessMouseMovement(deltaX, deltaY);
        }
    }
    EMSCRIPTEN_KEEPALIVE void AddTouchZoom(float delta) {
        if (activeApplication && std::abs(delta) > 0.0001f) {
            activeApplication->GetCamera().ProcessMouseScroll(delta);
        }
    }
    EMSCRIPTEN_KEEPALIVE int IsMobileWeb() {
        return gSimState->isMobileWeb ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void SetMusicVolume(float volume) {
        if (activeApplication) {
            activeApplication->SetMusicVolume(volume);
        }
        NotifySettingsChanged("musicVolume");
    }
    EMSCRIPTEN_KEEPALIVE float GetMusicVolume() {
        return activeApplication ? activeApplication->GetMusicVolume() : 0.3f;
    }
    EMSCRIPTEN_KEEPALIVE void SetMusicMuted(int muted) {
        if (activeApplication) {
            activeApplication->SetMusicMuted(muted != 0);
        }
        NotifySettingsChanged("musicMuted");
    }
    EMSCRIPTEN_KEEPALIVE int GetMusicMuted() {
        return activeApplication && activeApplication->GetMusicMuted() ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void FocusPlanet(int idx) {
        if (activeApplication) {
            activeApplication->FocusPlanetByIndex(idx);
        }
    }
    EMSCRIPTEN_KEEPALIVE void SetOrbitScaleMode(int mode) {
        if (activeApplication) {
            activeApplication->ApplyOrbitScaleMode(mode);
        } else {
            OrbitLayout::SetScaleMode(mode == 1 ? OrbitLayout::ScaleMode::Realistic : OrbitLayout::ScaleMode::Compressed);
        }
    }
    EMSCRIPTEN_KEEPALIVE int GetOrbitScaleMode() {
        return static_cast<int>(OrbitLayout::GetScaleMode());
    }
    EMSCRIPTEN_KEEPALIVE int GetNearestPlanetIndex() {
        return activeApplication ? activeApplication->GetNearestPlanetIndexForJs() : -1;
    }
    EMSCRIPTEN_KEEPALIVE int GetFocusedPlanetIndex() {
        return activeApplication ? activeApplication->GetFocusedPlanetIndex() : -1;
    }
    EMSCRIPTEN_KEEPALIVE float GetPlanetSceneDistance(int idx) {
        idx = std::clamp(idx, 0, 9);
        return OrbitLayout::GetSceneDistance(static_cast<OrbitLayout::Body>(idx));
    }
    EMSCRIPTEN_KEEPALIVE void SetOrbitLines(int enabled) {
        if (activeApplication) {
            activeApplication->SetOrbitLinesEnabled(enabled != 0);
        }
        NotifySettingsChanged("orbitLines");
    }
    EMSCRIPTEN_KEEPALIVE int GetOrbitLines() {
        return activeApplication && activeApplication->GetOrbitLinesEnabled() ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void SetMagneticFields(int enabled) {
        ApplyMagneticFieldMode(enabled);
    }
    EMSCRIPTEN_KEEPALIVE int GetMagneticFields() {
        return activeApplication && activeApplication->GetMagneticFieldsEnabled() ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void SetMagneticFieldMode(int enabled) {
        ApplyMagneticFieldMode(enabled);
    }
    EMSCRIPTEN_KEEPALIVE int GetMagneticFieldMode() {
        return activeApplication && activeApplication->GetMagneticFieldsEnabled() ? 1 : 0;
    }

    // --- WebXR stereo control surface ---
    EMSCRIPTEN_KEEPALIVE void SetXrSessionActive(int active) {
        if (!activeApplication) {
            return;
        }
        activeApplication->SetXrActive(active != 0);
        if (active) {
            emscripten_pause_main_loop();
        } else {
            emscripten_resume_main_loop();
        }
    }
    EMSCRIPTEN_KEEPALIVE void SetXrEyeCount(int count) {
        if (activeApplication) {
            activeApplication->SetXrEyeCount(count);
        }
    }
    EMSCRIPTEN_KEEPALIVE void SetXrEyeViewport(int eye, int x, int y, int width, int height) {
        if (activeApplication) {
            activeApplication->SetXrEyeViewport(eye, x, y, width, height);
        }
    }
    EMSCRIPTEN_KEEPALIVE float* GetXrMatrixScratch() {
        return activeApplication ? activeApplication->GetXrMatrixScratch() : nullptr;
    }
    EMSCRIPTEN_KEEPALIVE void CommitXrEyeMatrices(int eye) {
        if (activeApplication) {
            activeApplication->CommitXrEyeMatrices(eye);
        }
    }
    EMSCRIPTEN_KEEPALIVE void RunXrFrame() {
        if (activeApplication) {
            activeApplication->RunOneFrame();
        }
    }
    EMSCRIPTEN_KEEPALIVE float GetCameraPositionX() {
        return activeApplication ? activeApplication->GetCamera().GetPosition().x : 0.0f;
    }
    EMSCRIPTEN_KEEPALIVE float GetCameraPositionY() {
        return activeApplication ? activeApplication->GetCamera().GetPosition().y : 0.0f;
    }
    EMSCRIPTEN_KEEPALIVE float GetCameraPositionZ() {
        return activeApplication ? activeApplication->GetCamera().GetPosition().z : 0.0f;
    }
    EMSCRIPTEN_KEEPALIVE float GetCameraYaw() {
        return activeApplication ? activeApplication->GetCamera().GetYaw() : -90.0f;
    }
    EMSCRIPTEN_KEEPALIVE float GetCameraPitch() {
        return activeApplication ? activeApplication->GetCamera().GetPitch() : 0.0f;
    }
}

#ifdef __EMSCRIPTEN__
float GetTouchForward() {
    return g_touchForward;
}

float GetTouchRight() {
    return g_touchRight;
}

float GetTouchVertical() {
    return g_touchVertical;
}
#endif
#endif
