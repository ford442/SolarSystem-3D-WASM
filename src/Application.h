#ifndef SOLARSYSTEM_APPLICATION_H
#define SOLARSYSTEM_APPLICATION_H
#include "ApplicationTypes.h"
#include "ObserveState.h"
#include "FrameContext.h"
#include "FramePasses.h"
#include "Renderer.h"
#include "SimState.h"
#include "astro/MissionCatalog.h"
#include "astro/Observer.h"
#include "astro/SkyEvents.h"
#include "PlanetSystemManifest.h"
#include "Solar_System/AsteroidField.h"
#include "Solar_System/SolarSystem.h"
#include "SystemModules.h"
#include "XrState.h"
#include "core/Camera.h"
#include "core/CameraNavigation.h"
#include "core/FrameTimer.h"
#include "gfx/MeshHolder.h"
#include "gfx/ShadowFit.h"
#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

namespace Terrain {
class TerrainSystem;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

// Web has no native audio backend: the music bed is an HTMLAudioElement in web/src/music.ts.
#ifdef SOLARSYSTEM_USE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#elif !defined(__EMSCRIPTEN__)
#include <irrKlang.h>
#endif

class Application {
public:
    Application();
    ~Application();
    void Exec();
    void RunOneFrame(); // For Emscripten main loop / XR RAF
#ifdef __EMSCRIPTEN__
    void SetXrActive(bool active);
    bool IsXrActive() const { return _xr.active; }
    void SetXrEyeCount(int count);
    void SetXrBaseLayerFramebuffer(unsigned int framebuffer);
    void SetXrEyeViewport(int eye, int x, int y, int width, int height);
    float* GetXrMatrixScratch();
    void CommitXrEyeMatrices(int eye);
#endif
    /**
     * Framebuffer the frame composites into. FBO 0 normally; under WebXR the
     * XRWebGLLayer framebuffer, which JS binds before handing the frame to us.
     */
    GLuint DefaultFramebuffer() const {
#ifdef __EMSCRIPTEN__
        return _xr.active ? static_cast<GLuint>(_xr.baseLayerFramebuffer) : 0u;
#else
        return 0u;
#endif
    }
    void ApplyQualityPreset(int preset);
    void ApplyShadowQuality(int quality);
    void ApplyOrbitScaleMode(int mode);
    void FocusPlanetByIndex(int idx);
    int GetFocusedPlanetIndex() const;
    void FocusMissionByIndex(int idx);
    int GetFocusedMissionIndex() const;
    int GetMissionCount() const;
    std::string GetMissionCatalogJson() const;
    std::string GetFocusedMissionJson() const;
    void SetXrControllerRay(int hand, float ox, float oy, float oz, float dx, float dy, float dz, int visible);
    /**
     * Next inner-planet conjunction at the current simulation epoch. Cached: the search costs
     * a couple of milliseconds, so it only re-runs when the event passes or time is scrubbed
     * backwards, not every frame.
     */
    const SkyEvents::Conjunction& GetNextConjunction() const;
    /**
     * Next sky event of any kind (conjunction, eclipse, transit, shadow transit) at the
     * current simulation epoch. Cached on the same terms as GetNextConjunction() — the
     * multi-kind search is the more expensive of the two, so it must not run per frame.
     */
    const SkyEvents::SkyEvent& GetNextSkyEvent() const;
    int GetNearestPlanetIndexForJs() const;

    // Observe mode — camera on Earth at a lat/lon, sky from Observer::ComputeSky (ObserveMode.cpp).
    void SetObserveMode(bool active);
    bool IsObserveActive() const { return _observe.active; }
    const ObserveState& GetObserveState() const { return _observe; }
    void SetObserverSite(double latDeg, double lonDeg, double altM);
    /** Azimuth from north through east, elevation above the horizon, both in degrees. */
    void SetObserveView(float azimuthDeg, float elevationDeg);
    void SetObserveFov(float fovDeg);
    void SetObserveTimeRate(double rate);
    void SetObserveConstellations(bool visible);
    /** Sky for the current epoch and site; recomputed only when either changes. */
    const Observer::Sky& CurrentObserveSky() const;
    std::string GetObserveStateJson() const;
    /** GL name of the Moon's diffuse texture once its system is loaded, else 0 (Observe draws it on the Moon disc). */
    unsigned int ObserveMoonTexture() const;
    /** Earth's atmosphere shell once its system is loaded and its LUTs are resident, else null (Observe's daytime sky). */
    const Atmosphere* ObserveEarthAtmosphere() const;
#ifndef __EMSCRIPTEN__
    // On web, volume/mute live in web/src/music.ts; C++ only reports MusicDuckAmount().
    void SetMusicVolume(float volume);
    float GetMusicVolume() const;
    void SetMusicMuted(bool muted);
    bool GetMusicMuted() const;
#endif
    void SetOrbitLinesEnabled(bool enabled);
    bool GetOrbitLinesEnabled() const;
    void SetMagneticFieldsEnabled(bool enabled);
    bool GetMagneticFieldsEnabled() const;

    // --- Timelines and capture (Cinematics.cpp; web/src/timeline.ts, web/src/capture.ts) --
    /**
     * Start an offline capture: frames now come only from StepCaptureFrame, each a fixed step
     * of the animation clock (reset to 0 here) rather than of the wall clock. A `width` ×
     * `height` above 0 renders at that size (web: the canvas backing store) until EndCapture.
     */
    void BeginCapture(int width, int height);
    /** Draw one capture frame `deltaSeconds` after the previous one (0 redraws the same instant). */
    void StepCaptureFrame(double deltaSeconds);
    void EndCapture();
    bool IsCaptureActive() const { return _capture.active; }
    /** Textures queued or uploading, plus planet systems still downloading. */
    int PendingAssetCount() const;
    /** The Sun and every catalog row as JSON: index, id, name, parent id, kind, radiusKm. */
    std::string GetBodyCatalogJson() const;
    /**
     * Every body's heliocentric position and drawn radius at the current date and scale, and
     * each mission probe's position, as JSON (web/src/timeline.ts reads it). Places the bodies
     * first, so it answers for a date set since the last frame.
     */
    std::string GetSceneStateJson();
    /** Where a body's drawn surface is along a world direction from its centre. */
    struct SurfacePoint {
        double radius;     // centre to surface, scene units
        glm::dvec3 normal; // outward unit normal there, world frame
    };
    /**
     * The drawn surface along a world direction: the mesh facet the camera floor measures,
     * once the body is loaded; its analytic sphere (radius, and the direction as the normal)
     * before. Reads the placement GetSceneStateJson or the last frame left.
     */
    SurfacePoint SurfaceAlong(int catalogIndex, const glm::dvec3& direction) const;
    /** Start downloading the system a catalog body belongs to (web staged loading), without moving the camera. */
    void PreloadBodySystem(int catalogIndex);
    /**
     * Text overlays as a mask: bit 0 the hint list, bit 1 planet/star distances, bit 2 moon
     * distances, bit 3 the staged-loading labels at systems not loaded yet.
     */
    void SetHudOverlays(int mask);
    int GetHudOverlays() const;
    /**
     * Place the fly camera at a heliocentric scene position (deep links, tours, leaving
     * Observe). It stops co-moving with whatever body it was riding along with.
     */
    void SetCameraHeliocentricPose(const glm::dvec3& position, float yaw, float pitch);
    /** The camera's heliocentric scene position (Camera::GetWorldPosition). */
    glm::dvec3 GetCameraHeliocentricPosition() const;
    /** The camera in the render frame: zero once MoveRenderOrigin has put the origin on it. */
    glm::vec3 CameraRenderPosition() const;
    // The Camera's own position is a world (heliocentric scene) position in double.
    Camera& GetCamera() { return _camera; }
    const Camera& GetCamera() const { return _camera; }
    SimState& GetSimState() { return _simState; }
    const SimState& GetSimState() const { return _simState; }

private:
    SimState _simState;
    Camera _camera{0.001f, 20000.f, 5.0f, 0.05f, glm::dvec3(-134.0, 0.0, 0.0)};
    // C of the log-depth curve this frame (CameraNavigation::ChooseClipPlanes).
    float _logDepthC = 1.0f;

    // Co-moving camera (CameraController.cpp): the body the camera rides along with while the
    // orrery clock carries it round its orbit, and where that body was when last carried.
    struct CameraAnchor {
        std::shared_ptr<const SpaceObject> body;
        CameraNavigation::Body last;  // the body's pose when last carried (centre and drawn 3×3)
        bool pinned = false;     // a focus flight to this body: hold it until the flight lands
        bool coRotating = false; // hovering: the camera turns with the body, not only follows it
    };
    CameraAnchor _cameraAnchor;
    // Where the camera sits relative to a body, in that body's radii (scale toggles keep it).
    struct CameraBodyFrame {
        std::shared_ptr<const SpaceObject> body;
        glm::dvec3 offsetInRadii{0.0};
    };

    float _lastX = 0.f;
    float _lastY = 0.f;
    bool _isFirstMouse = true;
    double _deltaTime = 0.0;
    double _lastFrame = 0.0;

    // Offline capture (Cinematics.cpp): the frame clock steps by a fixed dt from JS.
    struct CaptureState {
        bool active = false;
        double stepSeconds = 0.0;
        bool resized = false; // BeginCapture set a render size; EndCapture fits the canvas again
        int width = 0;        // that size, re-asserted before every captured frame
        int height = 0;
    };
    CaptureState _capture;
    /** The scene object for a catalog index (0 = the Sun), or null while its system is not loaded. */
    std::shared_ptr<const SpaceObject> FindSceneBody(int catalogIndex) const;

    bool _isVertSyncEnabled = true;

#ifdef __EMSCRIPTEN__
    XrFrameState _xr;
#endif

    enum class AppState { LOADING, RUNNING };
    AppState _appState = AppState::LOADING;
    std::atomic<int> _resourcesPending{0};
    int _resourcesFailed = 0; // Finished core resources that did not arrive (subset of the finished ones)
    int _totalResources = 0;

    GLFWwindow* _mainWindow = nullptr;
    uint16_t _displayWidth = 0, _displayHeight = 0;
    int _nearestPlanetIndex = -1;      // Into _renderableSceneComponents; -1 when no planets are loaded yet
    int _focusedPlanetIndex = -1;      // FocusPlanet index (0=sun … 9=pluto); -1 = none
    FrameTimer _frameTimer;
    FT_Library _ft = nullptr;
    bool _isBackgroundMusicPlay = false, _isSearchNearestPlanet = false;

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    Mix_Music* _currentMusic = nullptr;
    int _currentSongIndex = 0;
    uint32_t _musicStartTime = 0;
    uint32_t _musicDuration = 0;
    bool _mixerInitialized = false;
    float _musicVolume = 0.3f;
    bool _musicMuted = false;
    std::unordered_set<std::string> _availableSongPaths;
#endif
#if !defined(SOLARSYSTEM_USE_SDL_MIXER) && !defined(__EMSCRIPTEN__)
    irrklang::ISoundEngine* _soundEngine = nullptr;
    std::unique_ptr<std::thread> _backgroundMusicThread;
#endif

    std::string _currentMusicTrack;
    MissionCatalog::Catalog _missionCatalog;
    int _focusedMissionIndex = -1;
    bool _missionFollowActive = false;
    std::unique_ptr<AsteroidField> _asteroidField;
    std::unique_ptr<AsteroidField> _kuiperField; // Population::Kuiper; realistic scale only
    std::shared_ptr<Star> _sun;
    std::vector<RenderableSceneComponent> _renderableSceneComponents;
    std::vector<std::string> _backgroundSongPaths;

    // Observe mode state and its sky cache — see CurrentObserveSky().
    ObserveState _observe;
    mutable Observer::Sky _observeSky;
    mutable double _observeSkyJd = 0.0;
    mutable Observer::Site _observeSkySite;
    mutable bool _observeSkyValid = false;

    // Next-conjunction hint cache — see GetNextConjunction().
    mutable SkyEvents::Conjunction _nextConjunction;
    mutable double _nextConjunctionComputedJd = 0.0;
    mutable bool _nextConjunctionCached = false;

    // Next-sky-event cache — see GetNextSkyEvent().
    mutable SkyEvents::SkyEvent _nextSkyEvent;
    mutable double _nextSkyEventComputedJd = 0.0;
    mutable bool _nextSkyEventCached = false;

    // Staged-loading infrastructure for all planet systems (WASM only)
    std::vector<PlanetSystemManifest> _planetSystemManifests;
    std::unique_ptr<MeshHolder> _sphereModel;

    // Shadow/color/overlay/HDR draw passes and the GPU resources they own — see Renderer.h.
    Renderer _renderer;
    // This frame's FrameContext pieces (BuildFrameSnapshot), read by the passes and the HUD.
    SceneSnapshot _frameScene;
    std::array<CameraView, 2> _frameViews{};
    HudState _frameHud;
    // Each mission's probe position at this frame's date (UpdateScene); nullopt off its trajectory.
    std::vector<std::optional<glm::vec3>> _missionProbes;

    // --- Terrain (TerrainController.cpp; src/render/terrain/, docs/ARCHITECTURE.md §11.3) --
    std::unique_ptr<Terrain::TerrainSystem> _terrain;
    std::string _terrainLogged; // the last [Terrain] stats line, and when it went out
    double _terrainLoggedAt = 0.0;
    /** Budget, pixel error and in-flight cap from a quality tier (Low turns terrain off). */
    void ConfigureTerrain(const QualityTierSettings& settings);
    /**
     * After UpdateScene and BuildFrameSnapshot, before CullScene: which render.terrain
     * bodies draw tiles this frame, their selections into _frameScene.terrain, and the
     * GPU copies of evicted tiles dropped.
     */
    void UpdateTerrain(int viewCount);
    /** The terrain slot of a body flagged render.terrain that UpdateTerrain has seen, else -1. */
    int TerrainSlotOf(const SpaceObject& object) const;
    /** The tile surface's radius under `eye` (model units) while `object` draws tiles, else nullopt. */
    std::optional<double> TerrainSurfaceRadius(const SpaceObject& object, const glm::dvec3& eye) const;

    void InitSystems();
    void InitScene();
    void LoadCoreResources(); // Download only core assets (skybox, sun, models)
    void InitSceneObjects();
    void InitStarSystem();
    void LoadPlanetSystemManifests(); // WASM: parse resource/planet_manifest.json
    std::function<void()> MakePlanetInitFunc(const std::string& initTag);
    void ActivateNearestPlanetSystems(int count);  // WASM: load the `count` systems nearest the camera now
    void ActivatePlanetSystem(OrbitLayout::Body body); // WASM: load one system now (focus / deep link)
    void StartPlanetSystemDownload(PlanetSystemManifest& manifest, const char* reason);
    /** Build a catalog system (planet + moons + atmosphere/clouds/rings flags) from an initTag. */
    void InitCatalogSystem(const MeshHolder& sphereModel, const std::string& initTag);
    /** Focus-index wrapper around InitCatalogSystem (Ceres, Vesta, or any catalog primary). */
    void InitCatalogBody(const MeshHolder& sphereModel, OrbitLayout::Body body);
    void InitSongList();
    void Dispose();
    void ReleaseGpuResources(); // Scene + renderer GL objects; called before the context goes
    void UpdateLoadingProgress(); // Update JavaScript loading progress bar
    void UpdatePlanetSystemLoading(); // Staged download + init of planet systems (WASM)
    void RefreshPlanetProxyPositions(); // Keep WASM proxy markers aligned with orbit scale
    void UpdateLOD();             // Central LOD manager: upgrade textures for nearest planets (WASM)
    glm::ivec2 DisplaySize() const { return {_displayWidth, _displayHeight}; }
    void LoadMissionCatalog();
    void LoadMissions();
    void UpdateMissionFollow();
    const char* ObserveEventVisibility() const; // "above" / "below" / "unknown" / "none" for the next sky event
    void UpdateObserveFrame();          // Observe: park the camera above Earth so loading/LOD follow it
    void ApplyObserveEnvOverride();     // SOLARSYSTEM_OBSERVE=lat,lon[,jd[,az,el,fov]] for headless captures
    void ApplyScaleEnvOverride();       // SOLARSYSTEM_SCALE=compressed|realistic: the web's scale toggle, natively
    void ApplyCameraPoseEnvOverride();  // SOLARSYSTEM_CAMERA_POSE=x,y,z,yaw,pitch[,jd]: web setCameraPose, natively
    void StopMissionFollow();
    bool SampleMissionHeliocentricPosition(int idx, glm::dvec3& outScene) const;
    /** SampleMissionHeliocentricPosition in the render frame. */
    bool SampleMissionScenePosition(int idx, glm::vec3& outScene) const;
    /** Once per frame, after everything that moves the camera: put the render origin on it. */
    void UpdateRenderOrigin();
    /** Move the render origin and carry every render-frame position cached across frames. */
    void MoveRenderOrigin(const glm::dvec3& origin);
    /** 1 = full volume, down to 0.32 when the camera is close to the nearest planet. */
    float MusicDuckAmount() const;
    void UpdateMusicDucking();
#ifdef __EMSCRIPTEN__
    float _reportedMusicDuck = 1.0f;
#endif
    /**
     * Everything that moves this frame, once, before any pass: body placement and light
     * matrices, the belts, proxy markers, LOD, the nearest-planet search and the sky-event
     * caches. The render passes after it only read scene state (so an XR frame, which draws
     * twice, still simulates once).
     */
    void UpdateScene(double julianDate);
    /** The sideways extent of `component`'s light box: the visible cap near the planet, else ±3 radii. */
    ShadowFit::Box FitShadowBox(RenderableSceneComponent& component, const ShadowFit::LightBasis& light, float fullExtent);
    /** Re-derive every body's render-frame placement from the ephemeris (no drawing). */
    void PlaceSceneComponents();
    /** The Sun and every component, placed for the current date (PlaceSceneComponents + Sun). */
    void PlaceBodies();

    // --- Camera navigation (CameraController.cpp; math in core/CameraNavigation.h) --------
    /** Every body the camera can fly near or collide with: the Sun, planets and moons. */
    std::vector<std::shared_ptr<const SpaceObject>> NavigationBodies() const;
    /** Before input: move the camera with its anchor body; set the fly speed for its altitude. */
    void CarryCameraWithAnchor();
    /** After input: keep the camera above the surface, pick its anchor, set the clip planes. */
    void ConstrainCamera();
    void SetCameraAnchor(std::shared_ptr<const SpaceObject> body, bool pinned);
    /** The nearest body in its own radii, and the camera's offset from it in those radii. */
    CameraBodyFrame CaptureCameraBodyFrame() const;
    /** Put the camera back at `frame`'s offset from its body (after a scale change resized it). */
    void RestoreCameraBodyFrame(const CameraBodyFrame& frame);
    /** Simulate once, then draw the scene into `viewCount` views (1 mono, 2 XR) — see FramePasses.h. */
    void RenderFrame(int viewCount);
    /** Debug: the previous frame's FramePasses::Stats and glUniform* count (SOLARSYSTEM_FRAME_STATS=N). */
    void ReportFrameStats();
    /**
     * Frustum culling after the views are known: marks every scene object and component
     * visible or not in any of the `viewCount` views, and fills _drawOrder with the visible
     * components front to back (plus the nearest one, which draws the star).
     */
    void CullScene(int viewCount);
    std::vector<std::size_t> _drawOrder; // into _renderableSceneComponents, this frame
#ifndef __EMSCRIPTEN__
    /** Native shader hot reload: every ~0.5 s, rebuild programs whose files changed (off with SOLARSYSTEM_SHADER_HOT_RELOAD=0). */
    void PollShaderHotReload(double now);
    double _lastShaderPoll = 0.0;
#endif
    /** After UpdateScene: fill _frameScene, _frameViews and _frameHud for this frame's passes. */
    void BuildFrameSnapshot(int viewCount);
    /** The passes' view of frame state for view `view`; valid until the next BuildFrameSnapshot. */
    FrameContext FrameContextFor(int view) const;
    void EndFrame();           // Count the frame for the FPS HUD; native: apply the frame limiter
#ifdef __EMSCRIPTEN__
    void RenderXrStereoFrame();
#endif
    void StartSearchNearestPlanet();
    void UpdateSearchNearestPlanet(); // Once per frame, on the main thread
    void StartPlayBackgroundMusic();
    void UpdateBackgroundMusic(); // For Emscripten frame-based music
    void StopPlayBackgroundMusic();
#ifndef __EMSCRIPTEN__
    void LoadWindowIcon() const;
#endif
    void DisplaySystemInformation() const;
    void ProcessInput(GLFWwindow* window);
    float CalculateSpaceObjectDistance(const SpaceObject* spaceObject) const;
    glm::vec3 CurrentFpsColor() const;
    void HandleResize(int width, int height);
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
#ifdef __EMSCRIPTEN__
    static EM_BOOL WebWindowResizeCallback(int eventType, const EmscriptenUiEvent* uiEvent, void* userData);
    /** Size the canvas backing store to its CSS size × backingStoreScale and follow it. */
    void FitCanvasToCss();
#endif
    static void MouseCallback(GLFWwindow* window, double xPos, double yPos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yOffset);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void VertSync(bool enable);
    static bool WGLExtensionSupported(const char* extensionName);
};

#endif //SOLARSYSTEM_APPLICATION_H
