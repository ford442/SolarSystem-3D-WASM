#version 300 es
// Terrain tiles in the colour pass (docs/ARCHITECTURE.md §11.3): the body's own catalog
// material (common/planet_surface.glsl, the same as planetLighting.fs), with the relief from
// terrain.vs folded into its normal map. Texture coordinates and the surface frame come from
// the model direction per fragment, in the sphere mesh's convention: u = 0.5 on +X rising
// east (longitude -atan2(z, x)), v = 0 at the +Y pole; tangent east, bitangent north.
// permutations: HAS_NIGHT_TEXTURE HAS_SPECULAR_MAP HAS_CLOUDS USE_SPHERE_INTERSECT NEAR_SURFACE
#define SURFACE_RELIEF
#ifdef NEAR_SURFACE
in float vLogDepth;
#endif
in vec3 vFragPos;
in vec3 vModelDirection;
in vec3 vModelNormal;
in vec4 vFragPosLightSpace;

uniform mat3 normalMatrix; // the body's transpose(inverse(mat3(model))), as planetLighting.vs forms it

out vec4 fragColor;

const float kPi = 3.14159265358979323846;

// Screen-space derivatives of u taken across the map's seam would ask for the smallest mip
// along a one-pixel line; each fragment picks whichever of u and u shifted half a turn is
// continuous around it.
vec2 gUvDx;
vec2 gUvDy;

vec4 SurfaceTexture(sampler2D map, vec2 uv) {
    return textureGrad(map, uv, gUvDx, gUvDy);
}

#include "common/frame.glsl"
#include "common/log_depth.glsl"
#include "common/planet_surface.glsl"

void main() {
#ifdef NEAR_SURFACE
    gl_FragDepth = LogFragDepth(vLogDepth); // first: every path that is not discarded writes it
#endif
    vec3 direction = normalize(vModelDirection);
    vec2 uv = vec2(0.5 - atan(direction.z, direction.x) * (0.5 / kPi), 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / kPi);
    float shifted = fract(uv.x + 0.5);
    vec2 du = vec2(dFdx(uv.x), dFdy(uv.x));
    vec2 duShifted = vec2(dFdx(shifted), dFdy(shifted));
    du = abs(du.x) + abs(du.y) <= abs(duShifted.x) + abs(duShifted.y) ? du : duShifted;
    gUvDx = vec2(du.x, dFdx(uv.y));
    gUvDy = vec2(du.y, dFdy(uv.y));

    // The sphere's frame here (east, north, up) in model space, then in the render frame the
    // way planetLighting.vs builds its TBN.
    vec3 east = vec3(direction.z, 0.0, -direction.x); // cross(+Y pole, direction): -Z on +X
    float eastLength = length(east);
    east = eastLength > 1e-7 ? east / eastLength : vec3(0.0, 0.0, -1.0);
    vec3 north = cross(direction, east);
    vec3 relief = vec3(dot(vModelNormal, east), dot(vModelNormal, north), dot(vModelNormal, direction));

    vec3 T = normalize(normalMatrix * east);
    vec3 N = normalize(normalMatrix * direction);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    mat3 toTangent = transpose(mat3(T, B, N));

    vec3 lightDir = normalize(toTangent * (frame.sunPos.xyz - vFragPos));
    vec3 viewDir = normalize(toTangent * (frame.cameraPos.xyz - vFragPos));
    vec3 lighting = ShadePlanetSurface(uv, normalize(relief), lightDir, viewDir, vFragPos, vFragPosLightSpace);
    fragColor = vec4(lighting, 1.0);
}
