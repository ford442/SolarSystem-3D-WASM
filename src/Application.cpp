// Application lifecycle and the per-frame loop. The rest of Application's members live
// alongside their subject: PlatformWindow.cpp (window/GL bring-up, resize, teardown),
// InputHandler.cpp, AudioPlayer.cpp, RenderSettings.cpp (quality presets + LOD),
// SceneRenderer.cpp / SceneOverlayRenderer.cpp (drawing), StarSystemFactory.cpp and
// PlanetSystemLoader.cpp (scene construction), XrSession.cpp (WebXR).
#include "Application.h"
#include "Solar_System/StarSystemCatalog.h"
#include "FrameContext.h"
#include "JsBridge.h"
#include "QualitySettings.h"
#include "ResourceManifest.h"
#include "SimState.h"
#include "WasmExports.h"
#include "assets/TextureLoadingQueue.h"
#include "assets/UploadQueue.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SceneScale.h"
#include "astro/MissionCatalog.h"
#include "GoldenCapture.h"
#include "IAppModule.h"
#include "core/FrameProfile.h"
#include "core/Log.h"
#include "render/XrPointerRenderer.h"
#include "render/terrain/TerrainSystem.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <ratio>
#include <sstream>
#include <memory>
#include <string>
#include <utility>

namespace {
#ifdef __EMSCRIPTEN__
// `?graph=1` on the page. (EM_JS is JavaScript: keep clang-format out.)
// clang-format off
EM_JS(int, FrameGraphQuery, (), {
    try {
        const value = new URLSearchParams(location.search).get('graph');
        return value !== null && value !== '0' ? 1 : 0;
    } catch (e) {
        return 0;
    }
});
// clang-format on
#endif

/** ?graph=1 (web) or SOLARSYSTEM_GRAPH=1 (native): log the compiled frame graph when it changes. */
bool FrameGraphDumpRequested() {
#ifdef __EMSCRIPTEN__
    return FrameGraphQuery() != 0;
#else
    const char* value = std::getenv("SOLARSYSTEM_GRAPH");
    return value && *value && std::string(value) != "0";
#endif
}
} // namespace

Application::Application() : _frameTimer(240) {
    SetActiveApplication(this);
    InitSystems();
    _missions.StartCatalogJob();
    ApplyQualityPreset(_simState.qualityPreset);
    InitScene();
}

Application::~Application() {
    if (GetActiveApplication() == this) {
        SetActiveApplication(nullptr);
    }
    Dispose();
}

void Application::Exec() {
#ifdef __EMSCRIPTEN__
    // No simulated infinite loop: main() heap-allocates the Application, so returning is
    // safe, and it lets the swap interval be applied now that a main loop exists to time
    // (emscripten_set_main_loop_timing is a no-op before this call).
    emscripten_set_main_loop_arg([](void* arg) {
        static_cast<Application*>(arg)->RunOneFrame();
    }, this, 0, 0);
    VertSync(_isVertSyncEnabled);
#else
    if (const char* golden = std::getenv("SOLARSYSTEM_GOLDEN"); golden && *golden) {
        RunGoldenCapture(golden);
        return;
    }
    while (!glfwWindowShouldClose(_mainWindow)) {
        RunOneFrame();
    }
#endif
}

#ifndef __EMSCRIPTEN__
void Application::RunGoldenCapture(const std::string& spec) {
    const GoldenSpec golden = ParseGoldenSpec(spec);
    // InitSystems already started the clock at `golden.jd` (GoldenStartJulianDate): the belts
    // measure their phases from the date the scene was built at, so a wall-clock start would
    // move every asteroid between two runs. Natively the core resources are on disk: loading
    // is a frame or two.
    for (int i = 0; _appState == AppState::LOADING && i < 1000 && !glfwWindowShouldClose(_mainWindow); ++i) {
        RunOneFrame();
    }
    if (_appState == AppState::LOADING) {
        LOG_ERROR("Golden", "Scene never finished loading; nothing written");
        return;
    }
    if (golden.jd > 0.0) {
        OrbitLayout::SetJulianDate(golden.jd); // again: an environment override may have moved it since
        _simState.timePaused = true;
    }
    if (golden.fields) {
        SetMagneticFieldsEnabled(true);
    }
    if (golden.focus >= 0) {
        FocusPlanetByIndex(golden.focus);
    }
    _cinematics.Begin(0, 0);
    _cinematics.SetFrozenHud(true);
    for (int frame = 1; frame <= golden.frames; ++frame) {
        if (frame == golden.frames) {
            _cinematics.RequestReadback(golden.out);
        }
        _cinematics.Step(golden.step);
    }
    _cinematics.SetFrozenHud(false);
    _cinematics.End();
}
#endif

void Application::FitRenderTargetToPage() {
#ifdef __EMSCRIPTEN__
    FitCanvasToCss();
#endif
}

bool Application::XrPresenting() const {
#ifdef __EMSCRIPTEN__
    return _xr.active;
#else
    return false;
#endif
}

void Application::RestartFrameClock() {
    _lastFrame = glfwGetTime();
}

glm::vec3 Application::CameraRenderPosition() const {
    return OrbitLayout::ToRenderFrame(_camera.GetWorldPosition());
}

void Application::RunOneFrame() {
    _frameTimer.RunFrameTimer();
#ifndef __EMSCRIPTEN__
    PollGpuContextReset(); // web hears about a lost context from the page instead
#endif

    if (_appState == AppState::LOADING) {
#ifdef __EMSCRIPTEN__
        if (!_xr.active)
#endif
        {
            glBindFramebuffer(GL_FRAMEBUFFER, DefaultFramebuffer());
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Update progress bar
        UpdateLoadingProgress();
        PumpUploads();

        if (_resourcesPending <= 0) {
             LOG_INFO("Loading", "All resources downloaded. Initializing scene...");
             InitSceneObjects();
             _appState = AppState::RUNNING;
        }

#ifdef __EMSCRIPTEN__
        if (!_xr.active)
#endif
        {
            glfwSwapBuffers(_mainWindow);
        }
        glfwPollEvents();
        EndFrame();
        return;
    }

    _profiler.BeginFrame(glfwGetTime());
    // The frame graph brackets every pass (rhi::GlCommandList): CPU into the profiler's stages
    // and the pass's row, GPU into the timer's slot kStageCount + that row.
    _renderer.SetProfiling(_profiler.Gpu(), &_profiler.Passes(), &_profiler.Stages());
    const double currentFrame = glfwGetTime();
    // A capture frame is a fixed step after the last, however long encoding it took.
    _deltaTime = _cinematics.FrameDelta(currentFrame - _lastFrame);
    _lastFrame = currentFrame;
    _simState.animationSeconds += _deltaTime;
    UpdateSystemJump(_deltaTime); // may swap the whole scene, so before anything places a body
#ifndef __EMSCRIPTEN__
    PollShaderHotReload(currentFrame);
#endif

    if (_observe.active) {
        // Sky watching runs on wall-clock UTC at timeRate, not the orrery's 3 days per second.
        _simState.simDeltaSeconds = 0.0f;
        if (!_simState.timePaused) {
            OrbitLayout::SetJulianDate(OrbitLayout::GetJulianDate() +
                                       _deltaTime * _observe.timeRate / 86400.0);
        }
        UpdateObserveFrame();
    } else {
        _simState.simDeltaSeconds = _simState.timePaused ? 0.0f : static_cast<float>(_deltaTime) * _simState.timeScale;
        OrbitLayout::Advance(_simState.simDeltaSeconds);
    }
    // Bodies at this frame's date before the camera reads them (UpdateScene places them again
    // once the render origin has followed the camera, and catches systems loaded meanwhile).
    PlaceBodies();
    _cameraController.CarryWithAnchor();
    _camera.UpdateTransition(_deltaTime);
    _missions.UpdateFollow();

    ProcessInput(_mainWindow);
    _cameraController.Constrain();
    // After everything that moves the camera this frame, before anything reads positions.
    UpdateRenderOrigin();
    UpdatePlanetSystemLoading();
    UpdateMusicDucking();

#ifdef __EMSCRIPTEN__
    if (_xr.active) {
        RenderXrStereoFrame(); // the frame graph's last passes pump the uploads
        _profiler.EndFrame();
#ifdef SOLARSYSTEM_USE_SDL_MIXER
        UpdateBackgroundMusic();
#endif
        glfwPollEvents();
        EndFrame();
        return;
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, DefaultFramebuffer());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // Scene, HUD, the jump fade, the upload pump and the loading line: one frame graph.
    RenderFrame(1);
    _profiler.EndFrame();

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    UpdateBackgroundMusic();
#endif

#ifndef __EMSCRIPTEN__
    _cinematics.BeforeSwap(DisplaySize()); // a golden capture's readback, if one is due
#endif
    glfwSwapBuffers(_mainWindow);
    glfwPollEvents();
    EndFrame();
}

#ifdef __EMSCRIPTEN__
namespace {
// `?uploadkb=N` on the page, or -1. (EM_JS is JavaScript: keep clang-format out.)
// clang-format off
EM_JS(int, UploadBudgetQueryKb, (), {
    try {
        const value = new URLSearchParams(location.search).get('uploadkb');
        const parsed = value ? parseInt(value, 10) : NaN;
        return isNaN(parsed) ? -1 : parsed;
    } catch (e) {
        return -1;
    }
});
// clang-format on
} // namespace
#endif

std::size_t Application::UploadBudgetBytes() const {
    // A phone's WebGL texImage2D runs a few hundred MB/s once ANGLE's copies are counted, so
    // 2 MB is a few ms of a 33 ms frame; desktop WebGL and native GL move far more per ms.
    static const long overrideKb = [] {
#ifdef __EMSCRIPTEN__
        return static_cast<long>(UploadBudgetQueryKb());
#else
        const char* value = std::getenv("SOLARSYSTEM_UPLOAD_BUDGET_KB");
        return value ? std::atol(value) : -1L;
#endif
    }();
    if (overrideKb > 0) {
        return static_cast<std::size_t>(overrideKb) * 1024;
    }
#ifdef __EMSCRIPTEN__
    return std::size_t{_simState.isMobileWeb ? 2u : 8u} * 1024 * 1024;
#else
    return std::size_t{32} * 1024 * 1024;
#endif
}

void Application::PumpUploads() {
    UploadQueue::Get().Pump(UploadBudgetBytes());
}

void Application::EndFrame() {
    _frameTimer.EndFrame();
#ifndef __EMSCRIPTEN__
    // With vsync on, the swap already paces the loop; the limiter only caps an unsynced one.
    if (!_isVertSyncEnabled) {
        _frameTimer.WaitForFrameTimer();
    }
#endif
}

std::string Application::GetFrameStatsJson() {
    return _profiler.Json();
}

void Application::SetGpuTimersEnabled(bool enabled) {
    _profiler.SetGpuTimersEnabled(enabled);
}

#ifndef __EMSCRIPTEN__
void Application::PollShaderHotReload(double now) {
    static const bool enabled = [] {
        const char* value = std::getenv("SOLARSYSTEM_SHADER_HOT_RELOAD");
        return !(value && std::string(value) == "0");
    }();
    if (!enabled || now - _lastShaderPoll < 0.5) {
        return;
    }
    _lastShaderPoll = now;
    Shader::ReloadChanged();
}
#endif

void Application::InitScene() {
#ifdef __EMSCRIPTEN__
    // Catalog JSON is in the .data preload; parse it before the first JS frame so
    // Explorer / deep links see Voyager rows before InitSceneObjects builds GL meshes.
    _missions.LoadCatalog();
    LoadCoreResources();
#else
    InitSceneObjects();
    _appState = AppState::RUNNING;
#endif
}

void Application::UpdateLoadingProgress() {
#ifdef __EMSCRIPTEN__
    const int loaded = _totalResources - _resourcesPending;
    NotifyLoadingProgress(loaded, _totalResources, _resourcesFailed);
#endif
}


void Application::InitSceneObjects() {
    const auto started = std::chrono::steady_clock::now();
    _camera.SetAspect(static_cast<float>(_displayWidth) / static_cast<float>(_displayHeight));
#ifdef __EMSCRIPTEN__
    TextureImage2D::SetWebAnisotropyCap(_simState.isMobileWeb ? 4.0f : 8.0f);
#endif
    _renderer.Init(_simState, _ft, DisplaySize());
    _ft = nullptr; // Renderer::Init released it once the font was loaded
    _renderer.SetUploadPump([this] {
        TextureLoadingQueue::GetInstance().ProcessQueue();
        PumpUploads();
    });
    _renderer.SetGraphDump(FrameGraphDumpRequested());
    LoadMissions();
    {
        const auto asteroidQuality = GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb);
        _asteroidField =
            std::make_unique<AsteroidField>(AsteroidField::kDefaultSeed, asteroidQuality.asteroidInstanceCount);
        _kuiperField = std::make_unique<AsteroidField>(AsteroidField::kKuiperSeed, asteroidQuality.kuiperInstanceCount,
                                                       AsteroidField::Population::Kuiper);
    }

    InitSongList();
    // ?system= on web, SOLARSYSTEM_SYSTEM natively: select the catalog before the build reads it.
    ApplySystemEnvOverride();
    _renderer.PrefetchCatalogVariants();
    InitStarSystem();
    _spaceWeather.Init();

    glfwShowWindow(_mainWindow);
#ifdef _WIN32
    glfwSetWindowMonitor(_mainWindow, glfwGetPrimaryMonitor(), 0, 0, _displayWidth, _displayHeight, GLFW_DONT_CARE);
#endif

    StartSearchNearestPlanet();
    StartPlayBackgroundMusic();
    ApplyScaleEnvOverride();
    if (_pendingSystemLanding) {
        _pendingSystemLanding = false;
        PlaceBodies();
        LandCameraInSystem(_pendingSystemFocus);
        if (!StarSystemCatalog::IsSol()) {
            // On web the module resolves before the scene is built, so the page set up its UI
            // for Sol (and may have applied a link's pose and speed, which the landing just
            // replaced): announce the system as a jump's swap and arrival would.
            NotifySettingsChanged("timeScale");
            NotifySystemChanged(StarSystemCatalog::ActiveId().c_str(), 1);
            NotifySystemChanged(StarSystemCatalog::ActiveId().c_str(), 2);
        }
    }
    ApplyCameraPoseEnvOverride();
    ApplyObserveEnvOverride();
    LOG_INFO("Loading", "Scene initialised in %.0f ms (%d job worker(s))",
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(),
             Jobs::WorkerCount());
}

void Application::ApplyScaleEnvOverride() {
    // Runs the same toggle as the web UI on the scene just built, before a pose override, so
    // SOLARSYSTEM_CAMERA_POSE takes realistic world coordinates when this asks for them.
    const char* spec = std::getenv("SOLARSYSTEM_SCALE");
    if (!spec || !*spec) {
        return;
    }
    const std::string mode(spec);
    if (mode == "realistic" || mode == "1") {
        ApplyOrbitScaleMode(1);
    } else if (mode == "compressed" || mode == "0") {
        ApplyOrbitScaleMode(0);
    } else {
        LOG_WARN("OrbitScale", "Unrecognized SOLARSYSTEM_SCALE=\"%s\"; use compressed or realistic", spec);
    }
}

void Application::ApplyCameraPoseEnvOverride() {
    // Same arguments as the web's window.setCameraPose, plus an optional Julian date that
    // pins (and pauses) the clock, so a native and a web capture can share one pose.
    const char* spec = std::getenv("SOLARSYSTEM_CAMERA_POSE");
    if (!spec || !*spec) {
        return;
    }
    double values[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    int parsed = 0;
    std::stringstream stream(spec);
    std::string token;
    while (parsed < 6 && std::getline(stream, token, ',')) {
        values[parsed++] = std::atof(token.c_str());
    }
    if (parsed < 5) {
        LOG_WARN("Camera", "SOLARSYSTEM_CAMERA_POSE needs x,y,z,yaw,pitch[,jd]");
        return;
    }
    if (parsed >= 6 && values[5] > 0.0) {
        OrbitLayout::SetJulianDate(values[5]);
        _simState.timePaused = true;
    }
    SetCameraHeliocentricPose(glm::dvec3(values[0], values[1], values[2]), static_cast<float>(values[3]),
                              static_cast<float>(values[4]));
}

void Application::FocusPlanetByIndex(int idx) {
    FocusPlanetByIndex(idx, glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f)));
}

void Application::FocusPlanetByIndex(int idx, const glm::vec3& fromDirection) {
    if (_observe.active) return; // the viewer stays on the ground in Observe
    _missions.Stop();
    idx = std::clamp(idx, 0, OrbitLayout::BodyCount() - 1);
    _focusedPlanetIndex = idx;

    glm::dvec3 target(0.0);
    float focusRadius = 10.0f;
    // The art distances below are compressed-scale numbers; realistic scales them with the
    // body, so a focus frames it the same either way.
    float sizeFactor = 1.0f;
    std::shared_ptr<const SpaceObject> anchor;

    if (idx == 0) {
        if (_sun) {
            target = _sun->GetWorldPosition();
            sizeFactor = _sun->SizeFactor();
            anchor = _sun;
        }
        focusRadius = 40.0f;
    } else {
        // Match on the planet's own name rather than position in the vector: staged loading
        // pushes components in camera-approach order, so the Nth component is not body N.
        const auto body = static_cast<OrbitLayout::Body>(idx);
        std::shared_ptr<const Planet> found;
        for (const auto& component : _renderableSceneComponents) {
            if (!component.planet) {
                continue;
            }
            const std::wstring& wideName = component.planet->GetEngName();
            const std::string name(wideName.begin(), wideName.end());
            if (OrbitLayout::BodyFromName(name) == body) {
                found = component.planet;
                break;
            }
        }

        if (found) {
            target = found->GetWorldPosition();
            sizeFactor = found->SizeFactor();
            focusRadius = std::max(found->GetRadius() * 4.0f, 2.0f * sizeFactor) / sizeFactor;
            anchor = found;
        } else {
            // Not loaded yet (web staged loading): fly to where the ephemeris says it is, and
            // start its download now rather than when the camera arrives.
            target = OrbitLayout::GetHeliocentricScenePosition(body);
            if (const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(idx)) {
                sizeFactor = SceneScale::BodySizeFactor(*entry);
            }
            focusRadius = 20.0f;
            ActivatePlanetSystem(body);
        }
    }

    const glm::vec3 direction = fromDirection;
    const glm::dvec3 cameraPos = target + glm::dvec3(direction) * static_cast<double>((focusRadius + 30.0f) * sizeFactor);
    const glm::vec3 lookDir = -direction;
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(lookDir.y));
    _camera.StartTransitionTo(cameraPos, yaw, pitch, 2.0f);
    // Ride along with the body during the flight and after it lands, even from outside the
    // distance at which the camera would pick it up by itself.
    _cameraController.SetAnchor(anchor, true);

#ifdef __EMSCRIPTEN__
    NotifyPlanetFocused(idx);
#endif
}

int Application::GetFocusedPlanetIndex() const {
    return _focusedPlanetIndex;
}

void Application::SetCameraHeliocentricPose(const glm::dvec3& position, float yaw, float pitch) {
    _camera.CancelTransition();
    _camera.SetWorldPosition(position);
    _camera.SetYawPitch(yaw, pitch);
    // A jump: whatever the camera rode along with is somewhere else now. ConstrainCamera
    // picks the body it arrived next to on the next frame.
    _cameraController.SetAnchor(nullptr, false);
    MoveRenderOrigin(position);
}

glm::dvec3 Application::GetCameraHeliocentricPosition() const {
    return _camera.GetWorldPosition();
}

void Application::UpdateRenderOrigin() {
    MoveRenderOrigin(_camera.GetWorldPosition());
}

void Application::MoveRenderOrigin(const glm::dvec3& origin) {
    const glm::dvec3 previous = OrbitLayout::GetRenderOrigin();
    if (origin == previous) {
        return;
    }
    OrbitLayout::SetRenderOrigin(origin);

    // Everything that holds a render-frame position across frames, moved so it stays put in
    // the heliocentric frame. Uniforms, the belts and the probes are re-derived every frame.
    const glm::vec3 shift(origin - previous);
#ifdef __EMSCRIPTEN__
    // webxr.ts composed this frame's eye views from the camera before the shift.
    const glm::mat4 undoShift = glm::translate(glm::mat4(1.0f), shift);
    for (auto& eye : _xr.eyes) {
        eye.view = eye.view * undoShift;
    }
#endif
    if (XrPointerRenderer* pointers = _renderer.GetXrPointers()) {
        pointers->ShiftOrigin(shift);
    }
    // Re-derive the bodies' translations now rather than in UpdateScene: staged loading and
    // the LOD manager compare them with the camera before this frame's UpdateScene runs.
    if (_sun) {
        _sun->Rebase();
    }
    for (auto& component : _renderableSceneComponents) {
        component.ForEachObject([](SpaceObject& object) { object.Rebase(); });
    }
    RefreshPlanetProxyPositions();
    _renderer.FollowMissionPathOrigin(_missions.Catalog(), _simState.qualityPreset);
}

void Application::FocusMissionByIndex(int idx) {
    if (idx < 0) {
        _missions.Stop();
        return;
    }
    if (_missions.Focus(idx)) {
        _focusedPlanetIndex = -1;
    }
}

void Application::SetXrControllerRay(int hand, float ox, float oy, float oz, float dx, float dy, float dz,
                                     int visible) {
    XrPointerRenderer* pointers = _renderer.GetXrPointers();
    if (!pointers) {
        return;
    }
    pointers->SetRay(hand, glm::vec3(ox, oy, oz), glm::vec3(dx, dy, dz), visible != 0);
}

void Application::SetOrbitLinesEnabled(bool enabled) { _renderer.SetOrbitLinesEnabled(enabled); }
bool Application::GetOrbitLinesEnabled() const { return _renderer.GetOrbitLinesEnabled(); }
void Application::SetMagneticFieldsEnabled(bool enabled) { _renderer.SetMagneticFieldsEnabled(enabled, _simState.qualityPreset); }
bool Application::GetMagneticFieldsEnabled() const { return _renderer.GetMagneticFieldsEnabled(); }

void Application::LoadMissions() {
    if (_missions.Count() == 0) {
        _missions.LoadCatalog();
    }
    _renderer.RebuildMissionPaths(_missions.Catalog(), _simState.qualityPreset);
}

const SkyEvents::Conjunction& Application::GetNextConjunction() const {
    return _skyEvents.NextConjunction();
}

const SkyEvents::SkyEvent& Application::GetNextSkyEvent() const {
    return _skyEvents.NextEvent();
}

int Application::GetNearestPlanetIndexForJs() const {
    if (_nearestPlanetIndex < 0) {
        return -1;
    }
    return _nearestPlanetIndex + 1;
}

void Application::StartSearchNearestPlanet() {
    _isSearchNearestPlanet = true;
}

void Application::UpdateSearchNearestPlanet() {
    // A min over a few dozen distances: cheap enough for every frame, and on the main thread
    // it never races the planet list or the camera (it used to run on its own thread natively).
    if (!_isSearchNearestPlanet) {
        return;
    }
    auto nearestPlanetIt = std::min_element(
        _renderableSceneComponents.begin(), _renderableSceneComponents.end(),
        [this](const RenderableSceneComponent& left, const RenderableSceneComponent& right) {
            return CalculateSpaceObjectDistance(left.planet.get()) < CalculateSpaceObjectDistance(right.planet.get());
        });

    const int previous = _nearestPlanetIndex;
    if (nearestPlanetIt != _renderableSceneComponents.end()) {
        _nearestPlanetIndex = static_cast<int>(distance(_renderableSceneComponents.begin(), nearestPlanetIt));
    } else {
        _nearestPlanetIndex = -1;
    }
    if (_nearestPlanetIndex != previous) {
        NotifyNearestPlanetChanged(GetNearestPlanetIndexForJs());
    }
}

float Application::CalculateSpaceObjectDistance(const SpaceObject* spaceObject) const {
    return static_cast<float>(glm::distance(_camera.GetWorldPosition(), spaceObject->GetWorldPosition()));
}

glm::vec3 Application::CurrentFpsColor() const {
    const uint16_t fps = _frameTimer.GetCurrentFps();

    if (fps < 30)
        return {0.949, 0.239, 0.325};
    else if (fps >= 30 && fps < 60)
        return {0.949, 0.85, 0.325};
    else
        return {0.239, 0.949, 0.45};
}
