#version 300 es
// Material switches: CatalogMaterial::Defines() picks them per draw from the row's
// render.shaderFlags and which maps are resident (ShaderLibrary caches each combination).
// NEAR_SURFACE (planetLighting.vs) writes depth per fragment. The material itself is
// common/planet_surface.glsl, which terrain.fs shares.
// permutations: HAS_NIGHT_TEXTURE HAS_SPECULAR_MAP HAS_CLOUDS USE_SPHERE_INTERSECT NEAR_SURFACE
#ifdef NEAR_SURFACE
in float vLogDepth;
#endif
in vec3 vFragPos;
in vec2 vTexCoords;
in vec3 vTangentLightPos;
in vec3 vTangentViewPos;
in vec3 vTangentFragPos;
in vec4 vFragPosLightSpace;

out vec4 fragColor;

vec4 SurfaceTexture(sampler2D map, vec2 uv) {
    return texture(map, uv);
}

#include "common/frame.glsl"
#include "common/log_depth.glsl"
#include "common/planet_surface.glsl"

void main() {
#ifdef NEAR_SURFACE
    gl_FragDepth = LogFragDepth(vLogDepth); // first: every path that is not discarded writes it
#endif
    vec3 lightDir = normalize(vTangentLightPos - vTangentFragPos);
    vec3 viewDir = normalize(vTangentViewPos - vTangentFragPos);
    vec3 lighting = ShadePlanetSurface(vTexCoords, vec3(0.0, 0.0, 1.0), lightDir, viewDir, vFragPos, vFragPosLightSpace);
    fragColor = vec4(lighting, 1.0);
}
