#ifndef SOLARSYSTEM_APPLICATION_H
#define SOLARSYSTEM_APPLICATION_H
#include "ApplicationTypes.h"
#include "Renderer.h"
#include "SimState.h"
#include "Auxiliary_Modules/AuxiliaryModules.h"
#include "Auxiliary_Modules/SkyEvents.h"
#include "PlanetSystemManifest.h"
#include "Solar_System/AsteroidField.h"
#include "Solar_System/SolarSystem.h"
#include "SystemModules.h"
#include "XrState.h"
#include <atomic>
#include <functional>
#include <string>
#include <unordered_set>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifdef SOLARSYSTEM_USE_SDL_MIXER
#include <SDL.h>
#include <SDL_mixer.h>
#else
#include <irrKlang.h>
using namespace irrklang;
#endif

class Application {
    friend class Renderer;
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
    void SetMusicVolume(float volume);
    float GetMusicVolume() const;
    void SetMusicMuted(bool muted);
    bool GetMusicMuted() const;
    void SetOrbitLinesEnabled(bool enabled);
    bool GetOrbitLinesEnabled() const;
    void SetMagneticFieldsEnabled(bool enabled);
    bool GetMagneticFieldsEnabled() const;
    void ForEachEnabledMagneticField(
        const std::function<void(const SpaceObject& object, const MagneticFieldParams& params)>& fn) const;
    Camera& GetCamera() { return _camera; }
    const Camera& GetCamera() const { return _camera; }
    SimState& GetSimState() { return _simState; }
    const SimState& GetSimState() const { return _simState; }

private:
    SimState _simState;
    Camera _camera{0.001f, 20000.f, 5.0f, 0.05f, glm::vec3(-134.0f, 0.0f, 0.0f)};

    float _lastX = 0.f;
    float _lastY = 0.f;
    bool _isFirstMouse = true;
    double _deltaTime = 0.0;
    double _lastFrame = 0.0;

    bool _isVertSyncEnabled = true;

#ifdef __EMSCRIPTEN__
    XrFrameState _xr;
#endif

    enum class AppState { LOADING, RUNNING };
    AppState _appState = AppState::LOADING;
    std::atomic<int> _resourcesPending{0};
    int _totalResources = 0;

    GLFWwindow* _mainWindow = nullptr;
    uint16_t _displayWidth = 0, _displayHeight = 0;
    ssize_t _nearestPlanetIndex = -1;  // -1 when no planets are loaded yet
    int _focusedPlanetIndex = -1;      // FocusPlanet index (0=sun … 9=pluto); -1 = none
    FPS_Handler _fpsHandler;
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
#ifndef __EMSCRIPTEN__
    std::unique_ptr<std::thread> _searchNearestPlanetThread;
#endif
#ifdef __EMSCRIPTEN__
    int _nearestPlanetSearchFrameCounter = 0;
#endif
#ifndef SOLARSYSTEM_USE_SDL_MIXER
    ISoundEngine* _soundEngine = nullptr;
    std::unique_ptr<std::thread> _backgroundMusicThread;
#endif

    std::string _currentMusicTrack;
    MissionCatalog::Catalog _missionCatalog;
    int _focusedMissionIndex = -1;
    bool _missionFollowActive = false;
    std::unique_ptr<AsteroidField> _asteroidField;
    std::shared_ptr<Star> _sun;
    std::vector<RenderableSceneComponent> _renderableSceneComponents;
    std::vector<std::string> _backgroundSongPaths;

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

    void InitSystems();
    void InitScene();
    void LoadCoreResources(); // Download only core assets (skybox, sun, models)
    void LoadOptionalSounds(); // Fire-and-forget background music downloads (WASM)
    void InitSceneObjects();
    void InitStarSystem();
    void LoadPlanetSystemManifests(); // WASM: parse resource/planet_manifest.json
    std::function<void()> MakePlanetInitFunc(const std::string& initTag);
    /** Build a catalog system (planet + moons + atmosphere/clouds/rings flags) from an initTag. */
    void InitCatalogSystem(const MeshHolder& sphereModel, const std::string& initTag);
    /** Focus-index wrapper around InitCatalogSystem (Ceres, Vesta, or any catalog primary). */
    void InitCatalogBody(const MeshHolder& sphereModel, OrbitLayout::Body body);
    void InitSongList();
    void Dispose();
    void UpdateLoadingProgress(); // Update JavaScript loading progress bar
    void UpdatePlanetSystemLoading(); // Staged download + init of planet systems (WASM)
    void RefreshPlanetProxyPositions(); // Keep WASM proxy markers aligned with orbit scale
    void ApplyRenderResources(uint16_t shadowResolution, bool enableHdr);
    void UpdateLOD();             // Central LOD manager: upgrade textures for nearest planets (WASM)
    void RenderPlanetProxyMarkers() const; // Show orbital markers for unloaded planets (WASM)
    void LoadMissionCatalog();
    void LoadMissions();
    void UpdateMissionFollow();
    void StopMissionFollow();
    bool SampleMissionScenePosition(int idx, glm::vec3& outScene) const;
    void UpdateMusicDucking();
    void RenderFrameContent(); // Sky → planets → effects (one eye / mono)
#ifdef __EMSCRIPTEN__
    void RenderXrStereoFrame();
#endif
    void StartSearchNearestPlanet();
    void UpdateSearchNearestPlanet(); // For Emscripten frame-based search
    void StartPlayBackgroundMusic();
    void UpdateBackgroundMusic(); // For Emscripten frame-based music
    void StopSearchNearestPlanet();
    void StopPlayBackgroundMusic();
    void LoadWindowIcon() const;
    void DisplaySystemInformation() const;
    void ProcessInput(GLFWwindow* window);
    float CalculateSpaceObjectDistance(const SpaceObject* spaceObject) const;
    glm::vec3 CurrentFpsColor() const;
    void HandleResize(int width, int height);
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
#ifdef __EMSCRIPTEN__
    static EM_BOOL WebWindowResizeCallback(int eventType, const EmscriptenUiEvent* uiEvent, void* userData);
#endif
    static void MouseCallback(GLFWwindow* window, double xPos, double yPos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yOffset);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void VertSync(bool enable);
    static bool WGLExtensionSupported(const char* extensionName);
};

#endif //SOLARSYSTEM_APPLICATION_H
