#version 300 es
// NEAR_SURFACE: the body is close to the eye (Renderer::NearSurface). Vertices are placed
// relative to the eye (gfx/MeshAnchor.h) and depth is written per fragment (log_depth.glsl).
// permutations: NEAR_SURFACE

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out vec3 vFragPos;
out vec2 vTexCoords;
out vec3 vTangentLightPos;
out vec3 vTangentViewPos;
out vec3 vTangentFragPos;
out vec4 vFragPosLightSpace;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
#ifdef NEAR_SURFACE
uniform vec3 meshAnchor;       // the eye in model space
uniform vec3 meshAnchorOffset; // render-frame position of meshAnchor
out float vLogDepth;
#endif


#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
#ifdef NEAR_SURFACE
    vFragPos = mat3(model) * (aPos - meshAnchor) + meshAnchorOffset;
#else
    vFragPos = vec3(model * vec4(aPos, 1.0));
#endif
    vTexCoords = aTexCoords;

    mat3 normalMatrix = mat3(transpose(inverse(model)));
    vec3 T = normalize(normalMatrix * aTangent);
    vec3 N = normalize(normalMatrix * aNormal);
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);

    mat3 TBN = transpose(mat3(T, B, N));

    vTangentLightPos = TBN * frame.sunPos.xyz;
    vTangentViewPos  = TBN * frame.cameraPos.xyz;
    vTangentFragPos  = TBN * vFragPos;
    vFragPosLightSpace = lightSpaceMatrix * vec4(vFragPos, 1.0);

    gl_Position = frame.proj * frame.view * vec4(vFragPos, 1.0f);

#ifdef NEAR_SURFACE
    vLogDepth = PrepareLogFragDepth(gl_Position);
#else
    gl_Position = ApplyLogDepth(gl_Position);
#endif
}