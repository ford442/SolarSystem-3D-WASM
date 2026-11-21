#ifndef SOLARSYSTEM_GFX_TEXTUREUNITS_H
#define SOLARSYSTEM_GFX_TEXTUREUNITS_H
#include <optional>
#include <string_view>

/**
 * The texture unit every sampler is bound to, shared by C++ and GLSL. This X-list is the
 * source: scripts/generate-texture-units.mjs writes resource/shaders/common/texture_units.glsl
 * from it (`TU_*` defines, pulled in by the preamble), and a shader names its unit with
 *
 *     layout(binding = TU_SHADOW_MAP) uniform sampler2D shadowMap;
 *
 * GLSL ES 3.00 has no sampler `binding`, so ShaderSource strips the qualifier and records it;
 * Shader assigns the unit once at link. C++ binds the texture with
 * glBindTextureUnit(Unit(TextureUnit::ShadowMap), ...). No per-draw SetInt for samplers.
 *
 * WebGL 2 guarantees 16 fragment units; keep every value below that.
 */
#define SOLARSYSTEM_TEXTURE_UNITS(X)                                                          \
    X(Diffuse, 0, TU_DIFFUSE)            /* surface albedo; also any program's single image */ \
    X(Normal, 1, TU_NORMAL)                                                                    \
    X(Specular, 2, TU_SPECULAR)                                                                \
    X(Night, 3, TU_NIGHT)                                                                      \
    X(Clouds, 4, TU_CLOUDS)              /* cloud map a surface reads for fake cloud shadows */ \
    X(ShadowMap, 5, TU_SHADOW_MAP)       /* the frame's shadow map, bound once per view */      \
    X(Ring, 6, TU_RING)                  /* a nearby ring's texture, for ring shadows/occlusion */ \
    X(TransmittanceLut, 7, TU_TRANSMITTANCE_LUT)                                               \
    X(MultiScatteringLut, 8, TU_MULTI_SCATTERING_LUT)                                          \
    X(Moon, 9, TU_MOON)                  /* Observe's Moon disc */                              \
    X(TerrainHeight, 10, TU_TERRAIN_HEIGHT) /* a terrain tile's heights, read by terrain.vs */  \
    X(TerrainParent, 11, TU_TERRAIN_PARENT) /* its parent's, which it morphs to */

enum class TextureUnit : int {
#define SOLARSYSTEM_TEXTURE_UNIT_ENUM(name, value, glsl) name = value,
    SOLARSYSTEM_TEXTURE_UNITS(SOLARSYSTEM_TEXTURE_UNIT_ENUM)
#undef SOLARSYSTEM_TEXTURE_UNIT_ENUM
};

constexpr unsigned int Unit(TextureUnit unit) { return static_cast<unsigned int>(unit); }

/** `TU_SHADOW_MAP` → ShadowMap; what ShaderSource resolves a `binding =` name with. */
inline std::optional<TextureUnit> TextureUnitFromGlslName(std::string_view name) {
#define SOLARSYSTEM_TEXTURE_UNIT_LOOKUP(unitName, value, glsl) \
    if (name == #glsl) return TextureUnit::unitName;
    SOLARSYSTEM_TEXTURE_UNITS(SOLARSYSTEM_TEXTURE_UNIT_LOOKUP)
#undef SOLARSYSTEM_TEXTURE_UNIT_LOOKUP
    return std::nullopt;
}

#endif // SOLARSYSTEM_GFX_TEXTUREUNITS_H
