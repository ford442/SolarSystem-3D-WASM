// Application lifecycle and the per-frame loop. The rest of Application's members live
// alongside their subject: PlatformWindow.cpp (window/GL bring-up, resize, teardown),
// InputHandler.cpp, AudioPlayer.cpp, RenderSettings.cpp (quality presets + LOD),
// SceneRenderer.cpp / SceneOverlayRenderer.cpp (drawing), StarSystemFactory.cpp and
// PlanetSystemLoader.cpp (scene construction), XrSession.cpp (WebXR).
#include "Application.h"
#include "JsBridge.h"
#include "QualitySettings.h"
#include "ResourceManifest.h"
#include "SimState.h"
#include "WasmExports.h"
#include "assets/TextureLoadingQueue.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SceneScale.h"
#include "core/Log.h"
#include "render/XrPointerRenderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

using namespace std;

Application::Application() : _frameTimer(240) {
    SetActiveApplication(this);
    InitSystems();
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
    while (!glfwWindowShouldClose(_mainWindow)) {
        RunOneFrame();
    }
#endif
}

void Application::RunOneFrame() {
    _frameTimer.RunFrameTimer();

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

    const double currentFrame = glfwGetTime();
    _deltaTime = currentFrame - _lastFrame;
    _lastFrame = currentFrame;
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
    CarryCameraWithAnchor();
    _camera.UpdateTransition(_deltaTime);
    UpdateMissionFollow();

    ProcessInput(_mainWindow);
    ConstrainCamera();
    // After everything that moves the camera this frame, before anything reads positions.
    UpdateRenderOrigin();
    UpdatePlanetSystemLoading();
    UpdateMusicDucking();

#ifdef __EMSCRIPTEN__
    if (_xr.active) {
        RenderXrStereoFrame();
        TextureLoadingQueue::GetInstance().ProcessQueue();
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
    RenderFrame(1);
    _renderer.RenderHud(FrameContextFor(0));

    TextureLoadingQueue::GetInstance().ProcessQueue();
    _renderer.RenderTextureLoadingProgress(FrameContextFor(0));

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    UpdateBackgroundMusic();
#endif

    glfwSwapBuffers(_mainWindow);
    glfwPollEvents();
    EndFrame();
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

void Application::UpdateScene(double julianDate) {
    _renderer.CountSceneUpdate();
    RefreshPlanetProxyPositions();
    if (_sun) {
        _sun->TakeStarSystemCenter();
    }
    PlaceSceneComponents();

    for (auto& component : _renderableSceneComponents) {
        // Shadow frustum tracks the planet's current heliocentric position. Its depth range
        // is fitted to the component's own bodies — the only casters drawn into its map and
        // the only receivers that read it — instead of the camera's 0.001–20000, which
        // wasted precision and clipped everything past 20000 units from the Sun (Uranus and
        // beyond on realistic scale).
        const float extent = component.planet->GetRadius() * 3.0f;
        std::vector<BoundingSphere> bodies;
        component.ForEachObject([&](const SpaceObject& object) { bodies.push_back(object.GetBoundingSphere()); });
        const glm::vec3 toPlanet = component.planet->GetPosition() - _sun->GetPosition();
        const DepthRange depth = FitDepthRange(_sun->GetPosition(), glm::normalize(toPlanet), bodies,
                                               0.01f * extent + 0.05f, _camera.GetNear());
        component.shadowDepthRange = glm::vec2(depth.nearPlane, depth.farPlane - depth.nearPlane);
        const glm::mat4 lightProjection =
            glm::ortho(-extent, extent, -extent, extent, depth.nearPlane, depth.farPlane);
        // Built in double: the eye is the Sun, tens of thousands of units from the render
        // origin on realistic scale, and a float lookAt folds that into the light-space x/y
        // the shadow lookup reads. In double those reduce to the planet's own small offset.
        const glm::mat4 lightView(glm::lookAt(glm::dvec3(_sun->GetPosition()),
                                              glm::dvec3(component.planet->GetPosition()),
                                              glm::dvec3(0.0, 1.0, 0.0)));
        component.lightSpaceMatrix = lightProjection * lightView;
    }

    if (_asteroidField) {
        _asteroidField->Update(julianDate);
    }
    if (_kuiperField) {
        _kuiperField->Update(julianDate);
    }
    _missionProbes.resize(_missionCatalog.missions.size());
    for (size_t i = 0; i < _missionProbes.size(); ++i) {
        glm::vec3 probe(0.0f);
        _missionProbes[i] = SampleMissionScenePosition(static_cast<int>(i), probe) ? std::optional(probe) : std::nullopt;
    }
    UpdateLOD();
    UpdateSearchNearestPlanet();
    // Warm the lazily searched caches here so no pass is the one that pays for a search.
    GetNextSkyEvent();
    if (_observe.active) {
        CurrentObserveSky();
    }
}

void Application::PlaceBodies() {
    if (_sun) {
        _sun->TakeStarSystemCenter();
    }
    PlaceSceneComponents();
}

void Application::PlaceSceneComponents() {
    const bool physicalAtmosphere = _renderer.PhysicalAtmosphereAvailable();
    for (auto& component : _renderableSceneComponents) {
        component.planet->AdjustToParent();
        for (const auto& satellite : component.satellites) {
            satellite->AdjustToParent();
        }
        if (component.clouds) {
            component.clouds->AdjustToParent();
        }
        for (const auto& renderableAtmosphere : component.atmospheres) {
            // Before AdjustToParent: the two paths draw different shell sizes.
            renderableAtmosphere.atmosphere->SetPhysicalPathActive(physicalAtmosphere);
            renderableAtmosphere.atmosphere->AdjustToParent();
        }
        if (component.planetaryRing) {
            component.planetaryRing->AdjustToParent();
        }
    }
}

void Application::ReportFrameStats() {
    // At debug level (SOLARSYSTEM_LOG=debug / ?log=debug), log the previous frame's pass
    // counts every N frames: SOLARSYSTEM_FRAME_STATS=N, 120 by default.
    static const int every = [] {
        const char* value = std::getenv("SOLARSYSTEM_FRAME_STATS");
        return value ? std::max(0, std::atoi(value)) : 120;
    }();
    static unsigned long long frame = 0;
    const unsigned long long uniformCalls = Shader::UniformCallCount();
    Shader::ResetUniformCallCount();
    if (every > 0 && frame++ % static_cast<unsigned long long>(every) == 0) {
        const FramePasses::Stats& stats = _renderer.Stats();
        LOG_DEBUG("FrameStats",
                  "updates=%d viewBinds=%d shadowPasses=%d componentDraws=%d bodyDraws=%d culled=%d uniformCalls=%llu",
                  stats.sceneUpdates, stats.viewBinds, stats.shadowPasses, stats.componentDraws, stats.bodyDraws,
                  stats.culledComponents, uniformCalls);
    }
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

void Application::RenderFrame(int viewCount) {
    FramePasses::Hooks hooks;
    hooks.updateScene = [this, viewCount]() -> std::size_t {
        ReportFrameStats();
        _renderer.BeginFrame();
        UpdateScene(OrbitLayout::GetJulianDate());
        BuildFrameSnapshot(viewCount);
        CullScene(viewCount);
        return _observe.active ? 0 : _drawOrder.size();
    };
    hooks.beginView = [this](int view) { _renderer.BeginView(FrameContextFor(view)); };
    hooks.background = [this](int view) { _renderer.RenderBackground(FrameContextFor(view)); };
    hooks.shadowPass = [this](std::size_t draw) {
        _renderer.ShadowMapPass(FrameContextFor(0), _renderableSceneComponents[_drawOrder[draw]]);
    };
    hooks.drawComponent = [this](int view, std::size_t draw) {
        _renderer.RenderPass(FrameContextFor(view), _renderableSceneComponents[_drawOrder[draw]]);
    };
    hooks.foreground = [this](int view) {
        const FrameContext ctx = FrameContextFor(view);
        _renderer.RenderForeground(ctx);
        if (!ctx.xr) {
            _renderer.RenderMonoEffects(ctx);
        }
    };
    FramePasses::Run(viewCount, hooks);
}

void Application::CullScene(int viewCount) {
    std::vector<Frustum> frusta;
    for (int view = 0; view < viewCount; ++view) {
        frusta.emplace_back(_frameViews[static_cast<size_t>(view)].projection * _frameViews[static_cast<size_t>(view)].view);
    }
    const auto seen = [&](const SpaceObject& object) {
        const BoundingSphere sphere = object.GetBoundingSphere();
        return std::any_of(frusta.begin(), frusta.end(), [&](const Frustum& f) { return f.Intersects(sphere); });
    };

    _drawOrder.clear();
    int culled = 0;
    for (size_t i = 0; i < _renderableSceneComponents.size(); ++i) {
        RenderableSceneComponent& component = _renderableSceneComponents[i];
        bool any = false;
        component.ForEachObject([&](SpaceObject& object) {
            const bool visible = seen(object);
            object.SetVisible(visible);
            any = any || visible;
        });
        component.visible = any;
        culled += any ? 0 : 1;
        if (any || static_cast<int>(i) == _nearestPlanetIndex) {
            _drawOrder.push_back(i);
        }
    }
    _renderer.CountCulledComponents(culled);

    // Opaque surfaces front to back, so early depth rejects what is behind. Within a
    // component the order (planet, moons, shells, ring) stays as RenderPass has it.
    const glm::vec3 eye = CameraRenderPosition();
    const auto distance = [&](size_t index) {
        return glm::distance(eye, _renderableSceneComponents[index].planet->GetPosition());
    };
    std::stable_sort(_drawOrder.begin(), _drawOrder.end(),
                     [&](size_t a, size_t b) { return distance(a) < distance(b); });
}

void Application::BuildFrameSnapshot(int viewCount) {
    SceneSnapshot& scene = _frameScene;
    scene.sun = _sun.get();
    scene.components = &_renderableSceneComponents;
    scene.nearestComponent = _nearestPlanetIndex;
    scene.asteroidField = _asteroidField.get();
    scene.kuiperField = _kuiperField.get();
    scene.missions = &_missionCatalog;
    scene.focusedMission = _focusedMissionIndex;
    scene.missionProbes = &_missionProbes;
    scene.stagedSystems = &_planetSystemManifests;
    scene.observeActive = _observe.active;
    scene.observe = &_observe;
    scene.observeSky = _observe.active ? &CurrentObserveSky() : nullptr;
    scene.observeMoonTexture = _observe.active ? ObserveMoonTexture() : 0u;
    scene.nextSkyEvent = &GetNextSkyEvent();

    // The head: the mono camera, and the axes every eye shares.
    CameraView head;
    head.projection = _camera.GetProjectionMatrix();
    head.headPosition = CameraRenderPosition();
    head.view = _camera.GetViewMatrix(head.headPosition);
    head.renderOrigin = OrbitLayout::GetRenderOrigin();
    head.front = _camera.GetFrontVector();
    head.right = _camera.GetRightVector();
    head.up = _camera.GetUpVector();
    head.nearPlane = _camera.GetNear();
    head.farPlane = _camera.GetFar();
    head.aspect = _camera.GetAspect();
    head.zoomDeg = _camera.GetZoom();
    head.yawDeg = _camera.GetYaw();
    head.pitchDeg = _camera.GetPitch();
    head.logDepthC = _logDepthC;
    head.zCoef = static_cast<float>(2.0 / glm::log2(static_cast<double>(_logDepthC) * _camera.GetFar() + 1.0));
    head.viewport = glm::ivec4(0, 0, _displayWidth, _displayHeight);

    const int views = std::clamp(viewCount, 1, static_cast<int>(_frameViews.size()));
    for (int i = 0; i < views; ++i) {
        CameraView& view = _frameViews[static_cast<size_t>(i)];
        view = head;
#ifdef __EMSCRIPTEN__
        if (_xr.active) {
            const XrEyeState& eye = _xr.eyes[i];
            view.projection = eye.projection;
            view.view = eye.view;
            view.viewport = glm::ivec4(eye.viewportX, eye.viewportY, eye.viewportWidth, eye.viewportHeight);
            view.eye = i;
        }
#endif
        view.eyePosition = glm::vec3(glm::inverse(view.view)[3]);
    }

    HudState& hud = _frameHud;
    hud.fps = _frameTimer.GetCurrentFps();
    hud.fpsColor = CurrentFpsColor();
    hud.vsync = _isVertSyncEnabled;
    hud.cameraSpeed = _camera.GetMovementSpeed();
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    hud.musicVolumePercent = (_musicMuted ? 0.0f : _musicVolume) * 100.0f;
#elif !defined(__EMSCRIPTEN__)
    hud.musicVolumePercent = _soundEngine ? _soundEngine->getSoundVolume() * 100.0f : 0.0f;
#endif
    hud.musicTrack = _currentMusicTrack;
}

FrameContext Application::FrameContextFor(int view) const {
    const size_t index = static_cast<size_t>(std::clamp(view, 0, static_cast<int>(_frameViews.size()) - 1));
#ifdef __EMSCRIPTEN__
    const bool xr = _xr.active;
#else
    const bool xr = false;
#endif
    return FrameContext{_frameViews[index], _frameScene, _simState, DisplaySize(), xr, DefaultFramebuffer(), &_frameHud};
}


void Application::InitScene() {
#ifdef __EMSCRIPTEN__
    // Catalog JSON is in the .data preload; parse it before the first JS frame so
    // Explorer / deep links see Voyager rows before InitSceneObjects builds GL meshes.
    LoadMissionCatalog();
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
    _camera.SetAspect(static_cast<float>(_displayWidth) / static_cast<float>(_displayHeight));
#ifdef __EMSCRIPTEN__
    TextureImage2D::SetWebAnisotropyCap(_simState.isMobileWeb ? 4.0f : 8.0f);
#endif
    _renderer.Init(_simState, _ft, DisplaySize());
    _ft = nullptr; // Renderer::Init released it once the font was loaded
    LoadMissions();
    {
        const auto asteroidQuality = GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb);
        _asteroidField = make_unique<AsteroidField>(AsteroidField::kDefaultSeed,
                                                    asteroidQuality.asteroidInstanceCount);
        _kuiperField = make_unique<AsteroidField>(AsteroidField::kKuiperSeed,
                                                  asteroidQuality.kuiperInstanceCount,
                                                  AsteroidField::Population::Kuiper);
    }

    InitSongList();
    InitStarSystem();

    glfwShowWindow(_mainWindow);
#ifdef _WIN32
    glfwSetWindowMonitor(_mainWindow, glfwGetPrimaryMonitor(), 0, 0, _displayWidth, _displayHeight, GLFW_DONT_CARE);
#endif

    StartSearchNearestPlanet();
    StartPlayBackgroundMusic();
    ApplyScaleEnvOverride();
    ApplyCameraPoseEnvOverride();
    ApplyObserveEnvOverride();
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
    if (_observe.active) return; // the viewer stays on the ground in Observe
    StopMissionFollow();
    idx = std::clamp(idx, 0, OrbitLayout::kBodyCount - 1);
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

    const glm::vec3 direction = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f));
    const glm::dvec3 cameraPos = target + glm::dvec3(direction) * static_cast<double>((focusRadius + 30.0f) * sizeFactor);
    const glm::vec3 lookDir = -direction;
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(lookDir.y));
    _camera.StartTransitionTo(cameraPos, yaw, pitch, 2.0f);
    // Ride along with the body during the flight and after it lands, even from outside the
    // distance at which the camera would pick it up by itself.
    SetCameraAnchor(anchor, true);

#ifdef __EMSCRIPTEN__
    NotifyPlanetFocused(idx);
#endif
}

int Application::GetFocusedPlanetIndex() const {
    return _focusedPlanetIndex;
}

void Application::StopMissionFollow() {
    const bool wasFocused = _focusedMissionIndex >= 0;
    _missionFollowActive = false;
    _focusedMissionIndex = -1;
    if (wasFocused) {
        NotifyMissionFocused(-1);
    }
}

bool Application::SampleMissionHeliocentricPosition(int idx, glm::dvec3& outScene) const {
    if (idx < 0 || idx >= static_cast<int>(_missionCatalog.missions.size())) {
        return false;
    }
    glm::dvec3 au{0.0};
    if (!MissionCatalog::InterpolateAu(_missionCatalog.missions[static_cast<size_t>(idx)],
                                      OrbitLayout::GetJulianDate(), au)) {
        return false;
    }
    outScene = OrbitLayout::HelioAuToSceneDouble(au);
    return true;
}

bool Application::SampleMissionScenePosition(int idx, glm::vec3& outScene) const {
    glm::dvec3 heliocentric{0.0};
    if (!SampleMissionHeliocentricPosition(idx, heliocentric)) {
        return false;
    }
    outScene = OrbitLayout::ToRenderFrame(heliocentric);
    return true;
}

void Application::SetCameraHeliocentricPose(const glm::dvec3& position, float yaw, float pitch) {
    _camera.CancelTransition();
    _camera.SetWorldPosition(position);
    _camera.SetYawPitch(yaw, pitch);
    // A jump: whatever the camera rode along with is somewhere else now. ConstrainCamera
    // picks the body it arrived next to on the next frame.
    SetCameraAnchor(nullptr, false);
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
    _renderer.FollowMissionPathOrigin(_missionCatalog, _simState.qualityPreset);
}

void Application::FocusMissionByIndex(int idx) {
    if (idx < 0) {
        StopMissionFollow();
        return;
    }
    if (_observe.active) return; // the viewer stays on the ground in Observe
    if (idx >= static_cast<int>(_missionCatalog.missions.size())) {
        return;
    }
    _focusedPlanetIndex = -1;
    _focusedMissionIndex = idx;
    _missionFollowActive = true;
    NotifyMissionFocused(idx);

    glm::dvec3 target(0.0);
    if (!SampleMissionHeliocentricPosition(idx, target)) {
        return;
    }
    const glm::vec3 direction = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f));
    const glm::dvec3 cameraPos = target + glm::dvec3(direction) * 45.0;
    const glm::vec3 lookDir = -direction;
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
    _camera.StartTransitionTo(cameraPos, yaw, pitch, 2.0f);
}

int Application::GetFocusedMissionIndex() const {
    return _focusedMissionIndex;
}

int Application::GetMissionCount() const {
    return static_cast<int>(_missionCatalog.missions.size());
}

std::string Application::GetMissionCatalogJson() const {
    std::ostringstream ss;
    ss << '[';
    for (size_t i = 0; i < _missionCatalog.missions.size(); ++i) {
        const auto& mission = _missionCatalog.missions[i];
        if (i > 0) {
            ss << ',';
        }
        ss << R"({"id":")" << mission.id << R"(","name":")" << mission.name
           << R"(","index":)" << i << '}';
    }
    ss << ']';
    return ss.str();
}

std::string Application::GetFocusedMissionJson() const {
    if (_focusedMissionIndex < 0 ||
        _focusedMissionIndex >= static_cast<int>(_missionCatalog.missions.size())) {
        return R"({"valid":false})";
    }
    glm::dvec3 pos{0.0};
    if (!SampleMissionHeliocentricPosition(_focusedMissionIndex, pos)) {
        return R"({"valid":false})";
    }
    const auto& mission = _missionCatalog.missions[static_cast<size_t>(_focusedMissionIndex)];
    char buffer[384];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"valid":true,"id":"%s","index":%d,"x":%.4f,"y":%.4f,"z":%.4f})",
                  mission.id.c_str(), _focusedMissionIndex, pos.x, pos.y, pos.z);
    return buffer;
}

void Application::UpdateMissionFollow() {
    if (!_missionFollowActive || _focusedMissionIndex < 0 || _camera.IsTransitionActive()) {
        return;
    }
    glm::dvec3 target(0.0);
    if (!SampleMissionHeliocentricPosition(_focusedMissionIndex, target)) {
        return;
    }
    const glm::vec3 direction = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f));
    const glm::vec3 lookDir = -direction;
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
    _camera.SetWorldPosition(target + glm::dvec3(direction) * 45.0);
    _camera.SetYawPitch(yaw, pitch);
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

void Application::LoadMissionCatalog() {
    std::string error;
    if (!MissionCatalog::LoadFromFile("resource/missions/catalog.json", _missionCatalog, error)) {
        LOG_WARN("Missions", "%s", error.c_str());
        _missionCatalog = {};
        return;
    }
    LOG_INFO("Missions", "Loaded %zu trajectory path(s) from catalog.json", _missionCatalog.missions.size());
}

void Application::LoadMissions() {
    if (_missionCatalog.missions.empty()) {
        LoadMissionCatalog();
    }
    _renderer.RebuildMissionPaths(_missionCatalog, _simState.qualityPreset);
}

const SkyEvents::Conjunction& Application::GetNextConjunction() const {
    // How long to wait before retrying a search that found nothing, in simulated days.
    // Only reachable if the search window is ever shortened; the default window always hits.
    constexpr double kEmptyRetryDays = 30.0;

    const double jd = OrbitLayout::GetJulianDate();
    const bool passed = _nextConjunction.valid && jd > _nextConjunction.julianDate;
    const bool rewound = jd < _nextConjunctionComputedJd - 0.5;
    const bool retryEmpty = !_nextConjunction.valid && jd > _nextConjunctionComputedJd + kEmptyRetryDays;

    if (!_nextConjunctionCached || passed || rewound || retryEmpty) {
        _nextConjunction = SkyEvents::NextInnerPlanetConjunction(jd);
        _nextConjunctionComputedJd = jd;
        _nextConjunctionCached = true;
    }
    return _nextConjunction;
}

const SkyEvents::SkyEvent& Application::GetNextSkyEvent() const {
    // Same cache policy as GetNextConjunction(): recompute once the event is behind us,
    // once time is scrubbed backwards, or after a quiet stretch when nothing was found.
    constexpr double kEmptyRetryDays = 30.0;

    const double jd = OrbitLayout::GetJulianDate();
    const bool passed = _nextSkyEvent.valid && jd > _nextSkyEvent.julianDate;
    const bool rewound = jd < _nextSkyEventComputedJd - 0.5;
    const bool retryEmpty = !_nextSkyEvent.valid && jd > _nextSkyEventComputedJd + kEmptyRetryDays;

    if (!_nextSkyEventCached || passed || rewound || retryEmpty) {
        _nextSkyEvent = SkyEvents::NextEvent(jd);
        _nextSkyEventComputedJd = jd;
        _nextSkyEventCached = true;
    }
    return _nextSkyEvent;
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
    auto nearestPlanetIt = min_element(_renderableSceneComponents.begin(), _renderableSceneComponents.end(),
                       [this](const RenderableSceneComponent& left, const RenderableSceneComponent& right)
    {
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
