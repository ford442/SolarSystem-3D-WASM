#ifndef SOLARSYSTEM_FRAMECONTEXT_H
#define SOLARSYSTEM_FRAMECONTEXT_H
#include "ApplicationTypes.h"
#include "ObserveState.h"
#include "PlanetSystemManifest.h"
#include "SimState.h"
#include "Auxiliary_Modules/MissionCatalog.h"
#include "Auxiliary_Modules/Observer.h"
#include "Auxiliary_Modules/SkyEvents.h"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

class AsteroidField;
class Star;

/**
 * Everything a render pass is allowed to know, handed to Renderer explicitly instead of the
 * renderer reaching into Application (it used to be a friend). Application::RenderFrame
 * builds one SceneSnapshot per frame and one CameraView per view (mono, or each XR eye).
 */

/** One view's camera: the mono camera, or one XR eye (its own matrices, the head's axes). */
struct CameraView {
    glm::mat4 projection{1.0f};
    glm::mat4 view{1.0f};
    glm::vec3 eyePosition{0.0f};  // this eye, render frame (inverse(view)[3])
    glm::vec3 headPosition{0.0f}; // Camera::GetPosition() — the head, between the eyes
    glm::vec3 front{0.0f, 0.0f, -1.0f}, right{1.0f, 0.0f, 0.0f}, up{0.0f, 1.0f, 0.0f}; // head axes
    float nearPlane = 0.001f;
    float farPlane = 20000.0f;
    float aspect = 1.0f;
    float zoomDeg = 45.0f; // Camera::GetZoom(), the mono vertical FOV
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float zCoef = 1.0f; // 2 / log2(far + 1): the log-depth scale every lit vertex shader uses
    glm::ivec4 viewport{0}; // x, y, width, height
    int eye = -1;           // -1 mono, 0/1 XR eye
};

/** The scene as this frame's passes see it. Placement already happened in UpdateScene. */
struct SceneSnapshot {
    // Non-const: passes still bind their shader on the object and read back the Sun's
    // occlusion query. They do not move anything.
    Star* sun = nullptr;
    std::vector<RenderableSceneComponent>* components = nullptr;
    int nearestComponent = -1; // into *components; -1 none
    const AsteroidField* asteroidField = nullptr;
    const AsteroidField* kuiperField = nullptr;

    const MissionCatalog::Catalog* missions = nullptr;
    int focusedMission = -1;
    // Probe position per mission at this frame's date (render frame); nullopt off its trajectory.
    const std::vector<std::optional<glm::vec3>>* missionProbes = nullptr;
    // Web staged loading: systems not READY yet get a proxy label.
    const std::vector<PlanetSystemManifest>* stagedSystems = nullptr;

    bool observeActive = false;
    const ObserveState* observe = nullptr;
    const Observer::Sky* observeSky = nullptr;
    unsigned int observeMoonTexture = 0;
    const SkyEvents::SkyEvent* nextSkyEvent = nullptr;

    const RenderableSceneComponent* Nearest() const {
        if (!components || nearestComponent < 0 || static_cast<size_t>(nearestComponent) >= components->size()) {
            return nullptr;
        }
        return &(*components)[static_cast<size_t>(nearestComponent)];
    }
};

/** What the hint overlay prints that is not scene or camera state. */
struct HudState {
    uint16_t fps = 0;
    glm::vec3 fpsColor{1.0f};
    bool vsync = true;
    float cameraSpeed = 0.0f;
    float musicVolumePercent = -1.0f; // native only; < 0 hides the line
    std::string musicTrack;
};

struct FrameContext {
    const CameraView& view;
    const SceneSnapshot& scene;
    const SimState& sim;
    glm::ivec2 display{0}; // window framebuffer size: HUD projection, screen-sized targets
    bool xr = false;
    unsigned int defaultFramebuffer = 0; // FBO 0, or the XRWebGLLayer framebuffer under WebXR
    const HudState* hud = nullptr;
};

#endif // SOLARSYSTEM_FRAMECONTEXT_H
