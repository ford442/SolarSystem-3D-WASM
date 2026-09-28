#ifndef SOLARSYSTEM_CATALOGMATERIAL_H
#define SOLARSYSTEM_CATALOGMATERIAL_H
#include "BodyCatalog.generated.h"

class Shader;

/**
 * The planetLighting material a catalog row asks for, and the one place that binds it.
 *
 * CatalogBody and CatalogSatellite both draw through BindMaterial, so every draw sets the
 * full flag block — a moon never inherits the previous planet's hasClouds/hasNightTexture —
 * and a new rocky body needs only a catalog row, not a Render() of its own.
 *
 * The flags come from render.shaderFlags alone; generate-planet-metadata.mjs rejects a row
 * whose flags disagree with its lod ids, so the two cannot drift.
 */
namespace CatalogMaterial {

// Fixed planetLighting sampler units. 6 (shadowMap) and 7 (ringDiffuse) are bound by
// SceneRenderer per pass and must stay clear of these.
constexpr int kDiffuseUnit = 0;
constexpr int kNormalUnit = 1;
constexpr int kSpecularUnit = 2;
constexpr int kNightUnit = 3;
constexpr int kCloudUnit = 4;

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

/** Set every planetLighting material uniform and bind the maps to their fixed units. */
void BindMaterial(const Shader& shader, const Material& material, const Textures& textures);

} // namespace CatalogMaterial

#endif //SOLARSYSTEM_CATALOGMATERIAL_H
