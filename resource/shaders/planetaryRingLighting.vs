#version 300 es

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

uniform mat4 model;
uniform mat4 lightSpaceMatrix;



out vec3 fPosition;
out vec3 normal;
out vec4 fragPosLightSpace;
out vec2 texCoords;
out vec3 fWorldPosition;
out float fLogZ;

out vec3 vEyePos;
out vec3 vLight0Pos;

#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    fWorldPosition = vec3(model * vec4(aPos, 1.0));
    fPosition = vec3(aPos.x, 0.0, aPos.z);

    vEyePos = frame.cameraPos.xyz - fWorldPosition.xyz;
    vLight0Pos = frame.sunPos.xyz - fWorldPosition.xyz;

    mat3 normalMatrix = mat3(transpose(inverse(model)));
    normal = normalize(normalMatrix * aNormal);

    fragPosLightSpace = lightSpaceMatrix * model * vec4(aPos, 1.0);
    texCoords = aTexCoords;
    gl_Position = frame.proj * frame.view * model * vec4(aPos.x, 0.0, aPos.z, 1.0);

    gl_Position = ApplyLogDepth(gl_Position);

    fLogZ = LogDepthArgument(gl_Position.w);
}