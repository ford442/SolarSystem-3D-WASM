#ifndef SOLARSYSTEM_FRAMECONTEXT_H
#define SOLARSYSTEM_FRAMECONTEXT_H
#include "ApplicationTypes.h"
#include "ObserveState.h"
#include "PlanetSystemManifest.h"
#include "SimState.h"
#include "astro/MissionCatalog.h"
#include "astro/Observer.h"
#include "astro/SkyEvents.h"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class AsteroidField;
class Atmosphere;
class SpaceObject;
class Star;
namespace Terrain {
struct ActiveBody;
}

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
    glm::vec3 headPosition{0.0f}; // the head (between the eyes), render frame: zero, the origin sits on it
    glm::dvec3 renderOrigin{0.0}; // world position of the render frame's origin (OrbitLayout::GetRenderOrigin)
    glm::vec3 front{0.0f, 0.0f, -1.0f}, right{1.0f, 0.0f, 0.0f}, up{0.0f, 1.0f, 0.0f}; // head axes
    float nearPlane = 0.001f;
    float farPlane = 20000.0f;
    float aspect = 1.0f;
    float zoomDeg = 45.0f; // Camera::GetZoom(), the mono vertical FOV
    float yawDeg = 0.0f;
    float pitchDeg = 0.0f;
    float logDepthC = 1.0f; // C in log2(1 + C·w) (common/log_depth.glsl): 1 compressed, 1/near realistic
    float zCoef = 1.0f;     // 2 / log2(C·far + 1): the log-depth scale every lit shader uses
    glm::ivec4 viewport{0}; // x, y, width, height
    int eye = -1;           // -1 mono, 0/1 XR eye
};

/**
 * Space weather this frame (Application::UpdateSpaceWeather, docs/ARCHITECTURE.md §13): what
 * SpaceWeather::State derived from the date, mapped into the scene, plus which particle groups
 * are worth drawing. Renderer packs it into the MagnetosphereUBO.
 */
struct SpaceWeatherFrame {
    struct Body {
        int slot = 0;                     // SpaceWeather::Magnetospheres() index: UBO slot, particle group - 1
        int body = 0;                     // OrbitLayout::Body
        glm::dvec3 heliocentric{0.0};     // scene units
        glm::vec3 render{0.0f};           // render frame
        float radius = 1.0f;              // drawn (scene units)
        glm::vec3 pole{0.0f, 1.0f, 0.0f}; // magnetic north, scene axes
        glm::vec3 sunward{1.0f, 0.0f, 0.0f};
        float standoff = 10.0f; // drawn radii
        float quietStandoff = 10.0f;
        float flaring = 0.58f; // Shue α
        float kp = 2.0f;
        float windRatio = 1.0f; // wind speed / 400 km/s
        float cmeStrength = 0.0f;
        float densityRatio = 1.0f; // over the quiet density at this distance
        glm::vec4 ovalRad{0.0f};   // equatorward midnight, noon; poleward midnight, noon
        // render.aurora (Mercury, which has none, keeps these: its precipitation is drawn only).
        glm::vec3 colorLow{0.8f, 0.85f, 1.0f};
        glm::vec3 colorHigh{0.8f, 0.85f, 1.0f};
        glm::vec3 colorEdge{0.8f, 0.85f, 1.0f};
        float brightness = 0.0f;
        float shellBottom = 1.0f; // the emitting layer, body radii from the centre
        float shellTop = 1.0f;
        bool groupActive = false; // its particle group runs this frame
    };
    struct Cme {
        glm::vec3 direction{1.0f, 0.0f, 0.0f};
        float frontAu = 0.0f;
        float cosEdge = 0.0f;
        float cosCore = 1.0f;
        float speedRatio = 1.0f; // front speed / 400 km/s
        float brightness = 1.0f;
    };

    bool windEnabled = false;
    int particles = 0;       // the tier's budget (0 with the wind off)
    int auroraMode = 0;      // QualityTierSettings::auroraMode
    int photosphereMode = 0; // QualityTierSettings::photosphereMode
    bool backgroundActive = false;
    float visualDt = 0.0f; // seconds the particles advance this frame (0 paused)
    float visualClock = 0.0f;
    float auPerSecond = 0.0f; // background wind at 400 km/s, AU per visual second
    float sunRotation = 0.0f; // rad per visual second, so the spiral winds as it does at 1 AU
    float edgeAu = 6.5f;
    float spawnAu = 0.01f;
    float cyclePhase = 0.0f;
    float beltHalfWidthRad = 0.35f; // the slow wind's streamer belt
    unsigned frameCounter = 0;
    std::vector<glm::vec2> knots; // OrbitLayout's AU → scene distance map: (AU, scene)
    std::vector<Body> bodies;     // Magnetospheres() order; a body whose system is not loaded is absent
    std::vector<Cme> cmes;

    // The Sun's photosphere (star.fs PHOTOSPHERE / SUNSPOTS).
    glm::vec4 spotBandsRad{0.0f}; // this cycle's latitude, half-width; the previous cycle's
    glm::vec2 spotCoverage{0.0f};
    float spotDays = 0.0f; // days since the cycle's minimum

    const Body* Find(int body) const {
        for (const Body& b : bodies) {
            if (b.body == body) {
                return &b;
            }
        }
        return nullptr;
    }
};

/**
 * A planned flight as the mission-paths pass draws it (FlightController, docs/ARCHITECTURE.md
 * §14): the whole path in heliocentric scene units (re-uploaded when `version` changes), how
 * much of it is flown, and the craft in the render frame.
 */
struct FlightFrame {
    std::uint32_t version = 0; // bumped when the path (plan or scale mode) changes
    const std::vector<glm::dvec3>* path = nullptr;
    std::size_t flownSamples = 0; // path[0, flownSamples) is behind the craft
    glm::dvec3 craftWorld{0.0};   // heliocentric scene units (the trail's last point)
    glm::vec3 craftRender{0.0f};
    bool craftVisible = false;
    glm::vec3 color{0.35f, 0.95f, 0.75f};
};

/** The scene as this frame's passes see it. Placement already happened in UpdateScene. */
struct SceneSnapshot {
    // Non-const for the Sun's occlusion query, which a pass reads back. Nothing moves.
    Star* sun = nullptr;
    std::vector<RenderableSceneComponent>* components = nullptr;
    // Indices into *components to draw this frame, front to back (Application::CullScene).
    const std::vector<std::size_t>* drawOrder = nullptr;
    int nearestComponent = -1; // into *components; -1 none
    const AsteroidField* asteroidField = nullptr;
    const AsteroidField* kuiperField = nullptr;

    const MissionCatalog::Catalog* missions = nullptr;
    int focusedMission = -1;
    // Probe position per mission at this frame's date (render frame); nullopt off its trajectory.
    const std::vector<std::optional<glm::vec3>>* missionProbes = nullptr;
    // The planned flight; null when there is none (or outside Sol).
    const FlightFrame* flight = nullptr;
    // Web staged loading: systems not READY yet get a proxy label.
    const std::vector<PlanetSystemManifest>* stagedSystems = nullptr;

    bool observeActive = false;
    const ObserveState* observe = nullptr;
    const Observer::Sky* observeSky = nullptr;
    unsigned int observeMoonTexture = 0;
    // Earth's shell once its LUTs are resident: Observe's daytime sky. Null draws the gradient.
    const Atmosphere* observeEarthAtmosphere = nullptr;
    const SkyEvents::SkyEvent* nextSkyEvent = nullptr;
    // Null outside Sol (the magnetospheres are Sol's) and before the first update.
    const SpaceWeatherFrame* spaceWeather = nullptr;

    // Bodies flagged render.terrain that the camera is near (src/render/terrain/). `tiles`
    // is this frame's selection when the body draws as tiles instead of its sphere; null
    // while it is only close enough for the renderer to get its terrain programs ready.
    struct TerrainBody {
        const SpaceObject* body = nullptr;
        const Terrain::ActiveBody* tiles = nullptr;
    };
    std::vector<TerrainBody> terrain;

    const TerrainBody* TerrainFor(const SpaceObject* body) const {
        for (const TerrainBody& entry : terrain) {
            if (entry.body == body) {
                return &entry;
            }
        }
        return nullptr;
    }

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
    // The star-system jump's fade over the whole frame (0 = none).
    glm::vec3 fadeColor{0.0f};
    float fadeAmount = 0.0f;
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
