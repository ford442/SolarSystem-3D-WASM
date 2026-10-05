#version 300 es

layout (location = 0) in vec3 aPos;

out vec3 fPosition;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;
uniform float zCoef; // for log z-buffer (2.0 / log2(farPlane + 1.0)) [логарифмический z-буфер]

#include "common/log_depth.glsl"

void main() {
    fPosition = aPos;
    gl_Position = projection * view * model * vec4(aPos, 1.0f);

    gl_Position = ApplyLogDepth(gl_Position, zCoef);
}