// Platform/window bring-up and teardown: GLFW + SDL + GL context creation, the window
// icon, framebuffer resize plumbing and vertical sync. Everything here runs once at
// startup or in response to a window event — no per-frame work.
#include "Application.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "astro/Ephemeris.h"
#include "gfx/GlCapabilities.h"
#include "assets/TextureFormatSupport.h"
#include "Solar_System/OrbitLayout.h"
#include "core/Log.h"
#ifndef __EMSCRIPTEN__
#include <SDL_image.h> // native window icon only; the web build links neither SDL nor SDL_image
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

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
        LOG_INFO("Texture", "Using the '%s' KTX2 pack", preferred);
        return;
    }
    LOG_INFO("Texture", "No '%s' KTX2 pack published; using .dds%s", preferred,
             GetGlCapabilities().s3tcCompressedTextures ? "" : " with software BC decode");
#endif
}
} // namespace

// Error Callback
void glfwErrorCallback(int error, const char* description) {
    LOG_ERROR("GLFW", "error %d: %s", error, description);
}

#ifndef __EMSCRIPTEN__
namespace {
// A KHR_debug context in Debug builds; SOLARSYSTEM_GL_DEBUG=1/0 forces it on/off in any build.
bool GlDebugRequested() {
    if (const char* value = std::getenv("SOLARSYSTEM_GL_DEBUG"); value && *value) {
        return std::string(value) != "0";
    }
#ifdef NDEBUG
    return false;
#else
    return true;
#endif
}

Log::Level GlDebugLevel(GLenum type, GLenum severity) {
    if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
        return Log::Level::Error;
    }
    switch (severity) {
        case GL_DEBUG_SEVERITY_MEDIUM: return Log::Level::Warn;
        case GL_DEBUG_SEVERITY_LOW: return Log::Level::Info;
        default: return Log::Level::Debug; // GL_DEBUG_SEVERITY_NOTIFICATION
    }
}

void GLAPIENTRY GlDebugCallback(GLenum /*source*/, GLenum type, GLuint id, GLenum severity, GLsizei /*length*/,
                                const GLchar* message, const void* /*userParam*/) {
    const Log::Level level = GlDebugLevel(type, severity);
    if (!Log::Enabled(level)) {
        return;
    }
    // A driver repeats the same message every draw (e.g. an incomplete placeholder texture);
    // print each id a few times, then say once that it is being dropped.
    constexpr int kRepeatsPerId = 5;
    static std::unordered_map<GLuint, int> seen;
    const int count = ++seen[id];
    if (count > kRepeatsPerId) {
        return;
    }
    Log::Write(level, "GL", "0x%x: %s%s", id, message,
               count == kRepeatsPerId ? " (further messages with this id suppressed)" : "");
}
} // namespace
#endif

void Application::InitSystems() {
    OrbitLayout::SetJulianDate(Ephemeris::JulianDateNowUtc());

    glfwSetErrorCallback(glfwErrorCallback);

    if (!glfwInit()) {
        throw runtime_error("Failed to initialize GLFW");
    }

#ifdef __EMSCRIPTEN__
    _simState.isMobileWeb = ReadIsMobileWeb();
    _simState.qualityPreset = ReadInitialQualityPreset();
    _simState.backingStoreScale = ReadBackingStoreScale();
#else
    _simState.qualityPreset = ReadInitialQualityPreset();
#endif
    // Context samples are fixed when the context is created, so they must come from the
    // *initial* quality preset; ApplyQualityPreset cannot change them later (same
    // restriction as web, where the WebGL context's `antialias` is decided in
    // web/src/webglContext.ts). A tier with HDR asks for none: see ContextMsaaSamples.
    const auto qualitySettings = GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb);
    const int requestedSamples = ContextMsaaSamples(qualitySettings);

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    // GLFW_SAMPLES (and every other GLFW context-attribute hint) is inert here: the
    // WebGL2 context is created up front by web/src/bootstrap.ts (with antialias already
    // decided from the same quality tier) and handed in via Module['preinitializedWebGLContext'],
    // which GLFW/Emscripten reuse as-is instead of building a context from these hints.
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, requestedSamples);
    // Debug builds (or SOLARSYSTEM_GL_DEBUG=1): KHR_debug context, messages routed into Log.
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GlDebugRequested() ? GLFW_TRUE : GLFW_FALSE);
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
        _displayWidth = static_cast<uint16_t>(std::lround(cssWidth * _simState.backingStoreScale));
        _displayHeight = static_cast<uint16_t>(std::lround(cssHeight * _simState.backingStoreScale));
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
    _simState.contextSamples = actualSamples;
    // The tier's HDR flag is only a request (HDR::InitFBO can refuse); the "[Quality] Active
    // tier" line later prints the path that actually runs.
    LOG_INFO("Quality", "Context samples: requested %d, GL_SAMPLES=%d; %s", requestedSamples, actualSamples,
             qualitySettings.enableHdr ? "HDR tone-map pass is the antialiasing path"
                                       : "HDR off, antialiasing is the context's");

    // The UI reports _isVertSyncEnabled from startup, but only the F-key toggle used to
    // call through to the driver — so the reported state could disagree with reality
    // until the user pressed it. Apply the initial state here instead. Web applies it in
    // Exec(): there the swap interval is main-loop timing, which needs the loop to exist.
#ifndef __EMSCRIPTEN__
    VertSync(_isVertSyncEnabled);
#endif

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

    if (GlDebugRequested() && glDebugMessageCallback) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(GlDebugCallback, nullptr);
        LOG_INFO("GL", "KHR_debug output enabled");
    }

    if (const char* selfTest = std::getenv("SOLARSYSTEM_SHADER_SELFTEST"); selfTest && *selfTest && std::string(selfTest) != "0") {
        const int failures = Shader::SelfTestDirectory("resource/shaders");
        glfwTerminate();
        std::exit(failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
    }
#endif

    FT_Init_FreeType(&_ft);

#ifdef __EMSCRIPTEN__
    // No SDL on web at all: GLFW owns the canvas and web/src/music.ts owns audio. SDL_image
    // exists only for the native window icon (LoadWindowIcon), so the SDL ports are not linked.
#else
    // SDL's default SIGINT/SIGTERM handlers turn the signal into an SDL_QUIT event, but the
    // frame loop is GLFW's and never polls SDL events — so Ctrl-C and `timeout` were
    // swallowed and the process had to be SIGKILLed. Keep the default (terminating) handlers.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_EVERYTHING)) {
        Dispose();
        throw runtime_error("Failed to init SDL");
    }
#endif

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        LOG_WARN("Audio", "Failed to init SDL_mixer: %s", Mix_GetError());
        _mixerInitialized = false;
    } else {
        Mix_AllocateChannels(16);
        _mixerInitialized = true;
        LOG_INFO("Audio", "SDL_mixer initialized");
    }
#elif !defined(__EMSCRIPTEN__)
    _soundEngine = irrklang::createIrrKlangDevice(irrklang::ESOD_AUTO_DETECT,
                                                  irrklang::ESEO_MULTI_THREADED | irrklang::ESEO_LOAD_PLUGINS);
    if (!_soundEngine) {
        throw runtime_error("Failed to init sound engine");
    }
    _soundEngine->setSoundVolume(0.3);
#endif

#ifndef __EMSCRIPTEN__
    if (!IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG)) {
        Dispose();
        std::string msg = "Failed to init SDL_Image: ";
        msg += IMG_GetError();
        throw runtime_error(msg);
    }
#endif

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
    // Order matters: every destructor below issues glDelete*, so the scene and renderer go
    // while the context is still current; then the worker threads; only then the window
    // system and the context with it.
    ReleaseGpuResources();
    StopPlayBackgroundMusic();

    glfwTerminate();

#ifndef __EMSCRIPTEN__
    IMG_Quit();
    SDL_Quit();
#endif

#ifdef SOLARSYSTEM_USE_SDL_MIXER
    Mix_CloseAudio();
#elif !defined(__EMSCRIPTEN__)
    _soundEngine->drop();
#endif
}

void Application::ReleaseGpuResources() {
    _renderableSceneComponents.clear();
    _planetSystemManifests.clear();
    _sun.reset();
    _asteroidField.reset();
    _kuiperField.reset();
    _sphereModel.reset();
    _renderer.Release();
}

void Application::DisplaySystemInformation() const {
    const auto glString = [](GLenum name) {
        const GLubyte* value = glGetString(name);
        return value ? reinterpret_cast<const char*>(value) : "?";
    };
    LOG_INFO("GPU", "Supplier: %s", glString(GL_VENDOR));
    LOG_INFO("GPU", "Renderer: %s", glString(GL_RENDERER));

    GLint majorVersion, minorVersion;
    glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
    glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
    LOG_INFO("GPU", "OpenGL version: %d.%d", majorVersion, minorVersion);

#ifndef __EMSCRIPTEN__
    // GL_GPU_MEMORY_INFO_*_NVX comes from NV_NVX_gpu_memory_info; querying it on AMD or
    // Intel just raises GL_INVALID_ENUM and prints garbage.
    if (glewIsSupported("GL_NVX_gpu_memory_info")) {
        GLint totalMemoryKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalMemoryKb);

        GLint currentMemoryKb = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &currentMemoryKb);
        LOG_INFO("GPU", "Total memory: %d kb, free: %d kb", totalMemoryKb, currentMemoryKb);
    }
#endif

    GLint maxTextureSize;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    LOG_INFO("GPU", "Max texture size: %dx%d", maxTextureSize, maxTextureSize);
    LOG_INFO("GPU", "Driver: %s", glString(GL_VERSION));
}

#ifndef __EMSCRIPTEN__
void Application::LoadWindowIcon() const {
    constexpr auto execIconPath = "resource/icons/solarsystem-logo.png";
    SDL_Surface* windowIcon = IMG_Load(execIconPath);
    if (windowIcon == nullptr) {
        LOG_WARN("Window", "Cannot load window icon %s (%s)", execIconPath, IMG_GetError());
        return;
    }
    if (windowIcon->w > 256 || windowIcon->h > 256) {
        LOG_WARN("Window", "Skipping oversized window icon (%dx%d)", windowIcon->w, windowIcon->h);
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
#endif

void Application::HandleResize(int width, int height) {
    width = std::max(1, width);
    height = std::max(1, height);
    if (width == _displayWidth && height == _displayHeight) {
        return; // Debounce: GLFW/browser can report the same size more than once.
    }
    _displayWidth = static_cast<uint16_t>(width);
    _displayHeight = static_cast<uint16_t>(height);
    _camera.SetAspect(static_cast<float>(width) / static_cast<float>(height));
    _renderer.Resize(_displayWidth, _displayHeight);
    glViewport(0, 0, width, height);
}

void Application::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app) {
        glViewport(0, 0, width, height);
        return;
    }
    if (app->_capture.resized) {
        return; // a capture renders at its own size (WebWindowResizeCallback)
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
    // A capture renders at its own size; EndCapture fits the canvas to the window again.
    if (!app->_capture.resized) {
        app->FitCanvasToCss();
    }
    return EM_FALSE; // Don't block the event from reaching other listeners.
}

void Application::FitCanvasToCss() {
    double cssWidth = 0.0, cssHeight = 0.0;
    emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight);
    if (cssWidth > 0.0 && cssHeight > 0.0) {
        const int width = static_cast<int>(std::lround(cssWidth * _simState.backingStoreScale));
        const int height = static_cast<int>(std::lround(cssHeight * _simState.backingStoreScale));
        emscripten_set_canvas_element_size("#canvas", width, height);
        HandleResize(width, height);
    }
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
