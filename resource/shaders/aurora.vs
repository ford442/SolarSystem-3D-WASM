#version 300 es

// The auroral shell (Solar_System/AuroraShell): a sphere at the top of the emitting layer,
// whose fragments ray-march the layer (aurora.fs). Depth is per fragment, since the camera
// can be just above or inside the shell (Observe, a low orbit).

layout (location = 0) in vec3 aPos;

uniform mat4 model;

out vec3 vWorld;
out float vLogDepth;

#include "common/frame.glsl"
#include "common/log_depth.glsl"

void main() {
    vWorld = vec3(model * vec4(aPos, 1.0));
    gl_Position = frame.viewProj * vec4(vWorld, 1.0);
    vLogDepth = PrepareLogFragDepth(gl_Position);
}
