#version 300 es
precision highp float;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aTangent;
layout (location = 2) in float aArcLength;
layout (location = 3) in float aLineUV;

uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform float ribbonWidth;
uniform float zCoef;

out float vArcLength;
out float vEdgeFade;

void main() {
    vec3 worldPos = aPosition;
    vec3 worldTangent = aTangent;
    float tLen = length(worldTangent);
    worldTangent = (tLen > 1e-5) ? worldTangent / tLen : vec3(0.0, 1.0, 0.0);

    vec3 viewDir = cameraPos - worldPos;
    float viewLen = length(viewDir);
    viewDir = (viewLen > 1e-5) ? viewDir / viewLen : vec3(0.0, 0.0, 1.0);

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

    float distScale = clamp(viewLen / 80.0, 1.0, 14.0);
    worldPos += sideDir * aLineUV * ribbonWidth * distScale;
    worldPos += viewDir * 0.04;

    vArcLength = aArcLength;
    vEdgeFade = 1.0 - abs(aLineUV);

    gl_Position = projection * view * vec4(worldPos, 1.0);
    gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * zCoef - 1.0;
    gl_Position.z *= gl_Position.w;
}
