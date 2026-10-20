#version 300 es

layout (location = 0) in vec3 aPos;

out vec3 fPosition;       // object space (sphere.obj, radius 2)
out vec3 fWorldPosition;
out vec3 fNormal;         // world space

uniform mat4 model;

#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    fPosition = aPos;
    fWorldPosition = vec3(model * vec4(aPos, 1.0));
    fNormal = normalize(mat3(model) * aPos); // a sphere about its centre
    gl_Position = frame.proj * frame.view * vec4(fWorldPosition, 1.0);

    gl_Position = ApplyLogDepth(gl_Position, frame.zCoef);
}
