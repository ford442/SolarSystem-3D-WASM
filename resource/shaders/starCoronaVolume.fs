#version 300 es

//
// Density of the K-corona at one slice point (see starCoronaVolume.vs). Additive, so the
// slices need no sorting; each adds density x the ray's path length through its slab.
//

in vec3 fLocal;

out vec4 fragColor;

#include "common/raytrace.glsl"
#include "common/noise_simplex4d.glsl"

uniform vec3 cameraLocal;    // camera relative to the Sun's centre, solar radii
uniform vec3 sliceForward;
uniform float sliceSpacing;  // distance between slices, solar radii
uniform float coronaExtent;
uniform vec3 coronaColor;
uniform float coronaIntensity;
uniform float time;

void main() {
    float r = length(fLocal);
    if (r < 1.0 || r > coronaExtent)
        discard;

    // Behind the photosphere from this eye: hidden by the disc.
    vec3 toPoint = fLocal - cameraLocal;
    float pointDistance = length(toPoint);
    vec3 rayDir = toPoint / pointDistance;
    vec2 photosphere = raySphere(cameraLocal, rayDir, 1.0);
    if (photosphere.x <= photosphere.y && photosphere.x > 0.0 && photosphere.x < pointDistance)
        discard;

    // Baumbach (1937) electron density, relative units: the K-corona's steep fall-off.
    float density = 0.036 * pow(r, -1.5) + 1.55 * pow(r, -6.0) + 2.99 * pow(r, -16.0);

    // Streamers: radial structure from noise on the direction (stretched along r so it
    // reads as rays), slowly evolving, strongest toward the solar equator (world XZ).
    vec3 direction = fLocal / r;
    float rays = noise(vec4(direction * 3.0, r * 0.15 + time), 3, 1.0, 0.55);
    float streamer = 0.35 + 1.6 * pow(clamp(0.5 + 0.5 * rays, 0.0, 1.0), 3.0);
    float equator = 1.0 - 0.55 * direction.y * direction.y;
    float structure = mix(1.0, streamer * equator, smoothstep(1.02, 1.5, r));

    float edgeFade = (1.0 - smoothstep(0.6 * coronaExtent, coronaExtent, r)) * smoothstep(1.0, 1.01, r);

    // Path length of the view ray through this slab (slices are perpendicular to the
    // camera->Sun axis, so off-axis rays cross them obliquely).
    float pathLength = sliceSpacing / max(abs(dot(rayDir, sliceForward)), 0.25);

    float emission = density * structure * edgeFade * pathLength * coronaIntensity;
    fragColor = vec4(coronaColor * emission, 1.0);
}
