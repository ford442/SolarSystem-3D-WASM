#version 300 es

layout (location = 0) in vec2 aPos;

uniform mat4 projection;
uniform mat4 view;

uniform vec3 center;
uniform vec2 dims;

uniform bool isPlanetaryRingInView;

uniform vec3 cameraPosition;
uniform vec3 ringCenter; // Center of disk in eye space
uniform vec3 ringNormal; // Disk plane normal in eye space
uniform vec2 ringInnerOuterRadiuses; // x = Inner, y = Outer
uniform sampler2D ringDiffuse; // Ring diffuse map

out vec2 fPosition;
out vec3 ringTint;

#include "common/raytrace.glsl"
#include "common/ring.glsl"

void main() {
    fPosition = aPos;
    gl_Position = projection * view * vec4(center, 1.0f);
    gl_Position /= gl_Position.w;

    vec2 correctDims = dims;
    ringTint = vec3(1.0);

    if (isPlanetaryRingInView) {
        float u;
        if (RingCrossing(cameraPosition, normalize(center - cameraPosition), center - ringCenter,
                         ringCenter, ringNormal, ringInnerOuterRadiuses, u)) {
            vec4 ringColor = texture(ringDiffuse, vec2(u, 0.0));
            correctDims *= 1.0 - ringColor.a * 0.65;
            ringTint = ringColor.rgb;
        }
    }

    gl_Position.xy += aPos * correctDims; // Move the vertex in screen space [Перемещение вершины в экранное пространство]
}