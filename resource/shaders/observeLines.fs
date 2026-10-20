#version 300 es
#include "common/preamble.glsl"

in float vUp;

uniform vec4 lineColor; // alpha already follows how dark the sky is

out vec4 fragColor;

void main() {
    // Hidden by the ground, like the stars.
    fragColor = vec4(lineColor.rgb, lineColor.a * smoothstep(-0.003, 0.003, vUp));
}
