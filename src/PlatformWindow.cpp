// Platform/window bring-up and teardown: GLFW + SDL + GL context creation, the window
// icon, framebuffer resize plumbing and vertical sync. Everything here runs once at
// startup or in response to a window event — no per-frame work.
#include "Application.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/GlCapabilities.h"
#include "Auxiliary_Modules/TextureFormatSupport.h"
#include "Solar_System/OrbitLayout.h"
#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace std;

namespace {
/**
 * Pick the texture pack every later asset path resolves to, from the packs the
 * deployment says it published in `window.__solarSystemTexturePacks`.
 *
 * A deployment that publishes no packs — the default, and every native build — stays on
 * the legacy `.dds` layout, where a GPU without S3TC is covered by the software BC
 * decoder in BlockCompression.cpp. Publishing an `astc`/`etc2` pack is what turns that
 * CPU decode into a real compressed upload on Safari/iOS and Android.
 */
void SelectTexturePack() {
#ifdef __EMSCRIPTEN__
    const char* preferred = GetGlCapabilities().PreferredTexturePack();
    // Bracket notation throughout: Release links with --closure 1, which renames dotted
    // property reads on `window`.
    const bool published = EM_ASM_INT({
        try {
            const packs = (typeof window !== 'undefined' && Array.isArray(window['__solarSystemTexturePacks']))
                ? window['__solarSystemTexturePacks']
                : [];
            return packs.indexOf(UTF8ToString($0)) >= 0 ? 1 : 0;
        } catch (e) {
            return 0;
        }
    }, preferred) != 0;

    if (published) {
        TextureFormats::SetTexturePack(preferred);
        std::cout << "[Texture] Using the '" << preferred << "' KTX2 pack" << std::endl;
        return;
    }
    std::cout << "[Texture] No '" << preferred << "' KTX2 pack published; using .dds"
              << (GetGlCapabilities().s3tcCompressedTextures ? "" : " with software BC decode")
              << std::endl;
#endif
}
} // namespace

// Error Callback
void glfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error (" << error << "): " << description << std::endl;
}

void Application::InitSystems() {
    ios_base::sync_with_stdio(false);
    cin.tie(nullptr);

    OrbitLayout::SetJulianDate(Ephemeris::JulianDateNowUtc());

    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw runtime_error("Failed to initialize GLFW");
    }

#ifdef __EMSCRIPTEN__
    gSimState->isMobileWeb = ReadIsMobileWeb();
    gSimState->qualityPreset = ReadInitialQualityPreset();
    gSimState->backingStoreScale = ReadBackingStoreScale();
#else
    gSimState->qualityPreset = ReadInitialQualityPreset();
#endif
    // MSAA is fixed when the context is created, so it must come from the *initial*
    // quality preset; ApplyQualityPreset cannot change it later (same restriction as
    // web, where the WebGL context's `antialias` is decided in bootstrap.ts).
    const auto qualitySettings = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    // GLFW_SAMPLES (and every other GLFW context-attribute hint) is inert here: the
    // WebGL2 context is created up front by web/src/bootstrap.ts (with antialias already
    // decided from the same quality tier) and handed in via Module.preinitializedWebGLContext,
    // which GLFW/Emscripten reuse as-is instead of building a context from these hints.
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, qualitySettings.requestedMsaaSamples);
#endif

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

#ifdef __EMSCRIPTEN__
    // Size the window from the canvas's actual CSS layout size (driven by index.html's
    // width:100vw/height:100vh) times the capped backing-store scale, not the monitor's
    // screen resolution — screen dimensions can differ from the actual viewport (browser
    // chrome, embedding, multi-monitor) and ignore the DPR cap entirely.
    double cssWidth = 0.0, cssHeight = 0.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    if (cssWidth > 0.0 && cssHeight > 0.0) {
        _displayWidth = static_cast<uint16_t>(std::lround(cssWidth * gSimState->backingStoreScale));
        _displayHeight = static_cast<uint16_t>(std::lround(cssHeight * gSimState->backingStoreScale));
    } else {
        _displayWidth = 1280;
        _displayHeight = 720;
    }
#else
    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if (mode) {
        _displayWidth = mode->width;
        _displayHeight = mode->height;
    } else {
        _displayWidth = 1280;
        _displayHeight = 720;
    }
#endif
    if (_displayWidth == 0) _displayWidth = 800;
    if (_displayHeight == 0) _displayHeight = 600;

    _mainWindow = glfwCreateWindow(_displayWidth, _displayHeight, "SolarSystem", nullptr, nullptr);

    if (_mainWindow == nullptr) {
        glfwTerminate();
        throw runtime_error("Failed to create GLFW window");
    }

#ifdef __EMSCRIPTEN__
    glfwSetInputMode(_mainWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    glfwSetMouseButtonCallback(_mainWindow, [](GLFWwindow* window, int button, int action, int /*mods*/) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
    });
    // GLFW forces the canvas's inline CSS width/height to match the window size (in
    // "non-HiDPI-aware" mode, the default here). Our backing-store size is already
    // scaled by backingStoreScale, so that inline style would blow the canvas up past
    // the viewport; clear it so index.html's width:100vw/height:100vh governs layout
    // while canvas.width/height (the backing store) stays at the scaled resolution.
    EM_ASM({
        const canvas = document.getElementById('canvas');
        if (canvas) {
            canvas.style.removeProperty('width');
            canvas.style.removeProperty('height');
        }
    });
#else
    glfwSetInputMode(_mainWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
#endif

    glfwMakeContextCurrent(_mainWindow);

    GLint actualSamples = 0;
    glGetIntegerv(GL_SAMPLES, &actualSamples);
    std::cout << "[Quality] Requested " << qualitySettings.requestedMsaaSamples
              << "x MSAA, context provides " << actualSamples << " samples" << std::endl;

    // The UI reports _isVertSyncEnabled from startup, but only the F-key toggle used to
    // call through to the driver — so the reported state could disagree with reality
    // until the user pressed it. Apply the initial state here instead.
    VertSync(_isVertSyncEnabled);

    GetGlCapabilities(); // Probed once here (context is current); logs its findings.
    SelectTexturePack();  // Decide which texture pack every later asset path resolves to.

    glfwSetWindowUserPointer(_mainWindow, this);
    glfwSetFramebufferSizeCallback(_mainWindow, FramebufferSizeCallback);
    glfwSetCursorPosCallback(_mainWindow, MouseCallback);
    glfwSetScrollCallback(_mainWindow, ScrollCallback);
    glfwSetKeyCallback(_mainWindow, KeyCallback);
#ifdef __EMSCRIPTEN__
    // See WebWindowResizeCallback: GLFW's own framebuffer-size callback is never invoked
    // by a plain browser window resize, only this listens for it (and orientation change).
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, WebWindowResizeCallback);
#endif

#ifndef __EMSCRIPTEN__
    glewExperimental = true;
    glewInit();
#endif

    FT_Init_FreeType(&_ft);

#ifdef __EMSCRIPTEN__
    // SDL_INIT_EVERYTHING pulls in video/joystick subsystems we do not want on web
    // (GLFW owns the canvas), but SDL_mixer and SDL_image below still need SDL itself
    // initialized, so bring up just the subsystems they use.
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        std::cerr << "[SDL] Failed to init audio/events subsystem: " << SDL_GetError() << std::endl;
    }
#else
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
    if (actualSamples > 1) {
        glEnable(GL_MULTISAMPLE);
    }
#endif
    glEnable(GL_CULL_FACE);

#ifndef __EMSCRIPTEN__
    // No GL_POLYGON_SMOOTH: it is deprecated in core profiles, needs sorted blended
    // geometry to look right, and MSAA above is the antialiasing path we actually use.
    LoadWindowIcon();
#endif

    glCullFace(GL_BACK);
    DisplaySystemInformation();
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

void Application::DisplaySystemInformation() const {
    cout << "GPU Supplier: " << glGetString(GL_VENDOR) << endl;
    cout << "GPU: " << glGetString(GL_RENDERER) << endl;

    GLint majorVersion, minorVersion;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
    cout << "OpenGL version: " << majorVersion << '.' << minorVersion << endl;

#ifndef __EMSCRIPTEN__
    // GL_GPU_MEMORY_INFO_*_NVX comes from NV_NVX_gpu_memory_info; querying it on AMD or
    // Intel just raises GL_INVALID_ENUM and prints garbage.
    if (glewIsSupported("GL_NVX_gpu_memory_info")) {
        GLint totalMemoryKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemoryKb);

        GLint currentMemoryKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &currentMemoryKb);
        cout << "Total GPU Memory: " << totalMemoryKb << " kb\nFree GPU Memory: " << currentMemoryKb << " kb" << endl;
    }
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

void Application::HandleResize(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == _displayWidth && height == _displayHeight) {
        return; // Debounce: GLFW/browser can report the same size more than once.
    }
    _displayWidth = static_cast<uint16_t>(width);
    _displayHeight = static_cast<uint16_t>(height);
    _camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));
    if (_renderer.hdr) {
        _renderer.hdr->Resize(_displayWidth, _displayHeight);
    }
    if (_renderer.magneticFieldBloom) {
        _renderer.magneticFieldBloom->Resize(_displayWidth, _displayHeight);
    }
    glViewport(0, 0, width, height);
}

void Application::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app) {
        glViewport(0, 0, width, height);
        return;
    }
    app->HandleResize(width, height);
}

#ifdef __EMSCRIPTEN__
// Emscripten's GLFW port never wires a browser window resize to
// glfwSetFramebufferSizeCallback on its own — GLFW/Browser only resize the canvas when
// something explicitly calls Browser.setCanvasSize() (e.g. the fullscreen enter/exit path),
// which a plain window resize never does. emscripten_set_resize_callback is the documented,
// independent mechanism that actually listens for window resize (and orientation change).
EM_BOOL Application::WebWindowResizeCallback(int /*eventType*/, const EmscriptenUiEvent*, void* userData) {
    auto* app = static_cast<Application*>(userData);
    double cssWidth = 0.0, cssHeight = 0.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    if (cssWidth > 0.0 && cssHeight > 0.0) {
        const int width = static_cast<int>(std::lround(cssWidth * gSimState->backingStoreScale));
        const int height = static_cast<int>(std::lround(cssHeight * gSimState->backingStoreScale));
        emscripten_set_canvas_element_size("#canvas", width, height);
        app->HandleResize(width, height);
    }
    return EM_FALSE; // Don't block the event from reaching other listeners.
}
#endif

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
