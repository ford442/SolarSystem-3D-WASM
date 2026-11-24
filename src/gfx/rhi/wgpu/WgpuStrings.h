#ifndef SOLARSYSTEM_GFX_RHI_WGPU_WGPUSTRINGS_H
#define SOLARSYSTEM_GFX_RHI_WGPU_WGPUSTRINGS_H
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_cpp.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace rhi::webgpu {

/** A webgpu.h string (pointer + length, or NUL-terminated when the length is WGPU_STRLEN). */
inline std::string ToString(wgpu::StringView view) {
    if (!view.data) {
        return {};
    }
    const std::size_t length = view.length == WGPU_STRLEN ? std::string_view(view.data).size() : view.length;
    return std::string(view.data, length);
}

} // namespace rhi::webgpu

#endif // SOLARSYSTEM_GFX_RHI_WGPU_WGPUSTRINGS_H
