#version 300 es

//
// Atmospheric scattering vertex shader
//
// Author: Sean O'Neil
//
// Copyright (c) 2004 Sean O'Neil
//

// NEAR_SURFACE: the camera is in or near the shell, whose triangles then cross the eye
// plane; depth is written per fragment (log_depth.glsl).
// permutations: NEAR_SURFACE

layout (location = 0) in vec3 aPos;

out vec3 fWorldPosition;
out vec3 fPosition;
out mat3 modelMat3;
out vec4 fragPosLightSpace;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;
#ifdef NEAR_SURFACE
out float vLogDepth;
#endif


#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    fWorldPosition = vec3(model * vec4(aPos, 1.0));
    fPosition = aPos;
    modelMat3 = mat3(model);
    fragPosLightSpace = lightSpaceMatrix * vec4(fWorldPosition, 1.0);

    gl_Position = frame.proj * frame.view * vec4(fWorldPosition, 1);

#ifdef NEAR_SURFACE
    vLogDepth = PrepareLogFragDepth(gl_Position);
#else
    gl_Position = ApplyLogDepth(gl_Position);
#endif
}