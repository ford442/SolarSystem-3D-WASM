#version 300 es
// One terrain tile (docs/ARCHITECTURE.md §11.3): a 65×65 grid of height samples plus skirts,
// drawn in place of the body's sphere close up. src/render/terrain/CubeSphere.cpp
// (GridOffset) is the C++ mirror of the placement below, held to the millimetre by
// tests/test_terrain_quadtree.cpp, and TerrainQuadtree.cpp of the morph.
//
// A vertex is an offset from the tile's centre computed small-to-small in float (a radius
// times a direction would round to 0.1 m on the Moon), added to the tile's render-frame
// centre. Its height morphs from the tile's own samples to the parent's surface across the
// tile's morph zone, so a split or merge never pops.
// NEAR_SURFACE: depth per fragment (log_depth.glsl), for a tile close to the eye.
// SHADOW_PASS: into the shadow map, with shadowMap.fs.
// permutations: NEAR_SURFACE SHADOW_PASS

layout (location = 0) in vec3 aGrid; // grid x, y in 0..64; z = 1 on a skirt vertex

uniform mat4 model;            // the body's; its 3×3 takes model offsets to the render frame
uniform mat4 lightSpaceMatrix;
uniform mat3 tileBasis;        // the face's n, u, v
uniform vec4 tileWarp;         // xy: float tan(π/4 · centre s, t); zw: what float rounding lost
uniform vec4 tileTrig;         // xy: cos(π/4 · centre s, t); zw: sin
uniform vec4 tileShape;        // x: tile size in face coordinates; y: mesh radius; z: model units per metre; w: skirt depth (model)
uniform vec3 tileOrigin;       // the tile's centre point, render frame
uniform vec2 tileMorph;        // morph zone, distance from the eye (world): own heights before x, the parent's past y
uniform vec2 tileParentOffset; // this tile's quadrant in its parent's grid: 0 or 32 on each axis

layout(binding = TU_TERRAIN_HEIGHT) uniform highp sampler2D heightTile; // R32F metres, apron included
layout(binding = TU_TERRAIN_PARENT) uniform highp sampler2D parentTile;

#ifndef SHADOW_PASS
out vec3 vFragPos;           // render frame
out vec3 vModelDirection;    // from the body's centre, model space: texture coordinates and the surface frame
out vec3 vModelNormal;       // the relief's normal, model space
out vec4 vFragPosLightSpace;
#endif
#if defined(NEAR_SURFACE) && !defined(SHADOW_PASS)
out float vLogDepth;
#endif

#include "common/frame.glsl"
#include "common/log_depth.glsl"

const float kQuarterPi = 0.78539816339744831;
const float kQuads = 64.0;

float Height(highp sampler2D tile, ivec2 g) {
    return texelFetch(tile, g + ivec2(1), 0).r; // one apron sample on each side
}

// sin and cos by their series (CubeSphere.cpp SmallSin/SmallCos): a GPU's sin is only good to
// ~1e-6 absolute, which is all of a deep tile's offset.
vec2 SmallSin(vec2 b) {
    vec2 b2 = b * b;
    return b * (1.0 - b2 / 6.0 * (1.0 - b2 / 20.0 * (1.0 - b2 / 42.0 * (1.0 - b2 / 72.0 * (1.0 - b2 / 110.0)))));
}

vec2 SmallCos(vec2 b) {
    vec2 b2 = b * b;
    return 1.0 - b2 / 2.0 * (1.0 - b2 / 12.0 * (1.0 - b2 / 30.0 * (1.0 - b2 / 56.0 * (1.0 - b2 / 90.0))));
}

// Central differences of a tile's heights at grid point g: metres per grid step.
vec2 Gradient(highp sampler2D tile, ivec2 g) {
    return 0.5 * vec2(Height(tile, g + ivec2(1, 0)) - Height(tile, g - ivec2(1, 0)),
                      Height(tile, g + ivec2(0, 1)) - Height(tile, g - ivec2(0, 1)));
}

void main() {
    vec3 n = tileBasis[0], u = tileBasis[1], v = tileBasis[2];
    ivec2 g = ivec2(aGrid.xy);

    // Where the grid point is on the sphere, relative to the tile's centre point.
    vec2 d = (aGrid.xy / kQuads - 0.5) * tileShape.x;
    vec2 b = kQuarterPi * d;
    vec2 sinB = SmallSin(b);
    vec2 cosB = SmallCos(b);
    vec2 cosAB = tileTrig.xy * cosB - tileTrig.zw * sinB;
    vec2 dw = sinB / (tileTrig.xy * cosAB) + tileWarp.zw;
    vec3 delta = dw.x * u + dw.y * v;
    vec3 reference = n + tileWarp.x * u + tileWarp.y * v;
    float referenceLength2 = dot(reference, reference);
    float referenceLength = sqrt(referenceLength2);
    float lift = 2.0 * dot(reference, delta) + dot(delta, delta);
    float len = sqrt(referenceLength2 + lift);
    float inverseDifference = -lift / (len * referenceLength * (len + referenceLength));
    vec3 directionDifference = delta / len + reference * inverseDifference;
    vec3 direction = (reference + delta) / len;
    vec3 onSphere = tileShape.y * directionDifference;

    mat3 shape = mat3(model);
    float ownHeight = Height(heightTile, g);
    vec3 unmorphed = tileOrigin + shape * (onSphere + ownHeight * tileShape.z * direction);

    // The morph weight from the unmorphed vertex's distance, as TerrainQuadtree::MorphWeight.
    float k = tileMorph.x < tileMorph.y ? clamp((length(unmorphed) - tileMorph.x) / (tileMorph.y - tileMorph.x), 0.0, 1.0) : 0.0;
    float height = ownHeight;
    vec2 gradient = vec2(0.0);
    // The parent's surface under the vertex: on its sample, or halfway along the quad edge
    // (or diagonal, which the index buffer splits along) its triangles have there.
    ivec2 c = g + 2 * ivec2(tileParentOffset);
    ivec2 p0 = c >> 1;
    ivec2 odd = c & 1;
    if (k > 0.0) {
        float parentHeight = 0.5 * (Height(parentTile, p0) + Height(parentTile, p0 + odd));
        height = mix(ownHeight, parentHeight, k);
        // Per child grid step: a parent step is two.
        gradient = k * 0.25 * (Gradient(parentTile, p0) + Gradient(parentTile, p0 + odd));
    }
    if (k < 1.0) {
        gradient += (1.0 - k) * Gradient(heightTile, g);
    }
    float heightModel = height * tileShape.z - aGrid.z * tileShape.w;
    vec3 position = tileOrigin + shape * (onSphere + heightModel * direction);

#ifdef SHADOW_PASS
    gl_Position = lightSpaceMatrix * vec4(position, 1.0);
#else
    // The relief's normal: the grid's two tangents on the sphere, tilted by the slope.
    vec2 w = tileWarp.xy + dw;
    vec2 dWarp = kQuarterPi * (1.0 + w * w) * (tileShape.x / kQuads); // d tan / d grid step
    vec3 alongU = (u - direction * dot(direction, u)) * (dWarp.x / len);
    vec3 alongV = (v - direction * dot(direction, v)) * (dWarp.y / len);
    vec3 tangentU = tileShape.y * alongU + gradient.x * tileShape.z * direction;
    vec3 tangentV = tileShape.y * alongV + gradient.y * tileShape.z * direction;
    vModelNormal = normalize(cross(tangentU, tangentV));
    vModelDirection = direction;
    vFragPos = position;
    vFragPosLightSpace = lightSpaceMatrix * vec4(position, 1.0);
    gl_Position = frame.proj * frame.view * vec4(position, 1.0);
#ifdef NEAR_SURFACE
    vLogDepth = PrepareLogFragDepth(gl_Position);
#else
    gl_Position = ApplyLogDepth(gl_Position);
#endif
#endif
}
