#ifndef SOLARSYSTEM_CATALOGMATERIAL_H
#define SOLARSYSTEM_CATALOGMATERIAL_H
#include "BodyCatalog.generated.h"
#include <string>
#include <vector>

class Shader;

/**
 * The planetLighting material a catalog row asks for, and the one place that binds it.
 *
 * The flags are not uniforms: they pick a permutation of planetLighting.fs (`#define`s, see
 * Variant()/Defines()), so a body without a night map never runs that branch, and a moon
 * cannot inherit the previous planet's clouds. The Renderer asks each body for its
 * ShaderVariant() before it draws and hands it that program; BindMaterial then binds the
 * maps. A new rocky body needs only a catalog row, not a Render() of its own.
 *
 * The flags come from render.shaderFlags alone; generate-planet-metadata.mjs rejects a row
 * whose flags disagree with its lod ids, so the two cannot drift.
 */
namespace CatalogMaterial {

// Sampler units are TextureUnit (gfx/TextureUnits.h): Diffuse/Normal/Specular/Night/Clouds
// here, ShadowMap and Ring bound by the Renderer per view and per component.

struct Material {
    bool hasNight;
    bool hasSpecular;
    bool hasClouds;
    bool useSphereIntersect;
    float ambientFactor;

    static Material FromEntry(const BodyCatalog::Entry& entry) {
        return {entry.shaderFlags.hasNightTexture, entry.shaderFlags.hasSpecularMap,
                entry.shaderFlags.hasClouds, entry.useSphereIntersect, entry.ambientFactor};
    }
};

/** GL texture names for one draw; 0 for a map the material does not use. */
struct Textures {
    unsigned int diffuse = 0;
    unsigned int normal = 0;
    unsigned int specular = 0;
    unsigned int night = 0;
    unsigned int clouds = 0;
};

/** Permutation bits of planetLighting.fs; Defines() names them. */
enum VariantBit : unsigned {
    kNightTexture = 1u << 0,    // HAS_NIGHT_TEXTURE
    kSpecularMap = 1u << 1,     // HAS_SPECULAR_MAP
    kClouds = 1u << 2,          // HAS_CLOUDS
    kSphereIntersect = 1u << 3, // USE_SPHERE_INTERSECT
};
constexpr unsigned kVariantCount = 1u << 4;

/**
 * The permutation this draw needs. A map flag only counts when its texture is resident;
 * the shader would otherwise sample whatever the previous draw left on that unit.
 */
unsigned Variant(const Material& material, const Textures& textures);
/** Every variant a row can draw with: its full mask and each subset of its map bits (maps load late). */
std::vector<unsigned> PossibleVariants(const Material& material);
/** The planetLighting.fs `#define`s of `variant`, in the order the shader's `// permutations:` line lists them. */
std::vector<std::string> Defines(unsigned variant);

/** Set the material's uniforms and bind its maps to their fixed units (TextureUnit). */
void BindMaterial(const Shader& shader, const Material& material, const Textures& textures);

} // namespace CatalogMaterial

#endif //SOLARSYSTEM_CATALOGMATERIAL_H
