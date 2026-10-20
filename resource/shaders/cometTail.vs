#version 300 es

layout (location = 0) in vec2 aCorner;
layout (location = 1) in vec2 aUV;

out vec2 vUV;
out float vFade;

uniform vec3 nucleusPos;
uniform vec3 cameraRight;
uniform float tailLength;
uniform float tailWidth;

#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    vec3 antiSun = nucleusPos - frame.sunPos.xyz;
    float antiLen = length(antiSun);
    antiSun = (antiLen > 1e-4) ? antiSun / antiLen : vec3(0.0, 0.0, 1.0);

    // Keep the tail billboard readable: width along a screen-stable side axis.
    vec3 side = cameraRight - antiSun * dot(cameraRight, antiSun);
    float sideLen = length(side);
    side = (sideLen > 1e-4) ? side / sideLen : normalize(cross(antiSun, vec3(0.0, 1.0, 0.0)));

    vec3 world = nucleusPos
               + antiSun * (aUV.y * tailLength)
               + side * (aCorner.x * tailWidth);

    vUV = aUV;
    vFade = 1.0 - aUV.y;

    gl_Position = frame.proj * frame.view * vec4(world, 1.0);
    gl_Position = ApplyLogDepth(gl_Position, frame.zCoef);
}
