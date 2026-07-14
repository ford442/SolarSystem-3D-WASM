#include "Application.h"
#include "QualitySettings.h"
#include "ResourceManifest.h"
#include "SimState.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include "Solar_System/OrbitLayout.h"
#include <SDL_image.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <random>
#include <cstring>
#include <iomanip>
#include <iostream>

using namespace std;

namespace {
    Application* activeApplication = nullptr;

#ifdef __EMSCRIPTEN__
    float g_touchForward = 0.0f;
    float g_touchRight = 0.0f;
    float g_touchVertical = 0.0f;
#endif
}

float gTimeScale = 1.0f;
bool gTimePaused = false;
bool gAdvanceStep = false;
int gShadowQuality = 3; // 0=off, 1=low, 2=medium, 3=full

#ifdef __EMSCRIPTEN__
extern "C" {
    // Keep this small control surface as an explicit C ABI consumed through
    // cwrap. If the web API grows to return planet lists or settings structs,
    // migrate those structured values to embind instead of adding pointer-based
    // C exports; these scalar settings remain simpler as cwrap calls.
    EMSCRIPTEN_KEEPALIVE void SetCameraPose(float x, float y, float z, float yaw, float pitch) {
        camera.SetPosition(glm::vec3(x, y, z));
        camera.SetYawPitch(yaw, pitch);
    }
    EMSCRIPTEN_KEEPALIVE void SetQualityPreset(int preset) {
        g_qualityPreset = (preset < 0 ? 0 : preset > 2 ? 2 : preset);
        if (activeApplication) {
            activeApplication->ApplyQualityPreset(g_qualityPreset);
        }
    }
    EMSCRIPTEN_KEEPALIVE int GetQualityPreset() {
        return g_qualityPreset;
    }
    EMSCRIPTEN_KEEPALIVE void SetTimeScale(float scale) {
        gTimeScale = glm::clamp(scale, 0.01f, 10000.0f);
    }
    EMSCRIPTEN_KEEPALIVE float GetTimeScale() {
        return gTimeScale;
    }
    EMSCRIPTEN_KEEPALIVE void SetPaused(bool paused) {
        gTimePaused = paused;
    }
    EMSCRIPTEN_KEEPALIVE int GetPaused() {
        return gTimePaused ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void SetShadowQuality(int quality) {
        if (activeApplication) {
            activeApplication->ApplyShadowQuality(quality);
        } else {
            gShadowQuality = std::clamp(quality, 0, 3);
        }
    }
    EMSCRIPTEN_KEEPALIVE int GetShadowQuality() {
        return gShadowQuality;
    }
    EMSCRIPTEN_KEEPALIVE void SetTouchMovement(float forward, float right, float vertical) {
        g_touchForward = glm::clamp(forward, -1.0f, 1.0f);
        g_touchRight = glm::clamp(right, -1.0f, 1.0f);
        g_touchVertical = glm::clamp(vertical, -1.0f, 1.0f);
    }
    EMSCRIPTEN_KEEPALIVE void AddTouchLook(float deltaX, float deltaY) {
        camera.ProcessMouseMovement(deltaX, deltaY);
    }
    EMSCRIPTEN_KEEPALIVE void AddTouchZoom(float delta) {
        if (std::abs(delta) > 0.0001f) {
            camera.ProcessMouseScroll(delta);
        }
    }
    EMSCRIPTEN_KEEPALIVE int IsMobileWeb() {
        return g_isMobileWeb ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE void SetMusicVolume(float volume) {
        if (activeApplication) {
            activeApplication->SetMusicVolume(volume);
        }
    }
    EMSCRIPTEN_KEEPALIVE float GetMusicVolume() {
        return activeApplication ? activeApplication->GetMusicVolume() : 0.3f;
    }
    EMSCRIPTEN_KEEPALIVE void SetMusicMuted(int muted) {
        if (activeApplication) {
            activeApplication->SetMusicMuted(muted != 0);
        }
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
}
#endif


// Error Callback
void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error (" << error << "): " << description << std::endl;
}

Application::Application() : _fpsHandler(240) {
    activeApplication = this;
    InitSystems();
    ApplyQualityPreset(g_qualityPreset);
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
    g_isMobileWeb = ReadIsMobileWeb();
    g_qualityPreset = ReadInitialQualityPreset();
    const auto qualitySettings = GetQualitySettings(g_qualityPreset, g_isMobileWeb);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_SAMPLES, qualitySettings.requestedMsaaSamples);
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

#ifdef __EMSCRIPTEN__
    GLint actualSamples = 0;
    glGetIntegerv(GL_SAMPLES, &actualSamples);
    std::cout << "[Quality] Requested " << qualitySettings.requestedMsaaSamples
              << "x MSAA, context provides " << actualSamples << " samples" << std::endl;
#endif

    glfwSetFramebufferSizeCallback(_mainWindow, FramebufferSizeCallback);
    glfwSetCursorPosCallback(_mainWindow, MouseCallback);
    glfwSetScrollCallback(_mainWindow, ScrollCallback);
    glfwSetKeyCallback(_mainWindow, KeyCallback);

#ifndef __EMSCRIPTEN__
    glewExperimental = true;
    glewInit();
#endif

    FT_Init_FreeType(&_ft);

#ifndef __EMSCRIPTEN__
    if (SDL_Init(SDL_INIT_EVERYTHING)) {
        Dispose();
        throw runtime_error("Failed to init SDL");
    }
#endif

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "[Audio] Failed to init SDL_mixer: " << Mix_GetError() << std::endl;
        _mixerInitialized = false;
    } else {
        Mix_AllocateChannels(16);
        _mixerInitialized = true;
        std::cout << "[Audio] SDL_mixer initialized" << std::endl;
    }
#else
    _soundEngine = createIrrKlangDevice(ESOD_AUTO_DETECT, ESEO_MULTI_THREADED | ESEO_LOAD_PLUGINS);
    if (!_soundEngine) {
        throw runtime_error("Failed to init sound engine");
    }
    _soundEngine->setSoundVolume(0.3);
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
    int loaded = _totalResources - _resourcesPending;
    // Call JavaScript function to update progress bar
    EM_ASM({
        if (typeof window.updateLoadingProgress === 'function') {
            window.updateLoadingProgress($0, $1);
        }
    }, loaded, _totalResources);
#endif
}


void Application::InitSceneObjects() {
    camera.SetAspect(static_cast<float>(_displayWidth) / static_cast<float>(_displayHeight));
    const auto qualitySettings = GetQualitySettings(
        gShadowQuality == 0 ? g_qualityPreset : gShadowQuality - 1, g_isMobileWeb);
    _shadowMapFBO = make_unique<ShadowMapFBO>(qualitySettings.shadowResolution, qualitySettings.shadowResolution);
    _hdrEnabled = qualitySettings.enableHdr;
    _hdrShader = make_unique<Shader>("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    _hdr = make_unique<HDR>(*_hdrShader, _displayWidth, _displayHeight, _hdrEnabled);
    LogQualityTier(qualitySettings, _hdrEnabled, gShadowQuality);

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
#ifdef _WIN32
    glfwSetWindowMonitor(_mainWindow, glfwGetPrimaryMonitor(), 0, 0, _displayWidth, _displayHeight, GLFW_DONT_CARE);
#endif

    StartSearchNearestPlanet();
    StartPlayBackgroundMusic();
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
    if (windowIcon == nullptr) {
        std::cerr << "WARNING: Cannot load window icon " << execIconPath
                  << " (" << IMG_GetError() << ")" << std::endl;
        return;
    }
    if (windowIcon->w > 256 || windowIcon->h > 256) {
        std::cerr << "WARNING: Skipping oversized window icon ("
                  << windowIcon->w << "x" << windowIcon->h << ")" << std::endl;
        SDL_FreeSurface(windowIcon);
        return;
    }
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
#ifdef SOLARSYSTEM_USE_SDL_MIXER
        SetMusicVolume(GetMusicVolume() + 0.05f);
#else
        _soundEngine->setSoundVolume(clamp(_soundEngine->getSoundVolume() + 0.01, 0.0, 1.0));
#endif
    }
    if (glfwGetKey(window, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
        SetMusicVolume(GetMusicVolume() - 0.05f);
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

#ifdef __EMSCRIPTEN__
    if (g_touchForward > 0.01f) {
        camera.ProcessKeyboard(CameraVector::FORWARD, static_cast<float>(deltaTime) * g_touchForward);
    } else if (g_touchForward < -0.01f) {
        camera.ProcessKeyboard(CameraVector::BACKWARD, static_cast<float>(deltaTime) * -g_touchForward);
    }
    if (g_touchRight > 0.01f) {
        camera.ProcessKeyboard(CameraVector::RIGHT, static_cast<float>(deltaTime) * g_touchRight);
    } else if (g_touchRight < -0.01f) {
        camera.ProcessKeyboard(CameraVector::LEFT, static_cast<float>(deltaTime) * -g_touchRight);
    }
    if (g_touchVertical > 0.01f) {
        camera.ProcessKeyboard(CameraVector::WORLD_UP, static_cast<float>(deltaTime) * g_touchVertical);
    } else if (g_touchVertical < -0.01f) {
        camera.ProcessKeyboard(CameraVector::WORLD_DOWN, static_cast<float>(deltaTime) * -g_touchVertical);
    }
#endif

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
#if defined(__EMSCRIPTEN__) || !defined(_WIN32)
    (void)extensionName;
    return false;
#else
    PFNWGLGETEXTENSIONSSTRINGEXTPROC wglGetExtensionsStringEXT = nullptr;
    wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC)wglGetProcAddress("wglGetExtensionsStringEXT");
    return strstr(wglGetExtensionsStringEXT(), extensionName) != nullptr;
#endif
}

void Application::VertSync(bool enable) {
#if defined(__EMSCRIPTEN__) || !defined(_WIN32)
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


void Application::ApplyOrbitScaleMode(int mode) {
    const auto scaleMode = mode == 1 ? OrbitLayout::ScaleMode::Realistic : OrbitLayout::ScaleMode::Compressed;
    OrbitLayout::SetScaleMode(scaleMode);
    RefreshPlanetProxyPositions();
    std::cout << "[OrbitScale] " << (scaleMode == OrbitLayout::ScaleMode::Realistic ? "realistic" : "compressed")
              << " distances active" << std::endl;
}

void Application::FocusPlanetByIndex(int idx) {
    idx = std::clamp(idx, 0, 9);
    _focusedPlanetIndex = idx;

    glm::vec3 target(0.0f);
    float focusRadius = 10.0f;

    if (idx == 0) {
        if (_sun) {
            target = _sun->GetPosition();
        }
        focusRadius = 40.0f;
    } else {
        const size_t componentIndex = static_cast<size_t>(idx - 1);
        if (componentIndex < _renderableSceneComponents.size()) {
            const auto& planet = _renderableSceneComponents[componentIndex].planet;
            target = planet->GetPosition();
            focusRadius = std::max(planet->GetRadius() * 4.0f, 2.0f);
        } else {
            target = OrbitLayout::GetOffset(static_cast<OrbitLayout::Body>(idx));
            focusRadius = 20.0f;
        }
    }

    const glm::vec3 offset = glm::normalize(glm::vec3(0.7f, 0.3f, 0.7f)) * (focusRadius + 30.0f);
    const glm::vec3 cameraPos = target + offset;
    const glm::vec3 lookDir = glm::normalize(target - cameraPos);
    const float yaw = glm::degrees(std::atan2(lookDir.z, lookDir.x));
    const float pitch = glm::degrees(std::asin(lookDir.y));
    camera.StartTransitionTo(cameraPos, yaw, pitch, 2.0f);

#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (typeof window.onPlanetFocused === 'function') {
            window.onPlanetFocused($0);
        }
    }, idx);
#endif
}

int Application::GetFocusedPlanetIndex() const {
    return _focusedPlanetIndex;
}

int Application::GetNearestPlanetIndexForJs() const {
    if (_nearestPlanetIndex < 0) {
        return -1;
    }
    return static_cast<int>(_nearestPlanetIndex) + 1;
}


void Application::SetMusicVolume(float volume) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    _musicVolume = glm::clamp(volume, 0.0f, 1.0f);
    if (!_musicMuted && _mixerInitialized && Mix_PlayingMusic()) {
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume));
    }
#else
    _soundEngine->setSoundVolume(glm::clamp(volume, 0.0f, 1.0f));
#endif
}

float Application::GetMusicVolume() const {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    return _musicVolume;
#else
    return _soundEngine->getSoundVolume();
#endif
}

void Application::SetMusicMuted(bool muted) {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    _musicMuted = muted;
    if (!_mixerInitialized) {
        return;
    }
    if (_musicMuted) {
        Mix_VolumeMusic(0);
        return;
    }
    if (Mix_PlayingMusic()) {
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume));
    }
#else
    (void)muted;
#endif
}

bool Application::GetMusicMuted() const {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    return _musicMuted;
#else
    return false;
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

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    _currentSongIndex = 0;
    _musicStartTime = 0;
    _musicDuration = 0;
    _currentMusic = nullptr;
#else
    auto mapRange = [](float value, float inMin, float inMax, float outMin, float outMax) {
        return outMin + (outMax - outMin) * (value - inMin) / (inMax - inMin);
    };

    _backgroundMusicThread = make_unique<thread>([=]() {
        for (ssize_t i = 0; i < _backgroundSongPaths.size() && _isBackgroundMusicPlay; i++) {
            this_thread::sleep_for(1s); 

            auto song = _soundEngine->play2D(_backgroundSongPaths[i].c_str(), false, true, true);
            song->setVolume(0);
            song->setIsPaused(false);
            _currentMusicTrack = _backgroundSongPaths[i].substr(16); 

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

            if (i == _backgroundSongPaths.size() - 1) 
                i = -1; 

            song->drop();
        }
    });
#endif
}

void Application::UpdateBackgroundMusic() {
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    if (!_isBackgroundMusicPlay || !_mixerInitialized || _musicMuted || _backgroundSongPaths.empty())
        return;

    if (_availableSongPaths.empty())
        return;

    const auto applyMusicVolume = [this](float fadeMultiplier) {
        Mix_VolumeMusic(static_cast<int>(MIX_MAX_VOLUME * _musicVolume * fadeMultiplier));
    };

    if (!Mix_PlayingMusic()) {
        if (_currentMusic) {
            Mix_FreeMusic(_currentMusic);
            _currentMusic = nullptr;
        }

        const size_t songCount = _backgroundSongPaths.size();
        for (size_t attempt = 0; attempt < songCount; ++attempt) {
            if (_currentSongIndex >= static_cast<int>(songCount)) {
                _currentSongIndex = 0;
            }

            const std::string& path = _backgroundSongPaths[_currentSongIndex];
            ++_currentSongIndex;

            if (_availableSongPaths.count(path) == 0) {
                continue;
            }

            _currentMusic = Mix_LoadMUS(path.c_str());
            if (!_currentMusic) {
                std::cerr << "[Audio] Failed to decode track (skipped): " << path
                          << " (" << Mix_GetError() << ")" << std::endl;
                _availableSongPaths.erase(path);
                continue;
            }

            _currentMusicTrack = path.substr(std::string("resource/sounds/").size());
            Mix_PlayMusic(_currentMusic, 1);
            applyMusicVolume(0.0f);
            _musicStartTime = SDL_GetTicks();
            _musicDuration = 180000;
            std::cout << "[Audio] Now playing: " << _currentMusicTrack << std::endl;
            return;
        }

        std::cerr << "[Audio] No playable tracks available yet; waiting for downloads" << std::endl;
        return;
    }

    const uint32_t currentTime = SDL_GetTicks();
    const uint32_t elapsed = currentTime - _musicStartTime;

    if (elapsed < 5000) {
        applyMusicVolume(static_cast<float>(elapsed) / 5000.0f);
    } else if (_musicDuration > 0 && elapsed > _musicDuration - 5000) {
        applyMusicVolume(static_cast<float>(_musicDuration - elapsed) / 5000.0f);
    } else {
        applyMusicVolume(1.0f);
    }
#endif
}

void Application::StopPlayBackgroundMusic() {
    _isBackgroundMusicPlay = false;
#ifdef SOLARSYSTEM_USE_SDL_MIXER
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
    
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    Mix_CloseAudio();
#elif !defined(__EMSCRIPTEN__)
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


Application::~Application() {
    if (activeApplication == this) {
        activeApplication = nullptr;
    }
    Dispose();
}
