#ifndef SOLARSYSTEM_GFX_RHI_BACKEND_H
#define SOLARSYSTEM_GFX_RHI_BACKEND_H
#include <cstdint>
#include <optional>
#include <string_view>

/**
 * Which GPU API a command list records against (docs/ARCHITECTURE.md §7.5, §7.9). GL-free.
 *
 * The GL backend is one implementation compiled two ways (desktop GL 4.6 natively, WebGL 2 on
 * the web), so it reports whichever this build is. The WebGPU backend (gfx/rhi/wgpu/, CMake
 * SOLARSYSTEM_WEBGPU) is a second implementation of the same rhi::CommandList.
 */
namespace rhi {

enum class Backend : std::uint8_t {
    OpenGl, // desktop GL 4.6 core
    WebGl2, // WebGL 2 (GLSL ES 3.00), the web default
    WebGpu, // webgpu.h: emdawnwebgpu on the web, Dawn natively
};

/** "OpenGL 4.6", "WebGL 2", "WebGPU": what the perf HUD and [FrameStats] print. */
const char* BackendName(Backend backend);
/** "opengl", "webgl2", "webgpu": the stable id in GetFrameStatsJson and on the command line. */
const char* BackendId(Backend backend);
/** The GL flavour this binary was compiled for (WebGl2 under Emscripten, else OpenGl). */
Backend GlBackendOfThisBuild();
/**
 * Parses a backend request: "webgpu"/"wgpu" → WebGpu, "webgl"/"webgl2"/"gl"/"opengl" → the GL
 * backend of this build. Anything else (including empty) is no request.
 */
std::optional<Backend> ParseBackend(std::string_view text);

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_BACKEND_H
