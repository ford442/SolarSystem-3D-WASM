#version 300 es

//
// Volumetric corona: an instanced stack of camera-facing slices through the Sun.
//
// Slice i sits at depth d_i along sliceForward (the camera->Sun axis, or the view direction
// while the camera is inside the corona), cut to the disc where that plane meets the
// corona's bounding sphere. Depths span only the part of the sphere in front of the camera.
// Each slice samples the 3D density at its own points (starCoronaVolume.fs), so the stack
// is a coarse ray-march done by the rasterizer: the streamers have real depth and
// parallax, unlike the flat billboard (starCorona.*).
// WebGL 2 instancing only — no geometry shader. Distances below are in solar radii.
//

layout (location = 0) in vec2 aPos; // Star's unit glow quad, [-1, 1]^2

uniform mat4 projection;
uniform mat4 view;
uniform float zCoef;

uniform vec3 sunCenter;      // world
uniform float sunRadius;     // scene units per solar radius
uniform vec3 sliceForward;   // slice normal, pointing away from the camera
uniform vec3 sliceRight;
uniform vec3 sliceUp;
uniform float coronaExtent;  // bounding radius, solar radii
uniform int sliceCount;
uniform float sliceDepthMin; // along sliceForward from the Sun's centre
uniform float sliceDepthMax;

out vec3 fLocal; // position relative to the Sun's centre, solar radii

#include "common/log_depth.glsl"

void main() {
    float depth = mix(sliceDepthMin, sliceDepthMax, (float(gl_InstanceID) + 0.5) / float(sliceCount));
    float halfSize = sqrt(max(coronaExtent * coronaExtent - depth * depth, 0.0));
    fLocal = sliceForward * depth + (sliceRight * aPos.x + sliceUp * aPos.y) * halfSize;

    vec3 world = sunCenter + fLocal * sunRadius;
    gl_Position = ApplyLogDepth(projection * view * vec4(world, 1.0), zCoef);
}
