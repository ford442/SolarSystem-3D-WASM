#version 300 es

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aTangent;
layout (location = 2) in float aArcLength;
layout (location = 3) in float aLineUV;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform float ribbonWidth;
// Space weather (docs/ARCHITECTURE.md §13): xyz the unit vector to the Sun, w the standoff
// over its quiet value. Below 1 the wind has pushed the dayside in and drawn the nightside
// out into a tail; 1 (or 0, unset) leaves the traced dipole as it is.
uniform vec4 uCompression;

out float vArcLength;
out float vEdgeFade;

#include "common/log_depth.glsl"

vec3 Compress(vec3 worldPos, vec3 center, float radius) {
    float ratio = uCompression.w > 0.0 ? uCompression.w : 1.0;
    if (abs(ratio - 1.0) < 1e-3) {
        return worldPos;
    }
    vec3 d = worldPos - center;
    float r = length(d) / radius;
    if (r < 1e-4) {
        return worldPos;
    }
    vec3 sunward = uCompression.xyz;
    float cosTheta = dot(d, sunward) / (r * radius);
    // Dayside: squeezed toward the planet by the standoff ratio, more the farther out.
    float day = smoothstep(-0.2, 0.6, cosTheta) * smoothstep(1.3, 3.0, r);
    d *= mix(1.0, ratio, day);
    // Nightside: stretched downwind into a tail.
    float night = smoothstep(0.2, -0.6, cosTheta) * smoothstep(1.5, 3.5, r);
    d -= sunward * (max(-dot(d, sunward), 0.0) * (1.0 / ratio - 1.0) * 1.5 * night);
    return center + d;
}

void main() {
    vec3 worldPos = (model * vec4(aPosition, 1.0)).xyz;
    worldPos = Compress(worldPos, (model * vec4(0.0, 0.0, 0.0, 1.0)).xyz, max(length(mat3(model)[0]), 1e-6));
    vec3 worldTangent = mat3(model) * aTangent;
    float tLen = length(worldTangent);
    worldTangent = (tLen > 1e-5) ? worldTangent / tLen : vec3(0.0, 1.0, 0.0);

    vec3 viewDir = cameraPos - worldPos;
    float viewLen = length(viewDir);
    viewDir = (viewLen > 1e-5) ? viewDir / viewLen : vec3(0.0, 0.0, 1.0);

    // Camera-facing side axis (same idea as cometTail.vs).
    vec3 sideDir = cross(worldTangent, viewDir);
    float sideLen = length(sideDir);
    if (sideLen < 1e-5) {
        sideDir = cross(worldTangent, vec3(0.0, 1.0, 0.0));
        sideLen = length(sideDir);
        if (sideLen < 1e-5) {
            sideDir = cross(worldTangent, vec3(1.0, 0.0, 0.0));
            sideLen = length(sideDir);
        }
    }
    sideDir = (sideLen > 1e-5) ? sideDir / sideLen : vec3(0.0, 0.0, 1.0);

    float bodyScale = max(length(mat3(model)[0]), 0.25);
    // Grow slightly with camera distance so ribbons stay readable at system scale.
    float distScale = clamp(viewLen / max(bodyScale * 6.0, 1.0), 1.0, 10.0);
    worldPos += sideDir * aLineUV * ribbonWidth * bodyScale * distScale;
    // Nudge toward the camera to reduce z-fighting with planet surfaces.
    worldPos += viewDir * (0.02 * bodyScale);

    vArcLength = aArcLength;
    vEdgeFade = 1.0 - abs(aLineUV);

    gl_Position = projection * view * vec4(worldPos, 1.0);
    gl_Position = ApplyLogDepth(gl_Position);
}
