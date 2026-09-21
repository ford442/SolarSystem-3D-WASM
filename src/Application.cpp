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
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include "Solar_System/OrbitLayout.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <thread>

using namespace std;

Application::Application() : _fpsHandler(240), _renderer(*this) {
    gSimState = &_simState;
    SetActiveApplication(this);
    InitSystems();
    ApplyQualityPreset(gSimState->qualityPreset);
    InitScene();
}

Application::~Application() {
    if (GetActiveApplication() == this) {
        SetActiveApplication(nullptr);
        ResetSimStateToFallback();
    }
    Dispose();
}

void Application::Exec() {
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg([](void* arg) {
        static_cast<Application*>(arg)->RunOneFrame();
    }, this, 0, 1);
#else
    while (!glfwWindowShouldClose(_mainWindow)) {
        RunOneFrame();
    }
#endif
}

void Application::RunOneFrame() {
    _fpsHandler.RunFrameTimer();

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
             std::cout << "All resources downloaded. Initializing scene..." << std::endl;
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
#ifndef __EMSCRIPTEN__
        _fpsHandler.WaitForFrameTimer();
#endif
        return;
    }

    const double currentFrame = glfwGetTime();
    _deltaTime = currentFrame - _lastFrame;
    _lastFrame = currentFrame;

    gSimState->simDeltaSeconds = gSimState->timePaused ? 0.0f : static_cast<float>(_deltaTime) * gSimState->timeScale;
    OrbitLayout::Advance(gSimState->simDeltaSeconds);
#ifdef __EMSCRIPTEN__
    RefreshPlanetProxyPositions();
#endif

    _camera.UpdateTransition(_deltaTime);
    UpdateMissionFollow();

    ProcessInput(_mainWindow);
    UpdatePlanetSystemLoading();
    UpdateLOD();
    UpdateMusicDucking();

#ifdef __EMSCRIPTEN__
    if (_xr.active) {
        RenderXrStereoFrame();
        TextureLoadingQueue::GetInstance().ProcessQueue();
#ifdef SOLARSYSTEM_USE_SDL_MIXER
        UpdateBackgroundMusic();
#endif
        UpdateSearchNearestPlanet();
        glfwPollEvents();
        return;
    }
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, DefaultFramebuffer());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    RenderFrameContent();

    if (_renderer.isRenderPlanetStarDistances || _renderer.isRenderSatelliteDistances)
        _renderer.RenderPlanetSatelliteStarDistances();
    if (_renderer.isRenderHints)
        _renderer.RenderHints();

    TextureLoadingQueue::GetInstance().ProcessQueue();
    _renderer.RenderTextureLoadingProgress();

#ifdef __EMSCRIPTEN__
    UpdateSearchNearestPlanet();
#endif
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    UpdateBackgroundMusic();
#endif

    glfwSwapBuffers(_mainWindow);
    glfwPollEvents();

#ifndef __EMSCRIPTEN__
    _fpsHandler.WaitForFrameTimer();
#endif
}

void Application::RenderFrameContent() {
    _renderer.ConfigureMainShaders();
    _renderer.skyBox->Render(*_renderer.mainSkyBoxShader);
    _renderer.RenderOrbitPaths();
    _renderer.RenderMissionPaths();
    _renderer.RenderXrPointers();
    _renderer.RenderStarCorona();
    _renderer.ProcessSceneComponentsRendering();
    _renderer.RenderAsteroidField();
    _renderer.RenderMagneticFields();
#ifdef __EMSCRIPTEN__
    if (!_xr.active) {
        RenderPlanetProxyMarkers();
        _renderer.RenderStarEffects();
    }
#else
    _renderer.RenderStarEffects();
#endif
}


void Application::InitScene() {
#ifdef __EMSCRIPTEN__
    // Catalog JSON is in the .data preload; parse it before the first JS frame so
    // Explorer / deep links see Voyager rows before InitSceneObjects builds GL meshes.
    LoadMissionCatalog();
    LoadCoreResources();
    LoadOptionalSounds();
#else
    InitSceneObjects();
    _appState = AppState::RUNNING;
#endif
}

void Application::UpdateLoadingProgress() {
#ifdef __EMSCRIPTEN__
    const int loaded = _totalResources - _resourcesPending;
    NotifyLoadingProgress(loaded, _totalResources);
#endif
}


void Application::InitSceneObjects() {
    _camera.SetAspect(static_cast<float>(_displayWidth) / static_cast<float>(_displayHeight));
    _renderer.Init();
    LoadMissions();
    {
        const auto asteroidQuality = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);
        _asteroidField = make_unique<AsteroidField>(AsteroidField::kDefaultSeed,
                                                    asteroidQuality.asteroidInstanceCount);
    }

    InitSongList();
    InitStarSystem();

    glfwShowWindow(_mainWindow);
#ifdef _WIN32
    glfwSetWindowMonitor(_mainWindow, glfwGetPrimaryMonitor(), 0, 0, _displayWidth, _displayHeight, GLFW_DONT_CARE);
#endif

    StartSearchNearestPlanet();
    StartPlayBackgroundMusic();
}

void Application::FocusPlanetByIndex(int idx) {
    StopMissionFollow();
    idx = std::clamp(idx, 0, OrbitLayout::kBodyCount - 1);
    _focusedPlanetIndex = idx;

    glm::vec3 target(0.0f);
    float focusRadius = 10.0f;

    if (idx == 0) {
        if (_sun) {
            target = _sun->GetPosition();
        }
        focusRadius = 40.0f;
    } else {
        // Match on the planet's own name rather than position in the vector: staged loading
        // pushes components in camera-approach order, so the Nth component is not body N.
        const auto body = static_cast<OrbitLayout::Body>(idx);
        const Planet* found = nullptr;
        for (const auto& component : _renderableSceneComponents) {
            if (!component.planet) {
                continue;
            }
            const std::wstring& wideName = component.planet->GetEngName();
            const std::string name(wideName.begin(), wideName.end());
            if (OrbitLayout::BodyFromName(name) == body) {
                found = component.planet.get();
                break;
            }
        }

        if (found) {
            target = found->GetPosition();
            focusRadius = std::max(found->GetRadius() * 4.0f, 2.0f);
        } else {
            // Not loaded yet (web staged loading): fly to where the ephemeris says it is.
            target = OrbitLayout::GetOffset(body);
            focusRadius = 20.0f;
        }
    }

    const glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f)) * (focusRadius + 30.0f);
    const glm::vec3 cameraPos = target + offset;
    const glm::vec3 lookDir = glm::normalize(target - cameraPos);
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(lookDir.y));
    _camera.StartTransitionTo(cameraPos, yaw, pitch, 2.0f);

#ifdef __EMSCRIPTEN__
    NotifyPlanetFocused(idx);
#endif
}

int Application::GetFocusedPlanetIndex() const {
    return _focusedPlanetIndex;
}

void Application::StopMissionFollow() {
    _missionFollowActive = false;
    _focusedMissionIndex = -1;
}

bool Application::SampleMissionScenePosition(int idx, glm::vec3& outScene) const {
    if (idx < 0 || idx >= static_cast<int>(_missionCatalog.missions.size())) {
        return false;
    }
    glm::vec3 au{0.0f};
    if (!MissionCatalog::InterpolateAu(_missionCatalog.missions[static_cast<size_t>(idx)],
                                      OrbitLayout::GetJulianDate(), au)) {
        return false;
    }
    outScene = OrbitLayout::HelioAuToScene(au);
    if (_sun) {
        outScene += _sun->GetPosition();
    }
    return true;
}

void Application::FocusMissionByIndex(int idx) {
    if (idx < 0) {
        StopMissionFollow();
        return;
    }
    if (idx >= static_cast<int>(_missionCatalog.missions.size())) {
        return;
    }
    _focusedPlanetIndex = -1;
    _focusedMissionIndex = idx;
    _missionFollowActive = true;

    glm::vec3 target(0.0f);
    if (!SampleMissionScenePosition(idx, target)) {
        return;
    }
    const glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f)) * 45.0f;
    const glm::vec3 cameraPos = target + offset;
    const glm::vec3 lookDir = glm::normalize(target - cameraPos);
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
    glm::vec3 pos{0.0f};
    if (!SampleMissionScenePosition(_focusedMissionIndex, pos)) {
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
    glm::vec3 target(0.0f);
    if (!SampleMissionScenePosition(_focusedMissionIndex, target)) {
        return;
    }
    const glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f)) * 45.0f;
    const glm::vec3 cameraPos = target + offset;
    const glm::vec3 lookDir = glm::normalize(target - cameraPos);
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(glm::clamp(lookDir.y, -1.0f, 1.0f)));
    _camera.SetPosition(cameraPos);
    _camera.SetYawPitch(yaw, pitch);
}

void Application::SetXrControllerRay(int hand, float ox, float oy, float oz, float dx, float dy, float dz,
                                     int visible) {
    if (!_renderer.xrPointerRenderer) {
        return;
    }
    _renderer.xrPointerRenderer->SetRay(hand, glm::vec3(ox, oy, oz), glm::vec3(dx, dy, dz), visible != 0);
}

void Application::SetOrbitLinesEnabled(bool enabled) { _renderer.SetOrbitLinesEnabled(enabled); }
bool Application::GetOrbitLinesEnabled() const { return _renderer.GetOrbitLinesEnabled(); }
void Application::SetMagneticFieldsEnabled(bool enabled) { _renderer.SetMagneticFieldsEnabled(enabled); }
bool Application::GetMagneticFieldsEnabled() const { return _renderer.GetMagneticFieldsEnabled(); }
void Application::ForEachEnabledMagneticField(
    const std::function<void(const SpaceObject& object, const MagneticFieldParams& params)>& fn) const {
    _renderer.ForEachEnabledMagneticField(fn);
}

void Application::LoadMissionCatalog() {
    std::string error;
    if (!MissionCatalog::LoadFromFile("resource/missions/catalog.json", _missionCatalog, error)) {
        std::cout << "[Missions] " << error << std::endl;
        _missionCatalog = {};
        return;
    }
    std::cout << "[Missions] Loaded " << _missionCatalog.missions.size()
              << " trajectory path(s) from catalog.json" << std::endl;
}

void Application::LoadMissions() {
    if (_missionCatalog.missions.empty()) {
        LoadMissionCatalog();
    }
    _renderer.RebuildMissionPaths();
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
    return static_cast<int>(_nearestPlanetIndex) + 1;
}

void Application::StartSearchNearestPlanet() {
    _isSearchNearestPlanet = true;

#ifdef __EMSCRIPTEN__
    _nearestPlanetSearchFrameCounter = 0;
#else
    auto searchNearestPlanet = [=]() {
        return min_element(_renderableSceneComponents.begin(), _renderableSceneComponents.end(),
                           [=](const RenderableSceneComponent& left, const RenderableSceneComponent& right)
        {
            return CalculateSpaceObjectDistance(left.planet.get()) < CalculateSpaceObjectDistance(right.planet.get());
        });
    };

    _searchNearestPlanetThread = make_unique<thread>([=]() {
        while (_isSearchNearestPlanet) {
            auto nearestPlanetIt = searchNearestPlanet();
            if (nearestPlanetIt != _renderableSceneComponents.end()) {
                _nearestPlanetIndex = distance(_renderableSceneComponents.begin(), nearestPlanetIt);
            } else {
                _nearestPlanetIndex = -1;
            }

            this_thread::sleep_for(25ms);
        }
    });
#endif
}

void Application::UpdateSearchNearestPlanet() {
#ifdef __EMSCRIPTEN__
    if (_isSearchNearestPlanet && ++_nearestPlanetSearchFrameCounter >= 60) {
        _nearestPlanetSearchFrameCounter = 0;

        auto nearestPlanetIt = min_element(_renderableSceneComponents.begin(), _renderableSceneComponents.end(),
                           [this](const RenderableSceneComponent& left, const RenderableSceneComponent& right)
        {
            return CalculateSpaceObjectDistance(left.planet.get()) < CalculateSpaceObjectDistance(right.planet.get());
        });

        if (nearestPlanetIt != _renderableSceneComponents.end()) {
            _nearestPlanetIndex = distance(_renderableSceneComponents.begin(), nearestPlanetIt);
        } else {
            _nearestPlanetIndex = -1;
        }
    }
#endif
}

void Application::StopSearchNearestPlanet() {
    _isSearchNearestPlanet = false;
#ifndef __EMSCRIPTEN__
    if (_searchNearestPlanetThread)
        _searchNearestPlanetThread->join();
#endif
}

float Application::CalculateSpaceObjectDistance(const SpaceObject* spaceObject) const {
    return glm::distance(_camera.GetPosition(), spaceObject->GetPosition());
}

glm::vec3 Application::CurrentFpsColor() const {
    const uint16_t fps = _fpsHandler.GetCurrentFps();

    if (fps < 30)
        return {0.949, 0.239, 0.325};
    else if (fps >= 30 && fps < 60)
        return {0.949, 0.85, 0.325};
    else
        return {0.239, 0.949, 0.45};
}
