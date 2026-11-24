#ifndef SOLARSYSTEM_GFX_RHI_WGSLBINDINGS_H
#define SOLARSYSTEM_GFX_RHI_WGSLBINDINGS_H
#include <cstdint>
#include <optional>
#include <string_view>

/**
 * Where every GLSL resource lands in WGSL (docs/ARCHITECTURE.md §7.9). GL-free; shared by
 * ShaderSource's WebGPU dialect (which writes these set/binding numbers into the GLSL it hands
 * glslang), tools/wgsl_shader_compiler (which checks Tint kept them) and gfx/rhi/wgpu (which
 * builds the bind group layouts from them). Fixed numbers, so a program's bind groups follow
 * from its source and need no per-program table at draw time:
 *
 *   @group(0)  per-view uniform blocks, at their GL binding point: FrameUBO 0, MagnetosphereUBO 1
 *   @group(1)  textures and samplers: TextureUnit u's texture at 2u, its sampler at 2u + 1
 *              (GLSL's combined sampler2D, split by Tint's SPIR-V reader)
 *   @group(2)  the program's loose `uniform` values, which glslang -R packs into one std140
 *              block per stage: the vertex stage's at binding 0, the fragment stage's at 1 (each
 *              has its own layout; a value both stages read is written into both)
 */
namespace rhi::wgsl {

constexpr std::uint32_t kFrameGroup = 0;
constexpr std::uint32_t kTextureGroup = 1;
constexpr std::uint32_t kUniformGroup = 2;
constexpr std::uint32_t kVertexUniformBinding = 0;
constexpr std::uint32_t kFragmentUniformBinding = 1;
/** glslang's name for the block it packs loose uniforms into (--global-uniform-name). */
constexpr const char* kUniformBlockName = "SsUniforms";

constexpr std::uint32_t TextureBinding(std::uint32_t unit) {
    return 2 * unit;
}
constexpr std::uint32_t SamplerBinding(std::uint32_t unit) {
    return 2 * unit + 1;
}

/** The @group(0) binding of a named uniform block (its GL binding point), if it is one of ours. */
constexpr std::optional<std::uint32_t> UniformBlockBinding(std::string_view blockName) {
    if (blockName == "FrameUBO") {
        return 0; // FrameUniformBuffer::kBinding
    }
    if (blockName == "MagnetosphereUBO") {
        return 1; // SpaceWeatherUniformBuffer::kBinding
    }
    return std::nullopt;
}

} // namespace rhi::wgsl

#endif // SOLARSYSTEM_GFX_RHI_WGSLBINDINGS_H
