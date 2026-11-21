#version 300 es

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in mat4 aInstanceMatrix;
layout (location = 6) in vec4 aInstanceColor;

out vec3 vFragPos;
out vec3 vNormal;
out vec3 vColor;

#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    vec4 worldPos = aInstanceMatrix * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;
    vNormal = mat3(aInstanceMatrix) * aNormal;
    vColor = aInstanceColor.rgb;

    gl_Position = frame.proj * frame.view * worldPos;
    gl_Position = ApplyLogDepth(gl_Position);
}
