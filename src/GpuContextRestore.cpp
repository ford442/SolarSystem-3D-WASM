// Getting the scene back after the GL context is lost (docs/ARCHITECTURE.md §7.8).
//
// Web: the browser fires webglcontextlost (bootstrap.ts pauses the main loop) and later
// webglcontextrestored on the same WebGL2RenderingContext object, now empty; the page calls
// the OnContextRestored export, which lands in RestoreGpuContext. Native: a GL_KHR_robustness
// reset leaves a context that can never draw again, so PollGpuContextReset replaces the window
// and context and then restores in the same way.
#include "Application.h"
#include "assets/UploadQueue.h"
#include "core/Log.h"
#include "gfx/GlCapabilities.h"
#include "gfx/GlPlatform.h"
#include "gfx/GpuResourceRegistry.h"
#include "gfx/GpuTimer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <ratio>

namespace {
// What Emscripten's GL layer keeps about the lost context. Its object tables still map our old
// integer names to dead WebGL objects; C++ has already forgotten those names
// (GpuResources::InvalidateAll), so drop the entries and let the objects be collected. New
// names keep counting up, so nothing is reused. Extensions are off again after a restore and
// the extension objects Emscripten cached are dead: enable them exactly as context creation
// did (GL.initExtensions, which skips WEBGL_lose_context and the debug ones).
// clang-format off
EM_JS(void, ResetWebGlLayerAfterRestore, (), {
    var ctx = GL.currentContext;
    if (!ctx) {
        return;
    }
    [GL.buffers, GL.textures, GL.framebuffers, GL.renderbuffers, GL.programs, GL.shaders, GL.vaos, GL.queries,
     GL.samplers, GL.transformFeedbacks, GL.syncs].forEach(function(table) {
        if (table) {
            table.fill(null);
        }
    });
    if (typeof GL.initExtensions == 'function') {
        ctx.initExtensionsDone = false;
        GL.initExtensions(ctx);
    } else {
        var gl = ctx.GLctx;
        (gl.getSupportedExtensions() || []).forEach(function(name) {
            if (name.indexOf('lose_context') < 0 && name.indexOf('debug') < 0) {
                gl.getExtension(name);
            }
        });
    }
});
// clang-format on
} // namespace
#endif

void Application::RestoreGpuContext() {
    const auto started = std::chrono::steady_clock::now();
    // 1. Forget every name the old context handed out (no glDelete*: they are not ours any more).
    const std::size_t handles = GpuResources::InvalidateAll();
#ifdef __EMSCRIPTEN__
    ResetWebGlLayerAfterRestore();
#endif
    // 2. What the new context needs before any object exists: extensions and the probe that
    //    decides texture formats and float targets, then the state set once at start-up.
    RefreshGlCapabilities();
    ApplyGlobalGlState();
    EnableGlDebugOutput();
    // 3. Every owner rebuilds itself in place (programs replay their uniforms; textures reload
    //    from MEMFS through the upload budget; lazily-built buffers wait for their next use).
    const std::size_t owners = GpuResources::RecreateAll();
    // 4. The few things rebuilt from data only the renderer and the app hold.
    _renderer.RestoreGpu(_missionCatalog, _simState.qualityPreset);
    const bool gpuTimers = _gpuTimer && _gpuTimer->Enabled();
    _gpuTimer.reset(); // its query pool named objects on the old context
    if (gpuTimers) {
        SetGpuTimersEnabled(true);
    }
    LOG_INFO("GL", "context restored (#%u): %zu stale names dropped, %zu owners rebuilt, %zu uploads queued (%.0f ms)",
             static_cast<unsigned>(GpuResources::Generation()), handles, owners, UploadQueue::Get().Pending(),
             std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
}

#ifndef __EMSCRIPTEN__
bool Application::PollGpuContextReset() {
    // glGetGraphicsResetStatus is core in 4.5 and NO_ERROR on a context that is not robust.
    const GLenum status = glGetGraphicsResetStatus ? glGetGraphicsResetStatus() : GL_NO_ERROR;
    if (_contextLossPeriod < 0.0) {
        // Test hook: SOLARSYSTEM_LOSE_CONTEXT=<seconds> replaces the context that often, through
        // the same path as a real reset (the native counterpart of the page's ?losecontext=).
        const char* every = std::getenv("SOLARSYSTEM_LOSE_CONTEXT");
        _contextLossPeriod = every ? std::max(0.0, std::atof(every)) : 0.0;
        _nextSimulatedContextLoss = glfwGetTime() + _contextLossPeriod;
    }
    const bool simulated = _contextLossPeriod > 0.0 && glfwGetTime() >= _nextSimulatedContextLoss;
    if (simulated) {
        _nextSimulatedContextLoss = glfwGetTime() + _contextLossPeriod;
    }
    if (status == GL_NO_ERROR && !simulated) {
        return false;
    }
    if (simulated) {
        LOG_INFO("GL", "SOLARSYSTEM_LOSE_CONTEXT: replacing the context");
    } else {
        LOG_WARN("GL", "graphics reset (0x%x); replacing the context", status);
    }
    RecreateNativeWindow();
    RestoreGpuContext();
    return true;
}

void Application::RecreateNativeWindow() {
    // The window hints from InitSystems are still in effect (GLFW keeps them until
    // glfwDefaultWindowHints), robustness and MSAA samples included.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    glfwGetWindowPos(_mainWindow, &x, &y);
    glfwGetWindowSize(_mainWindow, &width, &height);
    const bool visible = glfwGetWindowAttrib(_mainWindow, GLFW_VISIBLE) == GLFW_TRUE;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* fresh = glfwCreateWindow(width > 0 ? width : _displayWidth, height > 0 ? height : _displayHeight,
                                         "SolarSystem", nullptr, nullptr);
    if (!fresh) {
        throw std::runtime_error("cannot create a new GL context after a reset");
    }
    glfwSetWindowPos(fresh, x, y);
    GLFWwindow* old = _mainWindow;
    _mainWindow = fresh;
    glfwMakeContextCurrent(_mainWindow);
    InstallWindowCallbacks();
    glfwSetInputMode(_mainWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    VertSync(_isVertSyncEnabled); // the swap interval belongs to the context
    if (visible) {
        glfwShowWindow(_mainWindow);
    }
    glfwDestroyWindow(old); // takes the reset context with it
}
#endif
