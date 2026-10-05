#version 300 es

layout (location = 0) in vec3 aPos;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

uniform vec3 center;
uniform vec3 cameraRight;
uniform vec3 cameraUp;

uniform float maxSize;
uniform float starRadius;
uniform float zCoef; // for log z-buffer (2.0 / log2(farPlane + 1.0)) [логарифмический z-буфер]

// Output
out vec3 fPosition;

#include "common/log_depth.glsl"

void main() {
    fPosition = (cameraRight * aPos.x + cameraUp * aPos.y);
    vec3 vpw = fPosition * maxSize;
    gl_Position = projection * view * model * vec4(vpw, 1.0);

    gl_Position = ApplyLogDepth(gl_Position, zCoef);
}