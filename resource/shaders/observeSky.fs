#version 300 es
#include "common/preamble.glsl"

// Observe-mode sky and ground. Works in the horizon frame (+X East, +Y Up, +Z -North): the
// sky colour depends only on the view direction and the Sun's altitude/azimuth, so it is
// cheap and the same on every platform. Output is display-referred (no HDR pass here).

in vec2 vNdc;

uniform mat3 invViewRot;
uniform mat4 invProjection;
uniform vec3 sunDir;
uniform float eclipse; // fraction of the Sun's disc covered by the Moon, 0..1

out vec4 fragColor;

void main() {
    vec4 viewPos = invProjection * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(invViewRot * normalize(viewPos.xyz / viewPos.w));

    float sunAlt = sunDir.y; // sine of the Sun's altitude
    // Sky light follows the uncovered part of the Sun. Perception is roughly logarithmic, so the
    // sky only visibly darkens past ~90% coverage and then drops away quickly into totality.
    float sunlight = pow(1.0 - clamp(eclipse, 0.0, 1.0), 0.45);
    float day = smoothstep(-0.12, 0.20, sunAlt) * sunlight;
    float twilight = smoothstep(-0.30, -0.02, sunAlt) * (1.0 - smoothstep(-0.02, 0.15, sunAlt));

    // Brighter and warmer on the Sun's side of the horizon during twilight.
    vec2 viewAz = normalize(dir.xz + vec2(1e-4));
    vec2 sunAz = normalize(sunDir.xz + vec2(1e-4));
    float sunSide = pow(max(dot(viewAz, sunAz), 0.0), 2.0);

    vec3 zenith = mix(vec3(0.002, 0.003, 0.010), vec3(0.10, 0.27, 0.62), day);
    vec3 horizon = mix(vec3(0.022, 0.028, 0.055), vec3(0.60, 0.74, 0.92), day);
    zenith = mix(zenith, vec3(0.05, 0.08, 0.22), twilight * 0.6);
    horizon = mix(horizon, vec3(0.95, 0.50, 0.28), twilight * (0.2 + 0.8 * sunSide));

    float up = clamp(dir.y, 0.0, 1.0);
    vec3 sky = mix(horizon, zenith, pow(up, 0.45));

    // Glow around the Sun, fading out as it drops below the horizon.
    float cosSun = max(dot(dir, sunDir), 0.0);
    vec3 glow = mix(vec3(1.0, 0.65, 0.35), vec3(1.0, 0.92, 0.75), day);
    sky += glow * pow(cosSun, 24.0) * 0.35 * smoothstep(-0.2, 0.0, sunAlt) * sunlight;

    // In totality the horizon glows with the sunlit sky outside the Moon's shadow, a 360 degree
    // sunset: warm orange, brightest right at the horizon.
    float totality = smoothstep(0.97, 1.0, eclipse);
    sky += vec3(0.95, 0.45, 0.22) * pow(1.0 - up, 6.0) * 0.22 * totality * step(0.0, sunAlt);

    // Ground: a flat dark plane lit by what the sky gives it.
    float groundLight = 0.012 + 0.10 * day + 0.03 * twilight;
    vec3 ground = vec3(0.30, 0.33, 0.28) * groundLight;
    ground = mix(ground, horizon * 0.35, pow(1.0 + min(dir.y, 0.0) * 6.0, 4.0) * step(dir.y, 0.0));

    float skyMask = smoothstep(-0.004, 0.004, dir.y);
    fragColor = vec4(mix(ground, sky, skyMask), 1.0);
}
