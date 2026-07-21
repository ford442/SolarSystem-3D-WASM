#version 300 es
precision highp float;

layout (location = 0) in vec2 aCorner;
layout (location = 1) in vec2 aUV;

out vec2 vUV;
out float vFade;

uniform mat4 projection;
uniform mat4 view;
uniform vec3 nucleusPos;
uniform vec3 lightPos;
uniform vec3 cameraRight;
uniform float tailLength;
uniform float tailWidth;
uniform float zCoef;

void main() {
    vec3 antiSun = nucleusPos - lightPos;
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

    gl_Position = projection * view * vec4(world, 1.0);
    gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * zCoef - 1.0;
    gl_Position.z *= gl_Position.w;
}
