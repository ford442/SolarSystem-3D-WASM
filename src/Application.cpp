#include "Application.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <random>
#include <iomanip>
#include <iostream>
#include <filesystem>

using namespace std;

// Time and sim controls (enhancement)
float gTimeScale = 1.0f;
bool gTimePaused = false;
bool gAdvanceStep = false;

#ifdef __EMSCRIPTEN__
extern "C" {
    EMSCRIPTEN_KEEPALIVE void SetCameraPose(float x, float y, float z, float yaw, float pitch) {
        camera.SetPosition(glm::vec3(x, y, z));
        camera.SetYawPitch(yaw, pitch);
    }
    EMSCRIPTEN_KEEPALIVE void SetQualityPreset(int preset) {
        g_qualityPreset = (preset < 0 ? 0 : preset > 2 ? 2 : preset);
        std::cout << "[Quality] Preset set to " << g_qualityPreset 
                  << " (0=low,1=medium,2=full)" << std::endl;
    }
    EMSCRIPTEN_KEEPALIVE void FocusPlanet(int idx) {
        // simple focus presets for web (approx pos, 2s transition)
        glm::vec3 p(0); float r=10;
        if(idx==1){p=glm::vec3(1500,0,350); r=2;} //merc
        else if(idx==2){p=glm::vec3(1125,0,-1340);r=3;}
        else if(idx==3){p=glm::vec3(1900,0,0);r=3;}
        else if(idx==4){p=glm::vec3(-1732,0,1000);r=2;}
        else if(idx==5){p=glm::vec3(1350,0,1737);r=20;}
        else if(idx==6){p=glm::vec3(0,-100,2450);r=50;}
        else if(idx==7){p=glm::vec3(0,0,-2650);r=20;}
        else if(idx==8){p=glm::vec3(-2900,0,0);r=20;}
        else if(idx==9){p=glm::vec3(2800,0,1757);r=3;}
        glm::vec3 off = glm::normalize(glm::vec3(0.7,0.3,0.7)) * (r+30);
        glm::vec3 tp = p + off;
        glm::vec3 d = glm::normalize(p - tp);
        float ty = glm::degrees(std::atan2(d.z, d.x));
        float tpitch = glm::degrees(std::asin(d.y));
        camera.StartTransitionTo(tp, ty, tpitch, 2.0f);
    }
}
#endif

// Global for quality presets (set from JS or URL). Visible to planet LOD code.
int g_qualityPreset = 2; // 0=low,1=medium,2=full

// Helper function to get texture path with fallback for WebAssembly
std::string GetTexturePath(const std::string& lowRes, const std::string& highRes) {
#ifdef __EMSCRIPTEN__
    if (std::filesystem::exists(lowRes)) {
        return lowRes;
    } else {
        std::cerr << "Warning: Low-res texture missing: " << lowRes << ". Falling back to high-res." << std::endl;
        return highRes;
    }
#else
    return highRes;
#endif
}

namespace {
    constexpr std::array<const char*, 6> kLowSkyBoxFaces = {
        "resource/textures_low/Main_SkyBox/PositiveX.dds",
        "resource/textures_low/Main_SkyBox/NegativeX.dds",
        "resource/textures_low/Main_SkyBox/PositiveY.dds",
        "resource/textures_low/Main_SkyBox/NegativeY.dds",
        "resource/textures_low/Main_SkyBox/PositiveZ.dds",
        "resource/textures_low/Main_SkyBox/NegativeZ.dds"
    };

    constexpr std::array<const char*, 6> kHighSkyBoxFaces = {
        "resource/textures/Main_SkyBox/PositiveX.dds",
        "resource/textures/Main_SkyBox/NegativeX.dds",
        "resource/textures/Main_SkyBox/PositiveY.dds",
        "resource/textures/Main_SkyBox/NegativeY.dds",
        "resource/textures/Main_SkyBox/PositiveZ.dds",
        "resource/textures/Main_SkyBox/NegativeZ.dds"
    };

    std::vector<std::string> GetSkyBoxFaces() {
        std::vector<std::string> faces;
        faces.reserve(kLowSkyBoxFaces.size());
        for (size_t i = 0; i < kLowSkyBoxFaces.size(); ++i) {
            faces.push_back(GetTexturePath(kLowSkyBoxFaces[i], kHighSkyBoxFaces[i]));
        }
        return faces;
    }
}

// Error Callback
void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error (" << error << "): " << description << std::endl;
}

Application::Application() : _fpsHandler(240) {
    InitSystems();
    InitScene();
}

void Application::InitSystems() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw runtime_error("Failed to initialize GLFW");
    }

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
#endif

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if (mode) {
        _displayWidth = mode->width;
        _displayHeight = mode->height;
    } else {
        _displayWidth = 1280;
        _displayHeight = 720;
    }
    if (_displayWidth == 0) _displayWidth = 800;
    if (_displayHeight == 0) _displayHeight = 600;

    _mainWindow = glfwCreateWindow(_displayWidth, _displayHeight, "SolarSystem", nullptr, nullptr);

    if (_mainWindow == nullptr) {
        glfwTerminate();
        throw runtime_error("Failed to create GLFW window");
    }

#ifdef __EMSCRIPTEN__
    glfwSetInputMode(_mainWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetMouseButtonCallback(_mainWindow, [](GLFWwindow* window, int button, int action, int mods) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    });
#else
    glfwSetInputMode(_mainWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#endif

    glfwMakeContextCurrent(_mainWindow);
    glfwSetFramebufferSizeCallback(_mainWindow, FramebufferSizeCallback);
    glfwSetCursorPosCallback(_mainWindow, MouseCallback);
    glfwSetScrollCallback(_mainWindow, ScrollCallback);
    glfwSetKeyCallback(_mainWindow, KeyCallback);

#ifndef __EMSCRIPTEN__
    glewExperimental = true;
    glewInit();
#endif

    FT_Init_FreeType(&_ft);

#ifdef __EMSCRIPTEN__
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "Warning: Failed to init SDL_mixer: " << Mix_GetError() << std::endl;
    } else {
        Mix_AllocateChannels(16);
    }
#else
    _soundEngine = createIrrKlangDevice(ESOD_AUTO_DETECT, ESEO_MULTI_THREADED | ESEO_LOAD_PLUGINS);
    if (!_soundEngine) {
        throw runtime_error("Failed to init sound engine");
    }
    _soundEngine->setSoundVolume(0.3);
#endif

#ifndef __EMSCRIPTEN__
    if (SDL_Init(SDL_INIT_EVERYTHING)) {
        Dispose();
        throw runtime_error("Failed to init SDL");
    }
#endif

    if (!IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG)) {
        Dispose();
        std::string msg = "Failed to init SDL_Image: ";
        msg += IMG_GetError();
        throw runtime_error(msg);
    }

    glEnable(GL_DEPTH_TEST);
#ifndef __EMSCRIPTEN__
    glEnable(GL_MULTISAMPLE);
#endif
    glEnable(GL_CULL_FACE);

#ifndef __EMSCRIPTEN__
    glEnable(GL_POLYGON_SMOOTH);
    LoadWindowIcon();
#endif

    glCullFace(GL_BACK);
    DisplaySystemInformation();
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
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        
        // Update progress bar
        UpdateLoadingProgress();

        if (_resourcesPending <= 0) {
             std::cout << "All resources downloaded. Initializing scene..." << std::endl;
             InitSceneObjects();
             _appState = AppState::RUNNING;
        }

        glfwSwapBuffers(_mainWindow);
        glfwPollEvents();
#ifndef __EMSCRIPTEN__
        _fpsHandler.WaitForFrameTimer();
#endif
        return;
    }

    const double currentFrame = glfwGetTime();
    deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    camera.UpdateTransition(deltaTime);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ProcessInput(_mainWindow);
    UpdatePlanetSystemLoading();
    UpdateLOD();
    ConfigureMainShaders();
    _skyBox->Render(*_mainSkyBoxShader);
    RenderStarCorona();
    ProcessSceneComponentsRendering();
#ifdef __EMSCRIPTEN__
    RenderPlanetProxyMarkers();
#endif
    RenderStarEffects();

    if (isRenderPlanetStarDistances || isRenderSatelliteDistances)
        RenderPlanetSatelliteStarDistances();
    if (isRenderHints)
        RenderHints();

    // Process texture loading queue
    TextureLoadingQueue::GetInstance().ProcessQueue();
    RenderTextureLoadingProgress();

#ifdef __EMSCRIPTEN__
    UpdateSearchNearestPlanet();
    UpdateBackgroundMusic();
#endif

    glfwSwapBuffers(_mainWindow);
    glfwPollEvents();

#ifndef __EMSCRIPTEN__
    _fpsHandler.WaitForFrameTimer();
#endif
}

void Application::ProcessSceneComponentsRendering() {
    for (const auto& renderableSceneComponent : _renderableSceneComponents) {
        ShadowMapPass(renderableSceneComponent);
        RenderPass(renderableSceneComponent);
    }
}

void Application::ShadowMapPass(const RenderableSceneComponent& component) {
    glBindFramebuffer(GL_FRAMEBUFFER, _shadowMapFBO->GetFBO());
    glClear(GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _shadowMapFBO->GetShadowMapWidth(), _shadowMapFBO->GetShadowMapHeight());

    _shadowMapShader->Use();
    _shadowMapShader->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);

    component.planet->SetShader(*_shadowMapShader);
    component.planet->AdjustToParent(gTimePaused ? 0.0f : gTimeScale);
    component.planet->Render();

    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*_shadowMapShader);
        satellite->AdjustToParent(gTimePaused ? 0.0f : gTimeScale);
        satellite->Render();
    }

    RenderPlanetaryRing(*_shadowMapShader, component.planetaryRing.get(), component.lightSpaceMatrix);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::RenderPass(const RenderableSceneComponent& component) {
    glViewport(0, 0, _displayWidth, _displayHeight);
    _mainPlanetShader->Use();

    ConfigureMainPlanetShader(component);

    component.planet->SetShader(*_mainPlanetShader);
    component.planet->AdjustToParent(gTimePaused ? 0.0f : gTimeScale);
    component.planet->Render();

    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*_mainPlanetShader);
        satellite->AdjustToParent(gTimePaused ? 0.0f : gTimeScale);
        satellite->Render();
    }

    if (_nearestPlanetIndex >= 0
        && static_cast<size_t>(_nearestPlanetIndex) < _renderableSceneComponents.size()
        && component.planet == _renderableSceneComponents[static_cast<size_t>(_nearestPlanetIndex)].planet)
        ProcessStarRendering();

    RenderAtmospheres(component.atmospheres, component.lightSpaceMatrix, component.planetaryRing.get());
    RenderClouds(component.clouds.get(), component.lightSpaceMatrix);
    RenderPlanetaryRing(*_mainRingShader, component.planetaryRing.get(), component.lightSpaceMatrix);
}

void Application::RenderAtmospheres(const std::vector<RenderableAtmosphere>& renderableAtmospheres, const glm::mat4& lightSpaceMatrix, const PlanetaryRing* ring) const {
    if (!renderableAtmospheres.empty()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        _mainAtmosphereShader->Use();
        _mainAtmosphereShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);

        for (const auto& renderableAtmosphere : renderableAtmospheres) {
            _mainAtmosphereShader->SetVec3("camPosition", camera.GetPosition() - renderableAtmosphere.atmosphere->GetPosition());
            _mainAtmosphereShader->SetVec3("lightPos", _sun->GetPosition() - renderableAtmosphere.atmosphere->GetPosition());
            _mainAtmosphereShader->SetVec3("mieTint", renderableAtmosphere.atmosphere->GetMieTint());
            _mainAtmosphereShader->SetFloat("SCALE_H_FACTOR", renderableAtmosphere.hScaleFactor);
            _mainAtmosphereShader->SetFloat("SCALE_L_FACTOR", 1.0f);
            _mainAtmosphereShader->SetFloat("earthSizeCoefficient", renderableAtmosphere.parentEarthSizeCoefficient);
            _mainAtmosphereShader->SetBool("isUseToneMapping", renderableAtmosphere.isUseToneMapping);
            _mainAtmosphereShader->SetBool("isNearbyPlanetaryRing", ring != nullptr);

            if (ring) {
                _mainAtmosphereShader->SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
                _mainAtmosphereShader->SetFloat("ringParentPlanetRadiusSquared", ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
                _mainAtmosphereShader->SetBool("isUseSphereIntersect", ring->GetParent() != renderableAtmosphere.atmosphere->GetParent());

                _mainAtmosphereShader->SetVec3("ringCenter", ring->GetPosition());
                _mainAtmosphereShader->SetVec3("ringNormal", ring->GetRingNormal());
                _mainAtmosphereShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
                _mainAtmosphereShader->SetInt("ringDiffuse", 9);
                glBindTextureUnit(9, ring->GetRingTexture());
            }

            if (CalculateSpaceObjectDistance(renderableAtmosphere.atmosphere.get()) <= renderableAtmosphere.atmosphere->GetAtmosphereOuterBoundary())
                glFrontFace(GL_CW);

            renderableAtmosphere.atmosphere->AdjustToParent();
            renderableAtmosphere.atmosphere->Render();

            glFrontFace(GL_CCW);
        }

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Application::RenderClouds(Clouds* renderableClouds, const glm::mat4& lightSpaceMatrix) const {
    if (renderableClouds) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
        glDisable(GL_CULL_FACE);

        _mainCloudsShader->Use();
        _mainCloudsShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
        renderableClouds->AdjustToParent(gTimePaused ? 0.0f : gTimeScale);
        renderableClouds->Render();

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Application::RenderPlanetaryRing(const Shader& shader, PlanetaryRing* planetaryRing, const glm::mat4& lightSpaceMatrix) const {
    if (planetaryRing) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shader.Use();
        shader.SetMat4("lightSpaceMatrix", lightSpaceMatrix);
        planetaryRing->SetShader(shader);
        planetaryRing->AdjustToParent();
        planetaryRing->Render();

        glDisable(GL_BLEND);
    }
}

void Application::ProcessStarRendering() {
#ifdef __EMSCRIPTEN__
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    _sun->SetVisibility(1.0f);
#else
    glDepthMask(GL_FALSE);
    glBeginQuery(GL_SAMPLES_PASSED, _sun->GetStarOcclusionValue(0));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEndQuery(GL_SAMPLES_PASSED);

    glBeginQuery(GL_SAMPLES_PASSED, _sun->GetStarOcclusionValue(1));
    RenderStar();
    glEndQuery(GL_SAMPLES_PASSED);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    UpdateOcclusionQuery();
#endif
}

void Application::RenderStarCorona() const {
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    _mainCoronaStarShader->Use();
    _sun->SetShader(*_mainCoronaStarShader);
    _sun->TakeStarSystemCenter();
    _sun->Render();
    _sun->SetShader(*_mainStarShader);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Application::RenderStar() const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    _mainStarShader->Use();
    _sun->TakeStarSystemCenter();
    _sun->Render();

    glDisable(GL_BLEND);
}

void Application::RenderStarEffects() const {
    const PlanetaryRing* nearestPlanetaryRing = nullptr;
    if (!_renderableSceneComponents.empty()
        && _nearestPlanetIndex >= 0
        && static_cast<size_t>(_nearestPlanetIndex) < _renderableSceneComponents.size()) {
        nearestPlanetaryRing = _renderableSceneComponents[static_cast<size_t>(_nearestPlanetIndex)].planetaryRing.get();
    }

    optional<RingCameraInfo> ringCameraInfo;
    if (nearestPlanetaryRing) {
        ringCameraInfo = {camera.GetPosition(), nearestPlanetaryRing->GetPosition(), nearestPlanetaryRing->GetRingNormal(),
                          glm::vec2(nearestPlanetaryRing->GetInnerRadius(), nearestPlanetaryRing->GetOuterRadius()),
                          nearestPlanetaryRing->GetRingTexture()};
    }

    glBindFramebuffer(GL_FRAMEBUFFER, _hdr->GetHdrFBO());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    _sun->RenderGlow(_cameraProjection, _cameraView, camera.GetFrontVector() - camera.GetRightVector(), camera.GetAspect(),
                     CalculateSpaceObjectDistance(_sun.get()), ringCameraInfo, starTemperatureInKelvin);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    _hdr->Render(starExposure, starGamma);
    float intensity = glm::min(_sun->GetCurrentGlowSize() * _sun->GetVisibility(), 1.0f);
    _lensFlare->Render(_cameraProjection, _cameraView, _sun->GetPosition(), glm::vec3(1.0), camera.GetAspect(), 0.1, intensity, ringCameraInfo);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Application::RenderPlanetSatelliteStarDistances() const {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (isRenderPlanetStarDistances)
        RenderSpaceObjectDistance(_sun.get());

    for(const auto& renderableComponentPS : _renderableSceneComponents) {
        if (isRenderPlanetStarDistances) {
            RenderSpaceObjectDistance(renderableComponentPS.planet.get());
        }

        if (isRenderSatelliteDistances) {
            for(const auto& satellite : renderableComponentPS.satellites) {
                RenderSpaceObjectDistance(satellite.get());
            }
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Application::RenderSpaceObjectDistance(const SpaceObject* spaceObject) const {
    // Reuse pre-allocated container to eliminate per-frame heap allocation
    _distanceInfoCache.clear();
    _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetEngName().begin(), spaceObject->GetEngName().end());

    if (!spaceObject->GetOtherLangName().empty()) {
        _distanceInfoCache.push_back(L'[');
        _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetOtherLangName().begin(), spaceObject->GetOtherLangName().end());
        _distanceInfoCache.push_back(L']');
        _distanceInfoCache.push_back(L' ');
    }

    wstring distance(to_wstring(static_cast<uint16_t>(CalculateSpaceObjectDistance(spaceObject))));
    _distanceInfoCache.insert(_distanceInfoCache.end(), make_move_iterator(distance.begin()), make_move_iterator(distance.end()));

    _mainTextShader->Use();
    _mainTextShader->SetVec3("particleCenterWorldSpace", spaceObject->GetPosition());
    _mainTextShader->SetBool("is3D", true);
    _textRenderer->Render(*_mainTextShader, _distanceInfoCache, 0.0, 0.0, 0.075, glm::vec3(0.98431, 0.80784, 0.69412));
}

void Application::RenderHints() const {
    static const glm::mat4 textProjection = glm::ortho(0.0f, static_cast<float>(_displayWidth), 0.0f, static_cast<float>(_displayHeight));
    static const string gpuHintString = string(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    static constexpr glm::vec3 textColor = glm::vec3(0.98431, 0.80784, 0.69412);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", textProjection);
    _mainTextShader->SetBool("is3D", false);

    // Reuse pre-allocated containers to eliminate per-frame heap allocations
    _fpsHintCache.clear();
    _fpsHintCache.emplace_back(L"FPS: ");
    _fpsHintCache.emplace_back(to_wstring(_fpsHandler.GetCurrentFps()));

    _gpuHintCache.clear();
    _gpuHintCache.emplace_back(wstring(gpuHintString.begin(), gpuHintString.end()));

    _soundVolumeHintCache.clear();
    stringstream soundVolumeStream;
#ifdef __EMSCRIPTEN__
    soundVolumeStream << fixed << setprecision(0) << (static_cast<float>(Mix_VolumeMusic(-1)) / MIX_MAX_VOLUME) * 100.0;
#else
    soundVolumeStream << fixed << setprecision(0) << _soundEngine->getSoundVolume() * 100.0;
#endif
    string soundVolume = soundVolumeStream.str();
    _soundVolumeHintCache.emplace_back(wstring(L"Sound volume(PgUp/PgDown): ").append(soundVolume.begin(), soundVolume.end()).append(L" %"));

    _tmpStringCache.clear();
    _tmpStringCache.emplace_back(wstring(_currentMusicTrack.begin(), _currentMusicTrack.end()));

    deque<wstring> timeRunHint;
    timeRunHint.emplace_back(L"Time (P:pause, +/-:scale, .:step): ");
    std::wstringstream wss;
    wss << (gTimePaused ? L"paused" : L"run") << L" x" << static_cast<int>(gTimeScale);
    timeRunHint.emplace_back( wss.str() );

    deque<wstring> planetStarHint;
    planetStarHint.emplace_back(L"Planet/Star distances(Z): ");
    planetStarHint.emplace_back((isRenderPlanetStarDistances) ? L"On" : L"Off");

    deque<wstring> satelliteHint;
    satelliteHint.emplace_back(L"Satellite distances(X): ");
    satelliteHint.emplace_back((isRenderSatelliteDistances) ? L"On" : L"Off");

#ifdef __EMSCRIPTEN__
    // Indicate low-res start + high-res streaming (visual in streaming-progress + on-screen when active)
    deque<wstring> textureHint;
    textureHint.emplace_back(L"Web: low-res start; high-res streams when close (see HUD)");
#endif

    deque<wstring> cameraSpeedHint;
    cameraSpeedHint.emplace_back(L"Camera speed(1/2): ");
    cameraSpeedHint.emplace_back(to_wstring(camera.GetMovementSpeed()));

    deque<wstring> smoothCameraHint;
    smoothCameraHint.emplace_back(L"Smooth camera(Arrows)");

    deque<wstring> smoothZoomHint;
    smoothZoomHint.emplace_back(L"Smooth zoom(V/B)");

    deque<wstring> movementHint;
    movementHint.emplace_back(L"Move up/down(SPACE/C)");

    deque<wstring> speedBostHint;
    speedBostHint.emplace_back(L"Speed boost(SHIFT)");

    deque<wstring> starExposureHint;
    starExposureHint.emplace_back(L"Star Exposure(3/4): ");
    starExposureHint.emplace_back(to_wstring(starExposure));

    deque<wstring> starGammaHint;
    starGammaHint.emplace_back(L"Star Gamma(5/6): ");
    starGammaHint.emplace_back(to_wstring(starGamma));

    deque<wstring> starTemperatureHint;
    stringstream  starTemperatureStream;
    starTemperatureStream << fixed << setprecision(0) << starTemperatureInKelvin;
    string starTemperatureStr = starTemperatureStream.str();
    starTemperatureHint.emplace_back(wstring(L"Star Temperature(7/8): ").append(make_move_iterator(starTemperatureStr.begin()),
                                                                                make_move_iterator(starTemperatureStr.end())));
    deque<wstring> vertSyncHint;
    vertSyncHint.emplace_back(L"Vert Sync(F1): ");
    vertSyncHint.emplace_back((isVertSyncEnabled) ? L"On" : L"Off");

    deque<wstring> textHints;
    textHints.emplace_back(L"Text hints(TAB)");

    _textRenderer->ReverseRender(*_mainTextShader, _tmpStringCache, 0.99 * _displayWidth, 0.95 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, _fpsHintCache, 0.01 * _displayWidth, 0.95 * _displayHeight, 0.35, CurrentFpsColor());
    _textRenderer->Render(*_mainTextShader, _gpuHintCache, 0.01 * _displayWidth, 0.925 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, _soundVolumeHintCache, 0.01 * _displayWidth, 0.9 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, timeRunHint, 0.01 * _displayWidth, 0.875 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, planetStarHint, 0.01 * _displayWidth, 0.85 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, satelliteHint, 0.01 * _displayWidth, 0.825 * _displayHeight, 0.35, textColor);
#ifdef __EMSCRIPTEN__
    _textRenderer->Render(*_mainTextShader, textureHint, 0.01 * _displayWidth, 0.80 * _displayHeight, 0.30, glm::vec3(0.6f, 0.8f, 1.0f));
#endif
    _textRenderer->Render(*_mainTextShader, cameraSpeedHint, 0.01 * _displayWidth, 0.8 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, smoothCameraHint, 0.01 * _displayWidth, 0.775 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, smoothZoomHint, 0.01 * _displayWidth, 0.75 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, movementHint, 0.01 * _displayWidth, 0.725 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, speedBostHint, 0.01 * _displayWidth, 0.7 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starExposureHint, 0.01 * _displayWidth, 0.675 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starGammaHint, 0.01 * _displayWidth, 0.65 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starTemperatureHint, 0.01 * _displayWidth, 0.625 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, vertSyncHint, 0.01 * _displayWidth, 0.6 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, textHints, 0.01 * _displayWidth, 0.575 * _displayHeight, 0.35, textColor);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Application::RenderTextureLoadingProgress() const {
    auto& queue = TextureLoadingQueue::GetInstance();
    const int queued    = queue.GetQueuedCount();
    const int completed = queue.GetTotalCompleted();
    const int total     = queue.GetTotalQueued();

#ifdef __EMSCRIPTEN__
    // Expose streaming stats to JavaScript so the frontend can show a proper
    // "Streaming high-res... (2/5)" progress element.
    EM_ASM({
        if (typeof window.updateStreamingProgress === 'function') {
            window.updateStreamingProgress($0, $1);
        }
        // Also wire simple progress into the existing loading hook where applicable
        // (high-res streaming phase reuses the same (loaded,total) shape).
        if (typeof window.updateLoadingProgress === 'function' && $1 > 0) {
            window.updateLoadingProgress($0, $1);
        }
    }, completed, total);
#endif

    if (queued == 0) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    static const glm::mat4 textProjection = glm::ortho(0.0f, static_cast<float>(_displayWidth), 0.0f, static_cast<float>(_displayHeight));
    static constexpr glm::vec3 textColor = glm::vec3(0.3f, 0.8f, 1.0f);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", textProjection);
    _mainTextShader->SetBool("is3D", false);

    // Build hint string with progress counters, e.g. "High-res upgrade (2/5)"
    // Throttled render (via static) to avoid per-frame spam; visual is brief.
    static int lastCompleted = -1;
    static int lastTotal = -1;
    static int frameCounter = 0;
    frameCounter = (frameCounter + 1) % 10;
    if (frameCounter == 0 || completed != lastCompleted || total != lastTotal) {
        lastCompleted = completed;
        lastTotal = total;
        std::wstring hint = L"High-res upgrade (" + std::to_wstring(completed) + L"/" + std::to_wstring(total) + L")";
        deque<wstring> loadingHint;
        loadingHint.emplace_back(hint);
        _textRenderer->Render(*_mainTextShader, loadingHint, 0.5f * _displayWidth - 150, 0.1f * _displayHeight, 0.25, textColor);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Application::ConfigureMainShaders() {
    static const double zCoef = 2.0 / glm::log2(camera.GetFar() + 1.0);

    static const glm::mat4 skyBoxProjection = glm::perspective(glm::radians(45.0f), camera.GetAspect(), camera.GetNear(), camera.GetFar());

    _cameraProjection = camera.GetProjectionMatrix();
    _cameraView = camera.GetViewMatrix();

    _mainSkyBoxShader->Use();
    _mainSkyBoxShader->SetMat4("view", glm::mat4(glm::mat3(_cameraView)));
    _mainSkyBoxShader->SetMat4("projection", skyBoxProjection);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _cameraProjection);
    _mainTextShader->SetMat4("view", _cameraView);
    _mainTextShader->SetInt("text", 0);

    _mainStarShader->Use();
    _mainStarShader->SetMat4("projection", _cameraProjection);
    _mainStarShader->SetMat4("view", _cameraView);
    _mainStarShader->SetVec3("centerDir", glm::normalize(camera.GetPosition() - _sun->GetPosition()));
    _mainStarShader->SetVec3("shiftStarColor", _sun->GetShiftColor());
    _mainStarShader->SetVec3("colorMult", glm::vec3(0.96862745, 0.58039215, 0.235294117) * _sun->GetShiftColor());
    _mainStarShader->SetFloat("sunTemperatureInKelvin", _sun->GetStarTemperatureInKelvin());
    _mainStarShader->SetFloat("starRadiusInKilometers", _sun->GetStarRadius());
    _mainStarShader->SetFloat("zCoef", zCoef);
    _mainStarShader->SetFloat("uColorMap", _sun->GetTemperatureColorUCoordinate());
    _mainStarShader->SetBool("isVisible", _sun->GetVisibility() == 1.0);
    _mainStarShader->SetInt("colorMap", 0);
    glBindTextureUnit(0, _sun->GetStarSpectrumTexture());

    _mainCoronaStarShader->Use();
    _mainCoronaStarShader->SetMat4("projection", _cameraProjection);
    _mainCoronaStarShader->SetMat4("view", _cameraView);
    _mainCoronaStarShader->SetVec3("center", _sun->GetPosition());
    _mainCoronaStarShader->SetVec3("cameraRight", camera.GetRightVector());
    _mainCoronaStarShader->SetVec3("cameraUp", camera.GetUpVector());
    _mainCoronaStarShader->SetVec3("starShiftColor", _sun->GetShiftColor());
    _mainCoronaStarShader->SetFloat("zCoef", zCoef);
    _mainCoronaStarShader->SetFloat("maxSize", 7.1);
    _mainCoronaStarShader->SetFloat("starRadius", _sun->GetStarRadius());
    _mainCoronaStarShader->SetFloat("deltaTime", glfwGetTime() * 0.002);

    _mainPlanetShader->Use();
    _mainPlanetShader->SetMat4("projection", _cameraProjection);
    _mainPlanetShader->SetMat4("view", _cameraView);
    _mainPlanetShader->SetVec3("viewPos", camera.GetPosition());
    _mainPlanetShader->SetVec3("lightPos", _sun->GetPosition());
    _mainPlanetShader->SetVec3("starGlowTint", _sun->GetGlowTintMult());
    _mainPlanetShader->SetFloat("farPlane", camera.GetFar());
    _mainPlanetShader->SetFloat("zCoef", zCoef);
    _mainPlanetShader->SetFloat("bias", 0.0005);
    _mainPlanetShader->SetInt("shadowMap", 6);
    glBindTextureUnit(6, _shadowMapFBO->GetShadowMap());

    _mainAtmosphereShader->Use();
    _mainAtmosphereShader->SetMat4("projection", _cameraProjection);
    _mainAtmosphereShader->SetMat4("view", _cameraView);
    _mainAtmosphereShader->SetFloat("farPlane", camera.GetFar());
    _mainAtmosphereShader->SetFloat("zCoef", zCoef);
    _mainAtmosphereShader->SetFloat("bias", 0.001);
    _mainAtmosphereShader->SetInt("shadowMap", 11);
    glBindTextureUnit(11, _shadowMapFBO->GetShadowMap());

    _mainCloudsShader->Use();
    _mainCloudsShader->SetMat4("projection", _cameraProjection);
    _mainCloudsShader->SetMat4("view", _cameraView);
    _mainCloudsShader->SetVec3("viewPos", camera.GetPosition());
    _mainCloudsShader->SetVec3("lightPos", _sun->GetPosition());
    _mainCloudsShader->SetFloat("farPlane", camera.GetFar());
    _mainCloudsShader->SetFloat("zCoef", zCoef);
    _mainCloudsShader->SetFloat("bias", 0.001);
    _mainCloudsShader->SetInt("shadowMap", 8);
    glBindTextureUnit(8, _shadowMapFBO->GetShadowMap());

    _mainRingShader->Use();
    _mainRingShader->SetMat4("projection", _cameraProjection);
    _mainRingShader->SetMat4("view", _cameraView);
    _mainRingShader->SetVec3("lightPos", _sun->GetPosition());
    _mainRingShader->SetVec3("camPos", camera.GetPosition());
    _mainRingShader->SetVec3("starGlowTint", _sun->GetGlowTintMult());
    _mainRingShader->SetFloat("zCoef", zCoef);
    _mainRingShader->SetFloat("bias", 0.001);
    _mainRingShader->SetInt("shadowMap", 5);
    glBindTextureUnit(5, _shadowMapFBO->GetShadowMap());
}

void Application::InitScene() {
#ifdef __EMSCRIPTEN__
    LoadCoreResources();
#else
    InitSceneObjects();
    _appState = AppState::RUNNING;
#endif
}

void Application::UpdateLoadingProgress() {
#ifdef __EMSCRIPTEN__
    int loaded = _totalResources - _resourcesPending;
    // Call JavaScript function to update progress bar
    EM_ASM({
        if (typeof window.updateLoadingProgress === 'function') {
            window.updateLoadingProgress($0, $1);
        }
    }, loaded, _totalResources);
#endif
}

void Application::LoadCoreResources() {
    struct CoreResource {
        std::string url;
        std::string virtualPath;
    };

    std::vector<CoreResource> coreResources = {
        // Models
        {"resource/models/sphere.obj", "resource/models/sphere.obj"},
        {"resource/models/phobos.obj", "resource/models/phobos.obj"},
        {"resource/models/deimos.obj", "resource/models/deimos.obj"},
        {"resource/models/saturn_ring.obj", "resource/models/saturn_ring.obj"},
        {"resource/models/uranus_ring.obj", "resource/models/uranus_ring.obj"},
        // Sun
        {"resource/textures_low/Star_Spectrum_Low.dds", "resource/textures_low/Star_Spectrum_Low.dds"},
        {"resource/textures_low/flares_bright_Low.dds", "resource/textures_low/flares_bright_Low.dds"},
        // Sounds
        {"resource/sounds/Stellardrone - Galaxies.mp3", "resource/sounds/Stellardrone - Galaxies.mp3"},
        {"resource/sounds/Stellardrone - Mars.mp3", "resource/sounds/Stellardrone - Mars.mp3"},
        {"resource/sounds/Stellardrone - Billions And Billions.mp3", "resource/sounds/Stellardrone - Billions And Billions.mp3"},
        {"resource/sounds/Stellardrone - Gravitation (Remix).mp3", "resource/sounds/Stellardrone - Gravitation (Remix).mp3"},
        {"resource/sounds/Stellardrone - The Edge of Forever.mp3", "resource/sounds/Stellardrone - The Edge of Forever.mp3"}
    };

    const auto skyBoxFaces = GetSkyBoxFaces();
    for (size_t i = 0; i < skyBoxFaces.size(); ++i) {
        // The CDN only hosts the full skybox tier. Cache it over the selected
        // low-res MEMFS face so a failed/offline fetch leaves the bundled
        // placeholder available to InitSceneObjects().
        coreResources.push_back({kHighSkyBoxFaces[i], skyBoxFaces[i]});
    }

    _totalResources = static_cast<int>(coreResources.size());
    _resourcesPending = _totalResources;

    std::cout << "Loading " << _totalResources << " core resources..." << std::endl;
    
    // Initialize progress bar
    UpdateLoadingProgress();

    for(const auto& res : coreResources) {
        WebResourceFetcher::DownloadFile(res.url, res.virtualPath, [this](bool success) {
            _resourcesPending--;
            if (!success) {
                std::cerr << "Failed to download core resource!" << std::endl;
            }
            // Update progress after each resource
            UpdateLoadingProgress();
        });
    }
}

void Application::InitSceneObjects() {
    camera.SetAspect(static_cast<float>(_displayWidth) / static_cast<float>(_displayHeight));
    _shadowMapFBO = make_unique<ShadowMapFBO>(3000, 3000); 
    _hdrShader = make_unique<Shader>("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    _hdr = make_unique<HDR>(*_hdrShader, _displayWidth, _displayHeight);

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

    InitSongList();
    InitStarSystem();

    glfwShowWindow(_mainWindow);
#ifndef __EMSCRIPTEN__
    glfwSetWindowMonitor(_mainWindow, glfwGetPrimaryMonitor(), 0, 0, _displayWidth, _displayHeight, GLFW_DONT_CARE);
#endif

    StartSearchNearestPlanet();
    StartPlayBackgroundMusic();
}

void Application::InitSongList() {
    _backgroundSongs = vector<string_view> {
            "resource/sounds/Stellardrone - Galaxies.mp3",
            "resource/sounds/Stellardrone - Mars.mp3",
            "resource/sounds/Stellardrone - Billions And Billions.mp3",
            "resource/sounds/Stellardrone - Gravitation (Remix).mp3",
            "resource/sounds/Stellardrone - The Edge of Forever.mp3"
    };
    
    default_random_engine randEngine(static_cast<uint32_t>(chrono::high_resolution_clock::now().time_since_epoch().count()));
    shuffle(_backgroundSongs.begin(), _backgroundSongs.end(), randEngine);
}

void Application::InitStarSystem() {
    _sphereModel = std::make_unique<MeshHolder>("resource/models/sphere.obj");

    _starGlowShader = make_unique<Shader>("resource/shaders/starGlow.vs", "resource/shaders/starGlow.fs");
    StarInfo sunInfo(*_sphereModel, *_mainStarShader, *_starGlowShader, TextureImage2D("resource/textures_low/Star_Spectrum_Low.dds"),
                     starTemperatureInKelvin, 696342.0, glm::vec3(0.99607843, 0.890196078, 0.725490196), L"Sun", L"Солнце");
    _sun = make_shared<Sun>(sunInfo);

#ifdef __EMSCRIPTEN__
    // All planet systems are deferred on web — assets are downloaded on demand
    const glm::vec3 sunPos = _sun->GetPosition();
    _planetSystemManifests = {
        {
            "Mercury", sunPos + glm::vec3(1500.f, 0.f, 350.f), 800.f,
            // Required: low-res planet textures (must be ready before init)
            {
                "resource/textures_low/Mercury_Diffuse_Low.dds",
                "resource/textures_low/Mercury_Normal_Low.dds",
                "resource/textures_low/Mercury_Specular_Low.dds"
            },
            // Optional: no moons or rings for Mercury
            {},
            [this]{ InitMercury(*_sphereModel); }
        },
        {
            "Venus", sunPos + glm::vec3(1125.f, 0.f, -1340.f), 800.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Venus_Diffuse_Low.dds",
                "resource/textures_low/Venus_Normal_Low.dds"
            },
            // Optional: no moons or rings for Venus
            {},
            [this]{ InitVenus(*_sphereModel); }
        },
        {
            "Earth", sunPos + glm::vec3(1900.f, 0.f, 0.f), 800.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Earth_Day_Diffuse_Low.dds",
                "resource/textures_low/Earth_Normal_Low.dds",
                "resource/textures_low/Earth_Specular_Low.dds"
            },
            // Optional: cloud layers and moon textures (fallback if unavailable)
            {
                "resource/textures_low/Earth_Clouds_Diffuse_Low.dds",
                "resource/textures_low/Earth_Night_Diffuse_Low.dds",
                "resource/textures_low/Earth_Clouds_Normal_Low.dds",
                "resource/textures_low/Moon_Diffuse_Low.dds",
                "resource/textures_low/Moon_Normal_Low.dds"
            },
            [this]{ InitEarthSystem(*_sphereModel); }
        },
        {
            "Mars", sunPos + glm::vec3(-1732.f, 0.f, 1000.f), 800.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Mars_Diffuse_Low.dds",
                "resource/textures_low/Mars_Normal_Low.dds"
            },
            // Optional: moon textures (fallback if unavailable)
            {
                "resource/textures_low/Phobos_Diffuse_Low.dds",
                "resource/textures_low/Phobos_Normal_Low.dds",
                "resource/textures_low/Deimos_Diffuse_Low.dds",
                "resource/textures_low/Deimos_Normal_Low.dds"
            },
            [this]{ InitMarsSystem(*_sphereModel); }
        },
        {
            "Jupiter", sunPos + glm::vec3(1350.f, 0.f, 1737.f), 1500.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Jupiter_Diffuse_Low.dds",
                "resource/textures_low/Jupiter_Normal_Low.dds"
            },
            // Optional: Galilean moon textures (fallback if unavailable)
            {
                "resource/textures_low/Io_Diffuse_Low.dds",
                "resource/textures_low/Io_Normal_Low.dds",
                "resource/textures_low/Europa_Diffuse_Low.dds",
                "resource/textures_low/Europa_Normal_Low.dds",
                "resource/textures_low/Ganymede_Diffuse_Low.dds",
                "resource/textures_low/Ganymede_Normal_Low.dds",
                "resource/textures_low/Callisto_Diffuse_Low.dds",
                "resource/textures_low/Callisto_Normal_Low.dds"
            },
            [this]{ InitJupiterSystem(*_sphereModel); }
        },
        {
            "Saturn", sunPos + glm::vec3(0.f, -100.f, 2450.f), 1500.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Saturn_Diffuse_Low.dds",
                "resource/textures_low/Saturn_Normal_Low.dds"
            },
            // Optional: ring and moon textures (fallback if unavailable)
            {
                "resource/textures_low/Saturn_Rings_Low.dds",
                "resource/textures_low/Mimas_Diffuse_Low.dds",
                "resource/textures_low/Mimas_Normal_Low.dds",
                "resource/textures_low/Enceladus_Diffuse_Low.dds",
                "resource/textures_low/Enceladus_Normal_Low.dds",
                "resource/textures_low/Tethys_Diffuse_Low.dds",
                "resource/textures_low/Tethys_Normal_Low.dds",
                "resource/textures_low/Dione_Diffuse_Low.dds",
                "resource/textures_low/Dione_Normal_Low.dds",
                "resource/textures_low/Rhea_Diffuse_Low.dds",
                "resource/textures_low/Rhea_Normal_Low.dds",
                "resource/textures_low/Titan_Diffuse_Low.dds",
                "resource/textures_low/Titan_Normal_Low.dds",
                "resource/textures_low/Iapetus_Diffuse_Low.dds",
                "resource/textures_low/Iapetus_Normal_Low.dds"
            },
            [this]{ InitSaturnSystem(*_sphereModel); }
        },
        {
            "Uranus", sunPos + glm::vec3(0.f, 0.f, -2650.f), 1500.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Uranus_Diffuse_Low.dds",
                "resource/textures_low/Uranus_Normal_Low.dds"
            },
            // Optional: cloud layers, ring, and moon textures (fallback if unavailable)
            {
                "resource/textures_low/Uranus_Clouds_Diffuse_Low.dds",
                "resource/textures_low/Uranus_Rings_Low.dds",
                "resource/textures_low/Uranus_Clouds_Normal_Low.dds",
                "resource/textures_low/Miranda_Diffuse_Low.dds",
                "resource/textures_low/Miranda_Normal_Low.dds",
                "resource/textures_low/Ariel_Diffuse_Low.dds",
                "resource/textures_low/Ariel_Normal_Low.dds",
                "resource/textures_low/Umbriel_Diffuse_Low.dds",
                "resource/textures_low/Umbriel_Normal_Low.dds",
                "resource/textures_low/Titania_Diffuse_Low.dds",
                "resource/textures_low/Titania_Normal_Low.dds",
                "resource/textures_low/Oberon_Diffuse_Low.dds",
                "resource/textures_low/Oberon_Normal_Low.dds"
            },
            [this]{ InitUranusSystem(*_sphereModel); }
        },
        {
            "Neptune", sunPos + glm::vec3(-2900.f, 0.f, 0.f), 1500.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Neptune_Diffuse_Low.dds",
                "resource/textures_low/Neptune_Normal_Low.dds"
            },
            // Optional: cloud layer and moon textures (fallback if unavailable)
            {
                "resource/textures_low/Neptune_Clouds_Diffuse_Low.dds",
                "resource/textures_low/Neptune_Clouds_Normal_Low.dds",
                "resource/textures_low/Triton_Diffuse_Low.dds",
                "resource/textures_low/Triton_Normal_Low.dds"
            },
            [this]{ InitNeptuneSystem(*_sphereModel); }
        },
        {
            "Pluto", sunPos + glm::vec3(2800.f, 0.f, 1757.73f), 1500.f,
            // Required: low-res planet textures
            {
                "resource/textures_low/Pluto_Diffuse_Low.dds",
                "resource/textures_low/Pluto_Normal_Low.dds",
                "resource/textures_low/Pluto_Specular_Low.dds"
            },
            // Optional: moon textures (fallback if unavailable)
            {
                "resource/textures_low/Charon_Diffuse_Low.dds",
                "resource/textures_low/Charon_Normal_Low.dds",
                "resource/textures_low/Charon_Specular_Low.dds"
            },
            [this]{ InitPlutoSystem(*_sphereModel); }
        },
    };
    std::cout << "[StagedLoading] " << _planetSystemManifests.size()
              << " planet systems deferred until camera approaches." << std::endl;
#else
    // Desktop: load all immediately (no memory constraint)
    InitMercury(*_sphereModel);
    InitVenus(*_sphereModel);
    InitEarthSystem(*_sphereModel);
    InitMarsSystem(*_sphereModel);
    InitJupiterSystem(*_sphereModel);
    InitSaturnSystem(*_sphereModel);
    InitUranusSystem(*_sphereModel);
    InitNeptuneSystem(*_sphereModel);
    InitPlutoSystem(*_sphereModel);
#endif
}

void Application::InitMercury(const MeshHolder& sphereModel) {
    PlanetInfo mercuryInfo(sphereModel, 0.38, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Mercury_Diffuse_Low.dds", "resource/textures_low/Mercury_Diffuse_Low.dds")),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Mercury_Normal_Low.dds", "resource/textures_low/Mercury_Normal_Low.dds")), L"Mercury", L"Меркурий", TextureImage2D(GetTexturePath("resource/textures_low/Mercury_Specular_Low.dds", "resource/textures_low/Mercury_Specular_Low.dds")));
    shared_ptr<Planet> mercury = make_shared<Mercury>(mercuryInfo, _sun);

    const glm::mat4 lightProjection = glm::ortho(-mercury->GetRadius() * 3.0f, mercury->GetRadius() * 3.0f, -mercury->GetRadius() * 3.0f, mercury->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), mercury->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableSceneComponent mercurySystemComponent;
    mercurySystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    mercurySystemComponent.planet = move(mercury);
    _renderableSceneComponents.push_back(move(mercurySystemComponent));
}

void Application::InitVenus(const MeshHolder& sphereModel) {
    PlanetInfo venusInfo(sphereModel, 0.95, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Venus_Diffuse_Low.dds", "resource/textures_low/Venus_Diffuse_Low.dds")),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Venus_Normal_Low.dds", "resource/textures_low/Venus_Normal_Low.dds")), L"Venus", L"Венера");
    shared_ptr<Planet> venus = make_shared<Venus>(venusInfo, _sun);

    AtmosphereInfo venusAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 1.1, glm::vec3(203/255.f, 158/255.f, 69/255.), venus->GetRadius() - 0.00007, 1.995);
    unique_ptr<Atmosphere> venusAtmosphere = make_unique<Atmosphere>(venusAtmosphereInfo, venus);

    const glm::mat4 lightProjection = glm::ortho(-venus->GetRadius() * 3.0f, venus->GetRadius() * 3.0f, -venus->GetRadius() * 3.0f, venus->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), venus->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableVenusAtmosphere;
    renderableVenusAtmosphere.atmosphere = move(venusAtmosphere);
    renderableVenusAtmosphere.hScaleFactor = 6.0;
    renderableVenusAtmosphere.parentEarthSizeCoefficient = venus->GetEarthSizeCoefficient();

    RenderableSceneComponent venusSystemComponent;
    venusSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    venusSystemComponent.planet = move(venus);
    venusSystemComponent.atmospheres.push_back(move(renderableVenusAtmosphere));
    _renderableSceneComponents.push_back(move(venusSystemComponent));
}

void Application::InitEarthSystem(const MeshHolder& sphereModel) {
    // Load textures with low-res fallback for Web
    PlanetInfo earthInfo(sphereModel, 1.0, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Earth_Day_Diffuse_Low.dds", "resource/textures_low/Earth_Day_Diffuse_Low.dds")),
                TextureImage2D("resource/textures_low/Earth_Clouds_Diffuse_Low.dds"),
                TextureImage2D("resource/textures_low/Earth_Night_Diffuse_Low.dds"),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Earth_Normal_Low.dds", "resource/textures_low/Earth_Normal_Low.dds")), L"Earth", L"Земля", TextureImage2D(GetTexturePath("resource/textures_low/Earth_Specular_Low.dds", "resource/textures_low/Earth_Specular_Low.dds")));
    shared_ptr<Planet> earth = make_shared<Earth>(earthInfo, _sun);

    SatelliteInfo moonInfo(sphereModel, 0.2724, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Moon_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Moon_Normal_Low.dds"),
                           L"Moon", L"Луна");
    shared_ptr<Satellite> moon = make_shared<Moon>(moonInfo, earth);

    AtmosphereInfo earthAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 1.1, glm::vec3(0.3, 0.7, 1.0), earth->GetRadius() - 0.00007, 2.1);
    unique_ptr<Atmosphere> earthAtmosphere = make_unique<Atmosphere>(earthAtmosphereInfo, earth);

    CloudsInfo earthCloudsInfo(sphereModel, *_mainCloudsShader, 1.0055, TextureImage2D("resource/textures_low/Earth_Clouds_Diffuse_Low.dds"),
                               TextureImage2D("resource/textures_low/Earth_Clouds_Normal_Low.dds"));
    unique_ptr<Clouds> earthClouds = make_unique<EarthClouds>(earthCloudsInfo, earth);

    const glm::mat4 lightProjection = glm::ortho(-earth->GetRadius() * 3.0f, earth->GetRadius() * 3.0f, -earth->GetRadius() * 3.0f, earth->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), earth->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableEarthAtmosphere;
    renderableEarthAtmosphere.atmosphere = move(earthAtmosphere);
    renderableEarthAtmosphere.hScaleFactor = 6.0;
    renderableEarthAtmosphere.parentEarthSizeCoefficient = earth->GetEarthSizeCoefficient();

    RenderableSceneComponent earthSystemComponent;
    earthSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    earthSystemComponent.planet = move(earth);
    earthSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(moon)};
    earthSystemComponent.atmospheres.push_back(move(renderableEarthAtmosphere));
    earthSystemComponent.clouds = move(earthClouds);
    _renderableSceneComponents.push_back(move(earthSystemComponent));
}

void Application::InitMarsSystem(const MeshHolder& sphereModel) {
    MeshHolder phobosModel("resource/models/phobos.obj"), deimosModel("resource/models/deimos.obj");

    PlanetInfo marsInfo(sphereModel, 0.53, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Mars_Diffuse_Low.dds", "resource/textures_low/Mars_Diffuse_Low.dds")),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Mars_Normal_Low.dds", "resource/textures_low/Mars_Normal_Low.dds")), L"Mars", L"Марс");
    shared_ptr<Planet> mars = make_shared<Mars>(marsInfo, _sun);

    SatelliteInfo phobosInfo(phobosModel, 0.001768, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Phobos_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Phobos_Normal_Low.dds"),
                             L"Phobos", L"Фобос");
    SatelliteInfo deimosInfo(deimosModel, 0.00097316, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Deimos_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Deimos_Normal_Low.dds"),
                             L"Deimos", L"Деймос");
    shared_ptr<Satellite> phobos = make_shared<Phobos>(phobosInfo, mars);
    shared_ptr<Satellite> deimos = make_shared<Deimos>(deimosInfo, mars);

    AtmosphereInfo marsAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 0.583, glm::vec3(0.976, 0.302, 0.208), mars->GetRadius() - 0.00007, 1.113);
    unique_ptr<Atmosphere> marsAtmosphere = make_unique<Atmosphere>(marsAtmosphereInfo, mars);

    const glm::mat4 lightProjection = glm::ortho(-mars->GetRadius() * 3.0f, mars->GetRadius() * 3.0f, -mars->GetRadius() * 3.0f, mars->GetRadius() * 3.0f, camera.GetNear(),
                                                 glm::length(_sun->GetPosition() - mars->GetPosition()) + 50.f);
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), mars->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableMarsAtmosphere;
    renderableMarsAtmosphere.atmosphere = move(marsAtmosphere);
    renderableMarsAtmosphere.hScaleFactor = 6.0;
    renderableMarsAtmosphere.parentEarthSizeCoefficient = mars->GetEarthSizeCoefficient();

    RenderableSceneComponent marsSystemComponent;
    marsSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    marsSystemComponent.planet = move(mars);
    marsSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(phobos), move(deimos)};
    marsSystemComponent.atmospheres.push_back(move(renderableMarsAtmosphere));
    _renderableSceneComponents.push_back(move(marsSystemComponent));
}

void Application::InitJupiterSystem(const MeshHolder& sphereModel) {
    PlanetInfo jupiterInfo(sphereModel, 11.2, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Jupiter_Diffuse_Low.dds", "resource/textures_low/Jupiter_Diffuse_Low.dds")),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Jupiter_Normal_Low.dds", "resource/textures_low/Jupiter_Normal_Low.dds")), L"Jupiter", L"Юпитер");
    shared_ptr<Planet> jupiter = make_shared<Jupiter>(jupiterInfo, _sun);

    SatelliteInfo ioInfo(sphereModel, 0.28592, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Io_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Io_Normal_Low.dds"),
                         L"Io", L"Ио");
    SatelliteInfo europaInfo(sphereModel, 0.244985, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Europa_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Europa_Normal_Low.dds"),
                             L"Europa", L"Европа");
    SatelliteInfo ganymedeInfo(sphereModel, 0.41345, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Ganymede_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Ganymede_Normal_Low.dds"),
                               L"Ganymede", L"Ганимед");
    SatelliteInfo callistoInfo(sphereModel, 0.3783236, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Callisto_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Callisto_Normal_Low.dds"),
                               L"Callisto", L"Каллисто");
    shared_ptr<Satellite> io = make_shared<Io>(ioInfo, jupiter);
    shared_ptr<Satellite> europa = make_shared<Europa>(europaInfo, jupiter);
    shared_ptr<Satellite> ganymede = make_shared<Ganymede>(ganymedeInfo, jupiter);
    shared_ptr<Satellite> callisto = make_shared<Callisto>(callistoInfo, jupiter);

    AtmosphereInfo jupiterAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 11.4, glm::vec3(153.f/255, 139.f/255, 120.f/255), jupiter->GetRadius() - 0.00007, 23.35);
    unique_ptr<Atmosphere> jupiterAtmosphere = make_unique<Atmosphere>(jupiterAtmosphereInfo, jupiter);

    const glm::mat4 lightProjection = glm::ortho(-jupiter->GetRadius() * 3.0f, jupiter->GetRadius() * 3.0f, -jupiter->GetRadius() * 3.0f, jupiter->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), jupiter->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableJupiterAtmosphere;
    renderableJupiterAtmosphere.atmosphere = move(jupiterAtmosphere);
    renderableJupiterAtmosphere.hScaleFactor = 26.0;
    renderableJupiterAtmosphere.parentEarthSizeCoefficient = jupiter->GetEarthSizeCoefficient();
    renderableJupiterAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent jupiterSystemComponent;
    jupiterSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    jupiterSystemComponent.planet = move(jupiter);
    jupiterSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(io), move(europa), move(ganymede), move(callisto)};
    jupiterSystemComponent.atmospheres.push_back(move(renderableJupiterAtmosphere));
    _renderableSceneComponents.push_back(move(jupiterSystemComponent));
}

void Application::InitSaturnSystem(const MeshHolder& sphereModel) {
    MeshHolder saturnRingModel("resource/models/saturn_ring.obj");

    PlanetInfo saturnInfo(sphereModel, 9.14, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Saturn_Diffuse_Low.dds", "resource/textures_low/Saturn_Diffuse_Low.dds")),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Saturn_Normal_Low.dds", "resource/textures_low/Saturn_Normal_Low.dds")), L"Saturn", L"Сатурн");
    shared_ptr<Planet> saturn = make_shared<Saturn>(saturnInfo, _sun);

    PlanetaryRingInfo saturnRingInfo(saturnRingModel, 22.0, 43.7, *_mainPlanetShader, TextureImage2D("resource/textures_low/Saturn_Rings_Low.dds")); 
    unique_ptr<PlanetaryRing> saturnRing = make_unique<SaturnRing>(saturnRingInfo, saturn);

    SatelliteInfo mimasInfo(sphereModel, 0.03111, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Mimas_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Mimas_Normal_Low.dds"),
                            L"Mimas", L"Мимас");
    SatelliteInfo enceladusInfo(sphereModel, 0.03957, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Enceladus_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Enceladus_Normal_Low.dds"),
                            L"Enceladus", L"Энцелад");
    SatelliteInfo tethysInfo(sphereModel, 0.083346, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Tethys_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Tethys_Normal_Low.dds"),
                            L"Tethys", L"Тефия");
    SatelliteInfo dioneInfo(sphereModel, 0.08812, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Dione_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Dione_Normal_Low.dds"),
                            L"Dione", L"Диона");
    SatelliteInfo rheaInfo(sphereModel, 0.119886, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Rhea_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Rhea_Normal_Low.dds"),
                            L"Rhea", L"Рея");
    SatelliteInfo titanInfo(sphereModel, 0.404136, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Titan_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Titan_Normal_Low.dds"),
                            L"Titan", L"Титан");
    SatelliteInfo iapetusInfo(sphereModel, 0.115288, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Iapetus_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Iapetus_Normal_Low.dds"),
                            L"Iapetus", L"Япет");
    shared_ptr<Satellite> mimas = make_shared<Mimas>(mimasInfo, saturn);
    shared_ptr<Satellite> enceladus = make_shared<Enceladus>(enceladusInfo, saturn);
    shared_ptr<Satellite> tethys = make_shared<Tethys>(tethysInfo, saturn);
    shared_ptr<Satellite> dione = make_shared<Dione>(dioneInfo, saturn);
    shared_ptr<Satellite> rhea = make_shared<Rhea>(rheaInfo, saturn);
    shared_ptr<Satellite> titan = make_shared<Titan>(titanInfo, saturn);
    shared_ptr<Satellite> iapetus = make_shared<Iapetus>(iapetusInfo, saturn);

    AtmosphereInfo saturnAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 9.34, glm::vec3(84.f/255, 132.f/255, 176.f/255), saturn->GetRadius() - 0.00007, 18.6);
    unique_ptr<Atmosphere> saturnAtmosphere = make_unique<Atmosphere>(saturnAtmosphereInfo, saturn);

    AtmosphereInfo titanAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 0.504136, glm::vec3(40.f/255, 33.f/255, 72.f/255), titan->GetRadius() - 0.00007, 0.8429210,
                                       glm::vec3(0.36862745, 0.0666667, 0.0196078)); 
    unique_ptr<Atmosphere> titanAtmosphere = make_unique<Atmosphere>(titanAtmosphereInfo, titan);

    const glm::mat4 lightProjection = glm::ortho(-saturn->GetRadius() * 3.0f, saturn->GetRadius() * 3.0f, -saturn->GetRadius() * 3.0f, saturn->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), saturn->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableSaturnAtmosphere;
    renderableSaturnAtmosphere.atmosphere = move(saturnAtmosphere);
    renderableSaturnAtmosphere.hScaleFactor = 27.0;
    renderableSaturnAtmosphere.parentEarthSizeCoefficient = saturn->GetEarthSizeCoefficient();
    renderableSaturnAtmosphere.isUseToneMapping = true;

    RenderableAtmosphere renderableTitanAtmosphere;
    renderableTitanAtmosphere.atmosphere = move(titanAtmosphere);
    renderableTitanAtmosphere.hScaleFactor = 4.8;
    renderableTitanAtmosphere.parentEarthSizeCoefficient = titan->GetEarthSizeCoefficient();

    RenderableSceneComponent saturnSystemComponent;
    saturnSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    saturnSystemComponent.planet = move(saturn);
    saturnSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(mimas), move(enceladus), move(tethys), move(dione), move(rhea), move(titan), move(iapetus)};
    saturnSystemComponent.atmospheres.push_back(move(renderableSaturnAtmosphere));
    saturnSystemComponent.atmospheres.push_back(move(renderableTitanAtmosphere));
    saturnSystemComponent.planetaryRing = move(saturnRing);
    _renderableSceneComponents.push_back(move(saturnSystemComponent));
}

void Application::InitUranusSystem(const MeshHolder& sphereModel) {
    MeshHolder uranusRingModel("resource/models/uranus_ring.obj");

    PlanetInfo uranusInfo(sphereModel, 3.98085, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Uranus_Diffuse_Low.dds", "resource/textures_low/Uranus_Diffuse_Low.dds")),
                TextureImage2D("resource/textures_low/Uranus_Clouds_Diffuse_Low.dds")
            }, TextureImage2D(GetTexturePath("resource/textures_low/Uranus_Normal_Low.dds", "resource/textures_low/Uranus_Normal_Low.dds")), L"Uranus", L"Уран");
    shared_ptr<Planet> uranus = make_shared<Uranus>(uranusInfo, _sun);
    PlanetaryRingInfo uranusRingInfo(uranusRingModel, 12.6, 16.0, *_mainPlanetShader, TextureImage2D("resource/textures_low/Uranus_Rings_Low.dds")); // Radiuses from 3D model
    unique_ptr<PlanetaryRing> uranusRing = make_unique<UranusRing>(uranusRingInfo, uranus);

    SatelliteInfo mirandaInfo(sphereModel, 0.0368858, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Miranda_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Miranda_Normal_Low.dds"),
                            L"Miranda", L"Миранда");
    SatelliteInfo arielInfo(sphereModel, 0.090865, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Ariel_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Ariel_Normal_Low.dds"),
                            L"Ariel", L"Ариэль");
    SatelliteInfo umbrielInfo(sphereModel, 0.091775, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Umbriel_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Umbriel_Normal_Low.dds"),
                            L"Umbriel", L"Умбриэль");
    SatelliteInfo titaniaInfo(sphereModel, 0.123748, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Titania_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Titania_Normal_Low.dds"),
                            L"Titania", L"Титания");
    SatelliteInfo oberonInfo(sphereModel, 0.11951, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Oberon_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Oberon_Normal_Low.dds"),
                            L"Oberon", L"Оберон");
    shared_ptr<Satellite> miranda = make_shared<Miranda>(mirandaInfo, uranus);
    shared_ptr<Satellite> ariel = make_shared<Ariel>(arielInfo, uranus);
    shared_ptr<Satellite> umbriel = make_shared<Umbriel>(umbrielInfo, uranus);
    shared_ptr<Satellite> titania = make_shared<Titania>(titaniaInfo, uranus);
    shared_ptr<Satellite> oberon = make_shared<Oberon>(oberonInfo, uranus);

    CloudsInfo uranusCloudsInfo(sphereModel, *_mainCloudsShader, 3.98635, TextureImage2D("resource/textures_low/Uranus_Clouds_Diffuse_Low.dds"),
                            TextureImage2D("resource/textures_low/Uranus_Clouds_Normal_Low.dds"));
    unique_ptr<Clouds> uranusClouds = make_unique<UranusClouds>(uranusCloudsInfo, uranus);

    AtmosphereInfo uranusAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 4.0, glm::vec3(45.f/255, 101.f/255, 114.f/255), uranus->GetRadius() - 0.00007, 8.1);
    unique_ptr<Atmosphere> uranusAtmosphere = make_unique<Atmosphere>(uranusAtmosphereInfo, uranus);

    const glm::mat4 lightProjection = glm::ortho(-uranus->GetRadius() * 3.0f, uranus->GetRadius() * 3.0f, -uranus->GetRadius() * 3.0f, uranus->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), uranus->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableUranusAtmosphere;
    renderableUranusAtmosphere.atmosphere = move(uranusAtmosphere);
    renderableUranusAtmosphere.hScaleFactor = 24.0;
    renderableUranusAtmosphere.parentEarthSizeCoefficient = uranus->GetEarthSizeCoefficient();
    renderableUranusAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent uranusSystemComponent;
    uranusSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    uranusSystemComponent.planet = move(uranus);
    uranusSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(miranda), move(ariel), move(umbriel), move(titania), move(oberon)};
    uranusSystemComponent.atmospheres.push_back(move(renderableUranusAtmosphere));
    uranusSystemComponent.clouds = move(uranusClouds);
    uranusSystemComponent.planetaryRing = move(uranusRing);
    _renderableSceneComponents.push_back(move(uranusSystemComponent));
}

void Application::InitNeptuneSystem(const MeshHolder& sphereModel) {
    PlanetInfo neptuneInfo(sphereModel, 3.8647, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Neptune_Diffuse_Low.dds", "resource/textures_low/Neptune_Diffuse_Low.dds")),
                TextureImage2D("resource/textures_low/Neptune_Clouds_Diffuse_Low.dds")
            }, TextureImage2D(GetTexturePath("resource/textures_low/Neptune_Normal_Low.dds", "resource/textures_low/Neptune_Normal_Low.dds")), L"Neptune", L"Нептун");
    shared_ptr<Planet> neptune = make_shared<Neptune>(neptuneInfo, _sun);

    SatelliteInfo tritonInfo(sphereModel, 0.2724, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Triton_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Triton_Normal_Low.dds"),
                             L"Triton", L"Тритон");
    shared_ptr<Satellite> triton = make_shared<Triton>(tritonInfo, neptune);

    CloudsInfo neptuneCloudsInfo(sphereModel, *_mainCloudsShader, 3.87, TextureImage2D("resource/textures_low/Neptune_Clouds_Diffuse_Low.dds"),
                                TextureImage2D("resource/textures_low/Neptune_Clouds_Normal_Low.dds"));
    unique_ptr<Clouds> neptuneClouds = make_unique<NeptuneClouds>(neptuneCloudsInfo, neptune);

    AtmosphereInfo neptuneAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 3.9, glm::vec3(62.f/255, 92.f/255, 169.f/255), neptune->GetRadius() - 0.00007, 7.9);
    unique_ptr<Atmosphere> neptuneAtmosphere = make_unique<Atmosphere>(neptuneAtmosphereInfo, neptune);

    const glm::mat4 lightProjection = glm::ortho(-neptune->GetRadius() * 3.0f, neptune->GetRadius() * 3.0f, -neptune->GetRadius() * 3.0f, neptune->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), neptune->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderableNeptuneAtmosphere;
    renderableNeptuneAtmosphere.atmosphere = move(neptuneAtmosphere);
    renderableNeptuneAtmosphere.hScaleFactor = 23.0;
    renderableNeptuneAtmosphere.parentEarthSizeCoefficient = neptune->GetEarthSizeCoefficient();
    renderableNeptuneAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent neptuneSystemComponent;
    neptuneSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    neptuneSystemComponent.planet = move(neptune);
    neptuneSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(triton)};
    neptuneSystemComponent.atmospheres.push_back(move(renderableNeptuneAtmosphere));
    neptuneSystemComponent.clouds = move(neptuneClouds);
    _renderableSceneComponents.push_back(move(neptuneSystemComponent));
}

void Application::InitPlutoSystem(const MeshHolder& sphereModel) {
    PlanetInfo plutoInfo(sphereModel, 0.18651, *_mainPlanetShader,
            {
                TextureImage2D(GetTexturePath("resource/textures_low/Pluto_Diffuse_Low.dds", "resource/textures_low/Pluto_Diffuse_Low.dds")),
            }, TextureImage2D(GetTexturePath("resource/textures_low/Pluto_Normal_Low.dds", "resource/textures_low/Pluto_Normal_Low.dds")), L"Pluto", L"Плутон", TextureImage2D(GetTexturePath("resource/textures_low/Pluto_Specular_Low.dds", "resource/textures_low/Pluto_Specular_Low.dds")));
    shared_ptr<Planet> pluto = make_shared<Pluto>(plutoInfo, _sun);

    SatelliteInfo charonInfo(sphereModel, 0.09512, *_mainPlanetShader, {TextureImage2D("resource/textures_low/Charon_Diffuse_Low.dds")}, TextureImage2D("resource/textures_low/Charon_Normal_Low.dds"),
                             L"Charon", L"Харон", TextureImage2D("resource/textures_low/Charon_Specular_Low.dds"));
    shared_ptr<Satellite> charon  = make_shared<Charon>(charonInfo, pluto);

    AtmosphereInfo plutoAtmosphereInfo(sphereModel, *_mainAtmosphereShader, 0.45, glm::vec3(92.f/255, 120.f/255, 141.f/255), pluto->GetRadius(), 1.0,
                                       glm::vec3(35.f/255, 52.f/255, 220.f/255));
    unique_ptr<Atmosphere> plutoAtmosphere = make_unique<Atmosphere>(plutoAtmosphereInfo, pluto);

    const glm::mat4 lightProjection = glm::ortho(-pluto->GetRadius() * 3.0f, pluto->GetRadius() * 3.0f, -pluto->GetRadius() * 3.0f, pluto->GetRadius() * 3.0f, camera.GetNear(), camera.GetFar());
    const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), pluto->GetPosition() - _sun->GetPosition(), glm::vec3(0.0, 1.0, 0.0));
    const glm::mat4 lightSpaceMatrix = lightProjection * lightView;

    RenderableAtmosphere renderablePlutoAtmosphere;
    renderablePlutoAtmosphere.atmosphere = move(plutoAtmosphere);
    renderablePlutoAtmosphere.hScaleFactor = 16.0;
    renderablePlutoAtmosphere.parentEarthSizeCoefficient = pluto->GetEarthSizeCoefficient();
    renderablePlutoAtmosphere.isUseToneMapping = true;

    RenderableSceneComponent plutoSystemComponent;
    plutoSystemComponent.lightSpaceMatrix = lightSpaceMatrix;
    plutoSystemComponent.planet = move(pluto);
    plutoSystemComponent.satellites = vector<shared_ptr<Satellite>>{move(charon)};
    plutoSystemComponent.atmospheres.push_back(move(renderablePlutoAtmosphere));
    _renderableSceneComponents.push_back(move(plutoSystemComponent));
}

void Application::DisplaySystemInformation() const {
    cout << "GPU Supplier: " << glGetString(GL_VENDOR) << endl;
    cout << "GPU: " << glGetString(GL_RENDERER) << endl;

    GLint majorVersion, minorVersion;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
    cout << "OpenGL version: " << majorVersion << '.' << minorVersion << endl;

#ifndef __EMSCRIPTEN__
    GLint totalMemoryKb;
    glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemoryKb);

    GLint currentMemoryKb;
    glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &currentMemoryKb);
    cout << "Total GPU Memory: " << totalMemoryKb << " kb\nFree GPU Memory: " << currentMemoryKb << " kb" << endl;
#endif

    GLint maxTextureSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    cout << "Max texture size in the system: "<< maxTextureSize << "x" << maxTextureSize << endl;
    cout << "Driver: " << glGetString(GL_VERSION) << endl;
}

void Application::LoadWindowIcon() const {
    constexpr auto execIconPath = "resource/icons/solarsystem-logo.png";
    SDL_Surface* windowIcon = IMG_Load(execIconPath);
    if (windowIcon == nullptr)
        throw runtime_error(string("Cannot load exe icon ") + execIconPath);
    GLFWimage image;
    image.pixels = static_cast<unsigned char*>(windowIcon->pixels);
    image.width = windowIcon->w;
    image.height = windowIcon->h;
    glfwSetWindowIcon(_mainWindow, 1, &image);
    SDL_FreeSurface(windowIcon);
}

void Application::ProcessInput(GLFWwindow* window) {
    static float movementSpeed = camera.GetMovementSpeed();

    if (isFirstMouse) {
        lastX = 0;
        lastY = 0;
        isFirstMouse = false;
    }

    float xPos = lastX, yPos = lastY;
    float shiftIncrease = 1.0f, yScroll = 0;

    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        shiftIncrease = 4 * camera.GetMovementSpeed();
    }
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        camera.ProcessKeyboard(CameraVector::FORWARD, deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        camera.ProcessKeyboard(CameraVector::BACKWARD, deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        camera.ProcessKeyboard(CameraVector::LEFT, deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        camera.ProcessKeyboard(CameraVector::RIGHT, deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        camera.ProcessKeyboard(CameraVector::WORLD_UP, deltaTime * shiftIncrease);
    }
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) {
        camera.ProcessKeyboard(CameraVector::WORLD_DOWN, deltaTime * shiftIncrease);
    }
    
    if (glfwGetKey(window, GLFW_KEY_PAGE_UP) == GLFW_PRESS) {
#ifdef __EMSCRIPTEN__
        int currentVolume = Mix_VolumeMusic(-1);
        Mix_VolumeMusic(clamp(currentVolume + 1, 0, MIX_MAX_VOLUME));
#else
        _soundEngine->setSoundVolume(clamp(_soundEngine->getSoundVolume() + 0.01, 0.0, 1.0));
#endif
    }
    if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) {
#ifdef __EMSCRIPTEN__
        int currentVolume = Mix_VolumeMusic(-1);
        Mix_VolumeMusic(clamp(currentVolume - 1, 0, MIX_MAX_VOLUME));
#else
        _soundEngine->setSoundVolume(clamp(_soundEngine->getSoundVolume() - 0.01, 0.0, 1.0));
#endif
    }
    if (glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS) {
        movementSpeed = glm::clamp(movementSpeed + 0.01f, 0.0f, 150.f);
        camera.SetMovementSpeed(movementSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS) {
        movementSpeed = glm::clamp(movementSpeed - 0.01f, 0.0f, 150.f);
        camera.SetMovementSpeed(movementSpeed);
    }
    if (glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS) {
        starExposure = glm::clamp(starExposure + 0.1f, 0.0f, 20.f);
    }
    if (glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS) {
        starExposure = glm::clamp(starExposure - 0.1f, 0.0f, 20.f);
    }
    if (glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS) {
        starGamma = glm::clamp(starGamma + 0.01f, 0.0f, 2.f);
    }
    if (glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS) {
        starGamma = glm::clamp(starGamma - 0.01f, 0.0f, 2.f);
    }
    if (glfwGetKey(window, GLFW_KEY_7) == GLFW_PRESS) {
        starTemperatureInKelvin = glm::clamp(starTemperatureInKelvin + 15.0f, 800.f, 30000.f);
    }
    if (glfwGetKey(window, GLFW_KEY_8) == GLFW_PRESS) {
        starTemperatureInKelvin = glm::clamp(starTemperatureInKelvin - 15.0f, 800.f, 30000.f);
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        xPos -= 1;
        float xOffset = xPos - lastX;
        lastX = xPos;
        camera.ProcessMouseMovement(xOffset, 0);
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        xPos += 1;
        float xOffset = xPos - lastX;
        lastX = xPos;
        camera.ProcessMouseMovement(xOffset, 0);
    }
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        yPos -= 1;
        float yOffset = lastY - yPos; 
        lastY = yPos;
        camera.ProcessMouseMovement(0, yOffset);
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        yPos += 1;
        float yOffset = lastY - yPos; 
        lastY = yPos;
        camera.ProcessMouseMovement(0, yOffset);
    }
    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS) {
        yScroll += 0.16f;
        camera.ProcessMouseScroll(yScroll);
    }
    if (glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS) {
        yScroll -= 0.16f;
        camera.ProcessMouseScroll(yScroll);
    }

    glfwSetCursorPos(window, lastX, lastY);
}

void Application::FramebufferSizeCallback(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

void Application::MouseCallback(GLFWwindow*, double xPos, double yPos) {
    if (isFirstMouse) {
        lastX = xPos;
        lastY = yPos;
        isFirstMouse = false;
    }

    float xOffset = xPos - lastX;
    float yOffset = lastY - yPos; 

    lastX = xPos;
    lastY = yPos;

    camera.ProcessMouseMovement(xOffset, yOffset);
}

void Application::ScrollCallback(GLFWwindow*, double, double yOffset) {
    camera.ProcessMouseScroll(yOffset);
}

void Application::KeyCallback(GLFWwindow*, int key, int, int action, int) {
    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        isTimeRun = !isTimeRun;
    }
    if (key == GLFW_KEY_Z && action == GLFW_PRESS) {
        isRenderPlanetStarDistances = !isRenderPlanetStarDistances;
    }
    if (key == GLFW_KEY_X && action == GLFW_PRESS) {
        isRenderSatelliteDistances = !isRenderSatelliteDistances;
    }
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        isRenderHints = !isRenderHints;
    }
    if (key == GLFW_KEY_F1 && action == GLFW_PRESS) {
        isVertSyncEnabled = !isVertSyncEnabled;
        VertSync(isVertSyncEnabled);
    }

    if (action == GLFW_PRESS) {
        // Planet focus presets (smooth transition) - using known fixed positions
        auto doFocus = [&](glm::vec3 p, float r) {
            glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f)) * (r + 30.0f);
            glm::vec3 tpos = p + offset;
            glm::vec3 dir = glm::normalize(p - tpos);
            float ty = glm::degrees(std::atan2(dir.z, dir.x));
            float tp = glm::degrees(std::asin(dir.y));
            camera.StartTransitionTo(tpos, ty, tp, 2.0f);
        };
        if (key == GLFW_KEY_F2) doFocus(glm::vec3(1500.f,0,350.f), 2.0f*0.38f); // merc approx
        if (key == GLFW_KEY_F3) doFocus(glm::vec3(1125.f,0,-1340.f), 2.0f*0.95f); // venus
        if (key == GLFW_KEY_F4) doFocus(glm::vec3(1900.f,0,0.f), 2.0f); // earth
        if (key == GLFW_KEY_F5) doFocus(glm::vec3(-1732.f,0,1000.f), 2.0f*0.53f); // mars
        if (key == GLFW_KEY_F6) doFocus(glm::vec3(1350.f,0,1737.f), 2.0f*9.14f); // jup approx
        if (key == GLFW_KEY_F7) doFocus(glm::vec3(0.f,-100.f,2450.f), 2.0f*9.14f*5); // sat
        if (key == GLFW_KEY_F8) doFocus(glm::vec3(0.f,0.f,-2650.f), 2.0f*4); // ura
        if (key == GLFW_KEY_F9) doFocus(glm::vec3(-2900.f,0,0.f), 2.0f*4); // nep
        if (key == GLFW_KEY_F10) doFocus(glm::vec3(2800.f,0,1757.f), 2.0f*1); // plu
        if (key == GLFW_KEY_F11) { glm::vec3 p(0); doFocus(p, 10); }

        // Time scale / pause / step
        if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
            gTimeScale = glm::clamp(gTimeScale * 2.0f, 0.01f, 10000.0f);
        }
        if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
            gTimeScale = glm::clamp(gTimeScale / 2.0f, 0.01f, 10000.0f);
        }
        if (key == GLFW_KEY_P) {
            gTimePaused = !gTimePaused;
        }
        if (key == GLFW_KEY_PERIOD) {
            gAdvanceStep = true;
        }
    }
}

bool Application::WGLExtensionSupported(const char* extensionName) {
#ifdef __EMSCRIPTEN__
    return false;
#else
    PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsStringEXT = nullptr;
    wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");
    return strstr(wglGetExtensionsStringEXT(), extensionName) != nullptr;
#endif
}

void Application::VertSync(bool enable) {
#ifdef __EMSCRIPTEN__
    glfwSwapInterval(enable ? 1 : 0);
#else
    PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = nullptr;
    PFNWGLGETSWAPINTERVALEXTPROC wglGetSwapIntervalEXT = nullptr;

    if (WGLExtensionSupported("WGL_EXT_swap_control")) {
        wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        wglGetSwapIntervalEXT = (PFNWGLGETSWAPINTERVALEXTPROC)wglGetProcAddress("wglGetSwapIntervalEXT");
    }

    if (wglSwapIntervalEXT)
        wglSwapIntervalEXT(enable);
#endif
}

void Application::StopSearchNearestPlanet() {
    _isSearchNearestPlanet = false;
#ifndef __EMSCRIPTEN__
    if (_searchNearestPlanetThread)
        _searchNearestPlanetThread->join();
#endif
}

void Application::UpdatePlanetSystemLoading() {
#ifdef __EMSCRIPTEN__
    const glm::vec3 camPos = camera.GetPosition();
    for (auto& manifest : _planetSystemManifests) {
        if (manifest.state == PlanetSystemManifest::State::READY) continue;

        if (manifest.state == PlanetSystemManifest::State::NOT_LOADED) {
            float dist = glm::length(camPos - manifest.proxyPosition);
            if (dist < manifest.activationRadius) {
                std::cout << "[StagedLoading] Camera within " << dist
                          << " units — starting download for " << manifest.name << std::endl;
                manifest.state = PlanetSystemManifest::State::DOWNLOADING;
                manifest.totalDownloads = static_cast<int>(manifest.assetPaths.size());
                manifest.pendingDownloads = manifest.totalDownloads;

                // Download required assets — decrement pendingDownloads on completion (success or failure)
                for (const auto& path : manifest.assetPaths) {
                    WebResourceFetcher::DownloadFile(path, path, [&manifest](bool success) {
                        manifest.pendingDownloads--;
                        if (!success) {
                            std::cerr << "[StagedLoading] Required asset failed for "
                                      << manifest.name << std::endl;
                        }
                    });
                }

                // Download optional assets (moons, rings, clouds) — fire-and-forget.
                // These do NOT block init; failure is expected when textures are not yet deployed.
                for (const auto& path : manifest.optionalAssetPaths) {
                    WebResourceFetcher::DownloadFile(path, path, [name = manifest.name](bool success) {
                        if (!success) {
                            std::cout << "[StagedLoading] Optional asset unavailable for "
                                      << name << " — fallback texture will be used." << std::endl;
                        }
                    });
                }
            }
        }

        if (manifest.state == PlanetSystemManifest::State::DOWNLOADING) {
            if (manifest.pendingDownloads <= 0) {
                std::cout << "[StagedLoading] Required assets ready for " << manifest.name
                          << " — initializing system." << std::endl;
                manifest.initFunc();
                manifest.state = PlanetSystemManifest::State::READY;
            }
        }
    }
#endif
}

void Application::UpdateLOD() {
#ifdef __EMSCRIPTEN__
    if (_renderableSceneComponents.empty()) return;

    const glm::vec3 camPos = camera.GetPosition();

    if (g_qualityPreset == 0) {
        // Low preset: force downgrade any high-res (using fake far pos to trigger logic) and skip upgrades.
        for (auto& rc : _renderableSceneComponents) {
            if (rc.planet && rc.planet->IsHighResLoaded()) {
                glm::vec3 fakeFar = camPos + glm::vec3(100000.0f, 0.0f, 0.0f);
                rc.planet->LoadHighResIfClose(fakeFar);
            }
        }
        return;
    }

    // Call on *all* ready planets: far ones will downgrade if loaded + past hysteresis;
    // near ones will upgrade if appropriate.
    for (auto& rc : _renderableSceneComponents) {
        if (rc.planet) {
            rc.planet->LoadHighResIfClose(camPos);
        }
    }
#endif
}

void Application::RenderPlanetProxyMarkers() const {
#ifdef __EMSCRIPTEN__
    if (_planetSystemManifests.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _cameraProjection);
    _mainTextShader->SetMat4("view", _cameraView);
    _mainTextShader->SetBool("is3D", true);

    for (const auto& manifest : _planetSystemManifests) {
        if (manifest.state == PlanetSystemManifest::State::READY) continue;

        std::wstring label(manifest.name.begin(), manifest.name.end());
        if (manifest.state == PlanetSystemManifest::State::NOT_LOADED) {
            label += L" (approach to load)";
        } else if (manifest.state == PlanetSystemManifest::State::DOWNLOADING) {
            int done = manifest.totalDownloads - manifest.pendingDownloads;
            int pct = (manifest.totalDownloads > 0) ? (done * 100 / manifest.totalDownloads) : 0;
            label += L" [downloading " + std::to_wstring(pct) + L"%]";
        }

        std::deque<wchar_t> chars(label.begin(), label.end());
        _mainTextShader->SetVec3("particleCenterWorldSpace", manifest.proxyPosition);
        _textRenderer->Render(*_mainTextShader, chars, 0.0, 0.0, 0.075, glm::vec3(0.5f, 0.7f, 1.0f));
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#endif
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

void Application::StartPlayBackgroundMusic() {
    _isBackgroundMusicPlay = true;

#ifdef __EMSCRIPTEN__
    _currentSongIndex = 0;
    _musicStartTime = 0;
    _musicDuration = 0;
    _currentMusic = nullptr;
#else
    auto mapRange = [](float value, float inMin, float inMax, float outMin, float outMax) {
        return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
    };

    _backgroundMusicThread = make_unique<thread>([=]() {
        for (ssize_t i = 0; i < _backgroundSongs.size() && _isBackgroundMusicPlay; i++) {
            this_thread::sleep_for(1s); 

            auto song = _soundEngine->play2D(_backgroundSongs[i].data(), false, true, true);
            song->setVolume(0);
            song->setIsPaused(false);
            _currentMusicTrack = _backgroundSongs[i].substr(16); 

            while (!song->isFinished()) {
                if (song->getPlayPosition() < 5000) { 
                    auto x = mapRange(song->getPlayPosition(), 0.0f, 5000.0f, 0.0f, 0.7f); 
                    auto volume = exp(x) - 1; 
                    song->setVolume(clamp(volume, 0.0f, 1.0f));
                }
                else if (song->getPlayPosition() > song->getPlayLength() - 5000) { 
                    auto x = mapRange(song->getPlayPosition(), song->getPlayLength() - 5000, song->getPlayLength(), 0.0f, 6.0f);
                    auto volume = exp(-x); 
                    song->setVolume(clamp(volume, 0.0f, 1.0f));
                }

                this_thread::sleep_for(25ms); 
            }

            if (i == _backgroundSongs.size() - 1) 
                i = -1; 

            song->drop();
        }
    });
#endif
}

void Application::UpdateBackgroundMusic() {
#ifdef __EMSCRIPTEN__
    if (!_isBackgroundMusicPlay || _backgroundSongs.empty())
        return;

    if (!Mix_PlayingMusic()) {
        if (_currentMusic) {
            Mix_FreeMusic(_currentMusic);
            _currentMusic = nullptr;
        }

        if (_currentSongIndex >= _backgroundSongs.size()) {
            _currentSongIndex = 0; 
        }

        _currentMusic = Mix_LoadMUS(_backgroundSongs[_currentSongIndex].data());
        if (_currentMusic) {
            _currentMusicTrack = _backgroundSongs[_currentSongIndex].substr(16); 
            Mix_PlayMusic(_currentMusic, 1);
            Mix_VolumeMusic(MIX_MAX_VOLUME * 0.3); 
            _musicStartTime = SDL_GetTicks();
            _musicDuration = 180000; 
            
            _currentSongIndex++;
        }
    } else {
        uint32_t currentTime = SDL_GetTicks();
        uint32_t elapsed = currentTime - _musicStartTime;
        
        if (elapsed < 5000) {
            float fade = static_cast<float>(elapsed) / 5000.0f;
            Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * 0.3f * fade));
        }
        else if (_musicDuration > 0 && elapsed > _musicDuration - 5000) {
            float fade = static_cast<float>(_musicDuration - elapsed) / 5000.0f;
            Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * 0.3f * fade));
        }
        else {
            Mix_VolumeMusic(MIX_MAX_VOLUME * 0.3); 
        }
    }
#endif
}

void Application::StopPlayBackgroundMusic() {
    _isBackgroundMusicPlay = false;
#ifdef __EMSCRIPTEN__
    if (_currentMusic) {
        Mix_HaltMusic();
        Mix_FreeMusic(_currentMusic);
        _currentMusic = nullptr;
    }
#else
    _soundEngine->stopAllSounds();
    if (_backgroundMusicThread)
        _backgroundMusicThread->join();
#endif
}

void Application::Dispose() {
    glfwTerminate();
    
#ifndef __EMSCRIPTEN__
    SDL_Quit();
#endif
    
    IMG_Quit();
    StopSearchNearestPlanet();
    StopPlayBackgroundMusic();
    
#ifdef __EMSCRIPTEN__
    Mix_CloseAudio();
#else
    _soundEngine->drop();
#endif
}

float Application::CalculateSpaceObjectDistance(const SpaceObject* spaceObject) const {
    return glm::distance(camera.GetPosition(), spaceObject->GetPosition());
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

void Application::ConfigureMainPlanetShader(const RenderableSceneComponent& renderableComponent) {
    _mainPlanetShader->SetMat4("lightSpaceMatrix", renderableComponent.lightSpaceMatrix);
    _mainPlanetShader->SetBool("isNearbyPlanetaryRing", renderableComponent.planetaryRing != nullptr);

    if (renderableComponent.planetaryRing) {
        _mainPlanetShader->SetVec3("ringCenter", renderableComponent.planetaryRing->GetPosition());
        _mainPlanetShader->SetVec3("ringNormal", renderableComponent.planetaryRing->GetRingNormal());
        _mainPlanetShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(renderableComponent.planetaryRing->GetInnerRadius(), renderableComponent.planetaryRing->GetOuterRadius()));
        _mainPlanetShader->SetInt("ringDiffuse", 7);
        glBindTextureUnit(7, renderableComponent.planetaryRing->GetRingTexture());
    }
}

Application::~Application() {
    Dispose();
}
