#version 300 es

// The photosphere. With neither permutation, the sprite it always was (static noise spots
// and granulation). PHOTOSPHERE (Medium) animates the granulation; SUNSPOTS (Full) adds the
// sunspot cycle's spots (SpaceWeather::EvaluateCycle, docs/ARCHITECTURE.md §13): two
// butterfly wings at ±latitude drifting equatorward over the cycle (Spörer's law), as many
// as the smoothed sunspot number says, carried round by differential rotation.
// permutations: PHOTOSPHERE SUNSPOTS

#include "common/frame.glsl"
#include "common/noise_simplex3d.glsl"

in vec3 fPosition;
in vec3 fWorldPosition;
in vec3 fNormal;
uniform vec3 shiftStarColor;
uniform vec3 colorMult;
layout(binding = TU_DIFFUSE) uniform sampler2D colorMap;
uniform float uColorMap;
uniform float starRadiusInKilometers;
uniform bool isVisible;

#ifdef PHOTOSPHERE
uniform vec4 uSpotBands;    // radians: this cycle's latitude and half-width, the previous cycle's
uniform vec2 uSpotCoverage; // 0–1: this cycle's spots, the previous cycle's tail
uniform float uSpotDays;    // days since the cycle's minimum (rotation, spot evolution)
#endif

out vec4 fragColor;

#if defined(SUNSPOTS) && defined(PHOTOSPHERE)
// Darkening from one wing of the butterfly diagram at this point: 0 clear, down to -0.8 in an
// umbra (~20% of the photosphere's intensity; a penumbra ~70%).
float SpotWing(vec3 n, float latitude, float halfWidth, float coverage, float evolve, float salt) {
    if (coverage <= 0.0) {
        return 0.0;
    }
    float lat = asin(clamp(n.y, -1.0, 1.0));
    float band = exp(-pow((abs(lat) - latitude) / max(halfWidth, 0.02), 2.0));
    if (band < 0.02) {
        return 0.0;
    }
    // Snodgrass & Ulrich (1990): 14.713 − 2.396 sin² − 1.787 sin⁴ degrees a day.
    float s2 = n.y * n.y;
    float spin = radians((14.713 - 2.396 * s2 - 1.787 * s2 * s2) * uSpotDays);
    float lon = atan(n.z, n.x) - spin;
    vec3 p = vec3(cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon));
    // Active regions (a few days to weeks) and the spots in them.
    float region = snoise(p * 7.0 + vec3(salt, evolve * 0.04, -salt));
    float spots = snoise(p * 38.0 + vec3(-salt, salt, evolve * 0.12));
    float threshold = mix(0.92, 0.45, sqrt(coverage));
    float regionMask = smoothstep(threshold, threshold + 0.15, region * band + 0.25 * coverage * band);
    float penumbra = smoothstep(0.15, 0.4, spots) * regionMask;
    float umbra = smoothstep(0.45, 0.65, spots) * regionMask;
    return -(0.3 * penumbra + 0.5 * umbra);
}
#endif

// Limb darkening, power law I(mu)/I(1) = mu^alpha (Hestroffer & Magnan 1998), with alpha
// fitted per channel to the solar continuum at ~680/550/440 nm: the limb is darker and
// redder because the line of sight there ends in cooler, higher photosphere.
const vec3 LIMB_DARKENING_ALPHA = vec3(0.397, 0.503, 0.652);

void main() {
    vec3 position = fPosition;
    float spotFactor = 1.0; // the whole surface's intensity under spots and faculae
#ifdef PHOTOSPHERE
    vec3 unit = normalize(position);
    // Granules (bright cells, dark lanes) churning over minutes, on supergranulation.
    float t = frame.time * 0.03;
    float cells = 1.0 - abs(snoise(unit * 90.0 + vec3(0.0, t, -t)));
    float fine = 1.0 - abs(snoise(unit * 210.0 + vec3(t, 0.0, t)));
    float supergranules = snoise(unit * 14.0 + vec3(0.0, t * 0.1, 0.0));
    float totalNoise = 0.32 + 0.38 * cells * cells + 0.12 * fine + 0.06 * supergranules;
#ifdef SUNSPOTS
    float mu0 = clamp(dot(normalize(fNormal), normalize(frame.cameraPos.xyz - fWorldPosition)), 0.0, 1.0);
    float darkening = SpotWing(unit, uSpotBands.x, uSpotBands.y, uSpotCoverage.x, uSpotDays, 3.1) +
                      SpotWing(unit, uSpotBands.z, uSpotBands.w, uSpotCoverage.y, uSpotDays, 11.7);
    // Faculae: the bright network round active regions, seen best toward the limb.
    float faculae = -darkening * 0.25 * (1.0 - mu0);
    spotFactor = max(1.0 + darkening, 0.15) + faculae;
#endif
#else
    vec3 sPosition = position * starRadiusInKilometers;

    // Sunspots
    float s = 0.3;
    float frequency = 0.00001;
    float t1 = snoise(sPosition * frequency) - s;
    float t2 = snoise((sPosition + starRadiusInKilometers) * frequency) - s;
    float ss = (max(t1, 0.0) * max(t2, 0.0)) * 2.0;

    // Granulation
    float n = (noise(position, 4, 40.0, 0.7) + 1.0) * 0.5;
    float totalNoise = n - ss;
#endif

    vec3 colorByTemperature = texture(colorMap, vec2(uColorMap, 0.0)).rgb;
    vec3 surface = colorByTemperature * (shiftStarColor * colorMult + totalNoise - 0.1) * spotFactor;

    // mu = cosine between the surface normal and the line of sight.
    float mu = clamp(dot(normalize(fNormal), normalize(frame.cameraPos.xyz - fWorldPosition)), 0.0, 1.0);
    vec3 limb = pow(vec3(max(mu, 1e-4)), LIMB_DARKENING_ALPHA);

    fragColor = vec4(surface * limb, float(isVisible));
}
