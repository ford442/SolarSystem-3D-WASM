#include "Backend.h"
#include <optional>
#include <string_view>

namespace rhi {

const char* BackendName(Backend backend) {
    switch (backend) {
        case Backend::OpenGl: return "OpenGL 4.6";
        case Backend::WebGl2: return "WebGL 2";
        case Backend::WebGpu: return "WebGPU";
    }
    return "unknown";
}

const char* BackendId(Backend backend) {
    switch (backend) {
        case Backend::OpenGl: return "opengl";
        case Backend::WebGl2: return "webgl2";
        case Backend::WebGpu: return "webgpu";
    }
    return "unknown";
}

Backend GlBackendOfThisBuild() {
#ifdef __EMSCRIPTEN__
    return Backend::WebGl2;
#else
    return Backend::OpenGl;
#endif
}

std::optional<Backend> ParseBackend(std::string_view text) {
    if (text == "webgpu" || text == "wgpu") {
        return Backend::WebGpu;
    }
    if (text == "webgl" || text == "webgl2" || text == "gl" || text == "opengl") {
        return GlBackendOfThisBuild();
    }
    return std::nullopt;
}

} // namespace rhi
