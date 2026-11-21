#version 300 es

// Observe-mode constellation lines: J2000 equatorial unit vectors, carried into the horizon
// frame and refracted exactly as observeSprite.vs places the stars, so a line ends on its star.
layout (location = 0) in vec3 aDir;

uniform mat3 viewRot;
uniform mat4 projection;
uniform mat3 starRotation;  // equatorial J2000 → horizon frame

#include "common/refraction.glsl"

out float vUp;

void main() {
    vec3 d = RefractDirection(normalize(starRotation * aDir));
    vUp = d.y;
    gl_Position = projection * vec4(viewRot * d, 1.0);
}
