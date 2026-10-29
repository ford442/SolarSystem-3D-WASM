#version 300 es
#include "common/preamble.glsl"

// Observe-mode sky and ground. Works in the horizon frame (+X East, +Y Up, +Z -North).
// Output is display-referred (no HDR pass here).
//
// Two skies:
//  - Physical (uSteps > 0, the presets with enablePbrAtmosphere): Earth's baked LUT
//    atmosphere seen from inside the shell. The planet centre is straight down, so the
//    observer sits at (0, bottomRadius + altitude, 0) km and the view ray is the pixel's
//    direction; the march is the one atmospherePbr.fs uses (common/atmosphere_march.glsl).
//    The Moon's shadow is a caster on every sample, so a total eclipse darkens the air
//    overhead and leaves the distant horizon sunlit.
//  - Gradient (Low): a cheap analytic sky from the Sun's altitude only.

in vec2 vNdc;

uniform mat3 invViewRot;
uniform mat4 invProjection;
uniform vec3 sunDir;   // geometric: the light direction, not where the disc is drawn
uniform float eclipse; // fraction of the Sun's disc covered by the Moon, 0..1

#include "common/raytrace.glsl"
#include "common/eclipse.glsl"
#include "common/phase.glsl"
#include "common/atmosphere_lut.glsl"
#include "common/atmosphere_march.glsl"

uniform int uSteps;    // 0: gradient sky
uniform float uExposure;
uniform AtmosphereParams uAtmosphere;  // km
layout(binding = TU_TRANSMITTANCE_LUT) uniform sampler2D transmittanceLut;
layout(binding = TU_MULTI_SCATTERING_LUT) uniform sampler2D multiScatteringLut;
uniform float observerAltitudeKm;
uniform vec3 moonOffsetKm;     // topocentric Moon centre, horizon frame
uniform float moonRadiusKm;
uniform float sunDistanceKm;
uniform float sunRadiusKm;

out vec4 fragColor;

const float GROUND_ALBEDO = 0.12;

vec3 gObserverKm;

float AtmosphereSunVisibility(vec3 positionKm) {
    // Positions relative to the observer keep the float error at the shadow small.
    vec3 offset = positionKm - gObserverKm;
    return EclipseVisibilityAt(offset, sunDir * sunDistanceKm, moonOffsetKm, moonRadiusKm, sunRadiusKm);
}

// In-scattered light toward the observer along `dir` (horizon frame), exposure not applied.
vec3 PhysicalSky(vec3 dir, out vec3 throughput) {
    vec2 topHit = raySphere(gObserverKm, dir, uAtmosphere.topRadius);
    float tEnd = max(topHit.y, 0.0);
    vec2 groundHit = raySphere(gObserverKm, dir, uAtmosphere.bottomRadius);
    if (groundHit.x <= groundHit.y && groundHit.x > 0.0)
        tEnd = min(tEnd, groundHit.x);
    throughput = vec3(1.0);
    if (tEnd <= 0.0)
        return vec3(0.0);
    return IntegrateAtmosphereRay(uAtmosphere, transmittanceLut, multiScatteringLut, gObserverKm, dir, sunDir,
                                  0.0, tEnd, uSteps, throughput);
}

vec3 Expose(vec3 luminance) {
    return 1.0 - exp(-luminance * uExposure);
}

void main() {
    vec4 viewPos = invProjection * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(invViewRot * normalize(viewPos.xyz / viewPos.w));

    float sunAlt = sunDir.y; // sine of the Sun's altitude
    // Sky light follows the uncovered part of the Sun. Perception is roughly logarithmic, so the
    // sky only visibly darkens past ~90% coverage and then drops away quickly into totality.
    float sunlight = pow(1.0 - clamp(eclipse, 0.0, 1.0), 0.45);
    float day = smoothstep(-0.12, 0.20, sunAlt) * sunlight;
    float twilight = smoothstep(-0.30, -0.02, sunAlt) * (1.0 - smoothstep(-0.02, 0.15, sunAlt));
    float up = clamp(dir.y, 0.0, 1.0);
    float skyMask = smoothstep(-0.004, 0.004, dir.y);

    // The night sky (airglow and starlight, ~magnitude 21-22 per square arcsecond) is the floor
    // both skies sit on; the sunlit air is added over it.
    vec3 nightZenith = vec3(0.002, 0.003, 0.010);
    vec3 nightHorizon = vec3(0.022, 0.028, 0.055);
    vec3 night = mix(nightHorizon, nightZenith, pow(up, 0.45));

    if (uSteps > 0) {
        gObserverKm = vec3(0.0, uAtmosphere.bottomRadius + max(observerAltitudeKm, 0.002), 0.0);
        vec3 throughput;
        // Below the horizon the ray would hit the ground a few metres away; use the sky just
        // above it for the haze the distant ground sits in.
        vec3 skyDir = dir.y < 0.002 ? normalize(vec3(dir.x, 0.002, dir.z)) : dir;
        vec3 skyLuminance = PhysicalSky(skyDir, throughput);
        vec3 sky = night + Expose(skyLuminance);

        // Ground: a flat Lambertian plane under the sunlight that reaches it plus sky light, for
        // which the horizon luminance at this azimuth stands in (half the hemisphere's worth:
        // the zenith is darker, the Sun's side brighter). It sits in the same haze as the sky.
        float r = uAtmosphere.bottomRadius + max(observerAltitudeKm, 0.002);
        vec3 sunAtGround = RayIntersectsGround(uAtmosphere, r, sunAlt)
            ? vec3(0.0)
            : texture(transmittanceLut, TransmittanceLutUv(uAtmosphere, r, sunAlt)).rgb;
        sunAtGround *= AtmosphereSunVisibility(gObserverKm);
        vec3 groundRadiance = GROUND_ALBEDO * (sunAtGround * max(sunAlt, 0.0) / 3.14159265 + 0.5 * skyLuminance);
        vec3 ground = vec3(0.30, 0.33, 0.28) * 0.012 + Expose(groundRadiance);
        ground = mix(ground, sky, pow(1.0 + min(dir.y, 0.0) * 6.0, 4.0) * step(dir.y, 0.0) * 0.6);

        fragColor = vec4(mix(ground, sky, skyMask), 1.0);
        return;
    }

    // Brighter and warmer on the Sun's side of the horizon during twilight.
    vec2 viewAz = normalize(dir.xz + vec2(1e-4));
    vec2 sunAz = normalize(sunDir.xz + vec2(1e-4));
    float sunSide = pow(max(dot(viewAz, sunAz), 0.0), 2.0);

    vec3 zenith = mix(nightZenith, vec3(0.10, 0.27, 0.62), day);
    vec3 horizon = mix(nightHorizon, vec3(0.60, 0.74, 0.92), day);
    zenith = mix(zenith, vec3(0.05, 0.08, 0.22), twilight * 0.6);
    horizon = mix(horizon, vec3(0.95, 0.50, 0.28), twilight * (0.2 + 0.8 * sunSide));

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

    fragColor = vec4(mix(ground, sky, skyMask), 1.0);
}
