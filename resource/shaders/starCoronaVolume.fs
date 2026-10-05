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
uniform float nearMargin;    // nearest slice depth past the camera, solar radii
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
    bool hitsDisc = photosphere.x <= photosphere.y && photosphere.x > 0.0;
    if (hitsDisc && photosphere.x < pointDistance)
        discard;
    // In front of the disc the corona is ~1e-6 of the photosphere and invisible; letting it
    // add here would only wash out the limb darkening.
    float overDisc = hitsDisc ? 0.1 : 1.0;

    // Baumbach (1937) K-corona electron density, with the r^-1.5 term (the outer corona,
    // where streamers live) raised 4x over the published fit: physically it is ~1e-4 of the
    // limb brightness, which an 8-bit display cannot show next to the photosphere.
    float density = 0.15 * pow(r, -1.5) + 1.55 * pow(r, -6.0) + 2.99 * pow(r, -16.0);

    // Streamers: noise on the direction only (plus a slow drift along r), so features stay
    // radial and read as rays; strongest toward the solar equator (world XZ plane).
    vec3 direction = fLocal / r;
    float rays = noise(vec4(direction * 5.0, r * 0.08 + time), 3, 1.0, 0.5);
    float streamer = 0.25 + 2.2 * pow(clamp(0.5 + 0.5 * rays, 0.0, 1.0), 4.0);
    float equator = 1.0 - 0.6 * direction.y * direction.y;
    float structure = mix(1.0, streamer * equator, smoothstep(1.02, 1.4, r));

    float edgeFade = (1.0 - smoothstep(0.6 * coronaExtent, coronaExtent, r)) * smoothstep(1.0, 1.01, r);

    // Path length of the view ray through this slab. Off-axis rays cross the slices
    // obliquely; the floor only guards the outside view's widest angles.
    float pathLength = sliceSpacing / max(abs(dot(rayDir, sliceForward)), 0.25);
    // Inside the corona the nearest slices sweep past the camera; fade them in rather
    // than let them pop.
    float nearFade = smoothstep(nearMargin, 2.0 * nearMargin, pointDistance);

    float emission = density * structure * edgeFade * pathLength * nearFade * coronaIntensity * overDisc;
    fragColor = vec4(coronaColor * emission, 1.0);
}
