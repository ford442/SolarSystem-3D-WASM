#ifndef SOLARSYSTEM_GFX_RHI_WGPU_WGPUSELFTEST_H
#define SOLARSYSTEM_GFX_RHI_WGPU_WGPUSELFTEST_H
#include <string>

namespace rhi::webgpu {

class Device;

/**
 * SOLARSYSTEM_SHADER_SELFTEST's WGSL leg (docs/ARCHITECTURE.md §7.9), native only: every stage
 * permutation in `<directory>/manifest.json` becomes a shader module on `device`, and every
 * fragment stage is linked with its vertex partner (under the same defines) into a render
 * pipeline with the program's explicit bind group layouts — which is where WebGPU checks that
 * the stages' interfaces match and the bindings fit its limits. Logs a [ShaderSelfTest] summary
 * and returns how many failed.
 */
int SelfTestWgsl(Device& device, const std::string& directory);

} // namespace rhi::webgpu

#endif // SOLARSYSTEM_GFX_RHI_WGPU_WGPUSELFTEST_H
