#version 300 es
precision highp float;

layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec3 aTangent;
layout (location = 2) in float aArcLength;
layout (location = 3) in float aSide;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform float ribbonWidth;
uniform float zCoef;

out float vArcLength;
out float vSide;

void main() {
    vec3 worldPos = (model * vec4(aPosition, 1.0)).xyz;
    vec3 worldTangent = normalize(mat3(model) * aTangent);
    vec3 viewDir = cameraPos - worldPos;
    float viewLen = length(viewDir);
    viewDir = (viewLen > 1e-5) ? viewDir / viewLen : vec3(0.0, 0.0, 1.0);

    vec3 sideDir = cross(worldTangent, viewDir);
    float sideLen = length(sideDir);
    if (sideLen < 1e-5) {
        sideDir = normalize(cross(worldTangent, vec3(0.0, 1.0, 0.0)));
    } else {
        sideDir /= sideLen;
    }

    // ribbonWidth is in local body-radius units; model already scales the body.
    worldPos += sideDir * aSide * ribbonWidth * max(length(mat3(model)[0]), 0.25);

    vArcLength = aArcLength;
    vSide = aSide;

    gl_Position = projection * view * vec4(worldPos, 1.0);
    gl_Position.z = log2(max(1e-6, gl_Position.w + 1.0)) * zCoef - 1.0;
    gl_Position.z *= gl_Position.w;
}
