#version 300 es

//
// Atmospheric scattering vertex shader
//
// Author: Sean O'Neil
//
// Copyright (c) 2004 Sean O'Neil
//

layout (location = 0) in vec3 aPos;

out vec3 fWorldPosition;
out vec3 fPosition;
out mat3 modelMat3;
out vec4 fragPosLightSpace;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;


#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    fWorldPosition = vec3(model * vec4(aPos, 1.0));
    fPosition = aPos;
    modelMat3 = mat3(model);
    fragPosLightSpace = lightSpaceMatrix * vec4(fWorldPosition, 1.0);

    gl_Position = frame.proj * frame.view * vec4(fWorldPosition, 1);

    gl_Position = ApplyLogDepth(gl_Position, frame.zCoef);
}