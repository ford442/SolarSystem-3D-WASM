#ifndef SOLARSYSTEM_RENDER_PASSES_PASSCOMMON_H
#define SOLARSYSTEM_RENDER_PASSES_PASSCOMMON_H
#include "ApplicationTypes.h"
#include "Solar_System/CatalogMaterial.h"
#include "gfx/ShaderLibrary.h"
#include <string>
#include <utility>
#include <vector>

/**
 * What the Renderer pass files (src/render/passes/) share: program descriptions and the
 * shadow-test biases. Private to those files; nothing outside src/render/passes/ includes it.
 */
namespace RenderPasses {

// Shadow-test biases in scene units along the light. Each pass divides by its component's
// depth range (ShadowBias). These are the values the shaders always had: they used to be
// fractions of the camera's 0.001–20000 range, which the shadow frustum borrowed.
constexpr float kSurfaceShadowBias = 10.0f;
constexpr float kShellShadowBias = 20.0f; // atmospheres, clouds, rings

inline float ShadowBias(const RenderableSceneComponent& component, float sceneUnits) {
    return sceneUnits / component.shadowDepthRange.y;
}

inline ShaderSpec Program(const char* vertex, const char* fragment, std::vector<std::string> defines = {}) {
    return ShaderSpec(std::string("resource/shaders/") + vertex, std::string("resource/shaders/") + fragment,
                      std::move(defines));
}

inline ShaderSpec PlanetSpec(unsigned variant) {
    return Program("planetLighting.vs", "planetLighting.fs", CatalogMaterial::Defines(variant));
}

// The same material permutations on the terrain tiles (terrain.fs shares planet_surface.glsl).
inline ShaderSpec TerrainSpec(unsigned variant) {
    return Program("terrain.vs", "terrain.fs", CatalogMaterial::Defines(variant));
}

inline const std::vector<std::string>& NearSurfaceDefine() {
    static const std::vector<std::string> define{"NEAR_SURFACE"};
    return define;
}

} // namespace RenderPasses

#endif // SOLARSYSTEM_RENDER_PASSES_PASSCOMMON_H
