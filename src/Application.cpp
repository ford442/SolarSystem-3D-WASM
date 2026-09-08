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
#include <iostream>
#include <thread>

using namespace std;

Application::Application() : _fpsHandler(240) {
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
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

    ProcessInput(_mainWindow);
    UpdatePlanetSystemLoading();
    UpdateLOD();

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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    RenderFrameContent();

    if (_isRenderPlanetStarDistances || _isRenderSatelliteDistances)
        RenderPlanetSatelliteStarDistances();
    if (_isRenderHints)
        RenderHints();

    TextureLoadingQueue::GetInstance().ProcessQueue();
    RenderTextureLoadingProgress();

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
    ConfigureMainShaders();
    _skyBox->Render(*_mainSkyBoxShader);
    RenderOrbitPaths();
    RenderStarCorona();
    ProcessSceneComponentsRendering();
    RenderAsteroidField();
    RenderMagneticFields();
#ifdef __EMSCRIPTEN__
    if (!_xr.active) {
        RenderPlanetProxyMarkers();
        RenderStarEffects();
    }
#else
    RenderStarEffects();
#endif
}


void Application::InitScene() {
#ifdef __EMSCRIPTEN__
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
    const auto qualitySettings = GetQualitySettings(
        gSimState->shadowQuality == 0 ? gSimState->qualityPreset : gSimState->shadowQuality - 1, gSimState->isMobileWeb);
    _shadowMapFBO = make_unique<ShadowMapFBO>(qualitySettings.shadowResolution, qualitySettings.shadowResolution);
    _hdrEnabled = qualitySettings.enableHdr;
    _hdrShader = make_unique<Shader>("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    _hdr = make_unique<HDR>(*_hdrShader, _displayWidth, _displayHeight, _hdrEnabled);
    LogQualityTier(qualitySettings, _hdrEnabled, gSimState->shadowQuality);

    const vector<string> skyBoxFaces = GetSkyBoxFaces();

    _skyBox = make_unique<SkyBox>(skyBoxFaces);
    _mainTextShader = make_unique<Shader>("resource/shaders/text.vs", "resource/shaders/text.fs");
    _textRenderer = make_unique<TextRenderer>(_ft, "resource/fonts/Arial.ttf");
    FT_Done_FreeType(_ft);
    _shadowMapShader = make_unique<Shader>("resource/shaders/shadowMap.vs", "resource/shaders/shadowMap.fs");
    _mainSkyBoxShader = make_unique<Shader>("resource/shaders/skyBox.vs", "resource/shaders/skyBox.fs");
    _mainStarShader = make_unique<Shader>("resource/shaders/star.vs", "resource/shaders/star.fs");
    _mainCoronaStarShader = make_unique<Shader>("resource/shaders/starCorona.vs", "resource/shaders/starCorona.fs");
    _mainPlanetShader = make_unique<Shader>("resource/shaders/planetLighting.vs", "resource/shaders/planetLighting.fs");
    _mainAtmosphereShader = make_unique<Shader>("resource/shaders/atmosphere.vs", "resource/shaders/atmosphere.fs");
    _mainCloudsShader = make_unique<Shader>("resource/shaders/planetLighting.vs", "resource/shaders/cloudsLighting.fs");
    _mainRingShader = make_unique<Shader>("resource/shaders/planetaryRingLighting.vs", "resource/shaders/planetaryRingLighting.fs");
    _lensFlareShader = make_unique<Shader>("resource/shaders/lensFlare.vs", "resource/shaders/lensFlare.fs");
    _lensFlare = make_unique<LensFlare>(*_lensFlareShader, TextureImage2D("resource/textures_low/flares_bright_Low.dds"),
            FlaresInfo {4,
            {
                FlareSprite{false, 1.0, 7.0, 0},
                FlareSprite{false, 1.35, 0.3, 1},
                FlareSprite{false, 1.5, 0.4, 4},
                FlareSprite{false, 1.7, 0.6, 5},
                FlareSprite{false, 1.9, 1.2, 6},
                FlareSprite{false, 2.1, 0.4, 2},
                FlareSprite{false, 2.25, 0.2, 3},
                FlareSprite{false, 2.75, 2.0, 7}
            }});
    _orbitPathRenderer = make_unique<OrbitPathRenderer>();
    _magneticFieldRenderer = make_unique<MagneticFieldLineRenderer>();
    {
        const auto fieldQuality = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);
        _magneticFieldBloom = make_unique<MagneticFieldBloom>(_displayWidth, _displayHeight,
                                                              fieldQuality.enableMagneticBloom);
    }
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
