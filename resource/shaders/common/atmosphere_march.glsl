// The physically based atmosphere's view-ray integral, shared by the planet shell seen from
// anywhere (atmospherePbr.fs) and the ground observer's sky dome (observeSky.fs).
//
// Needs common/phase.glsl and common/atmosphere_lut.glsl, and the including shader must define
//     float AtmosphereSunVisibility(vec3 positionKm);
// the fraction of direct sunlight reaching a planet-centred sample point (km, in the frame the
// LUTs are traced in) past anything the LUTs do not know about: rings, an eclipsing moon.

const int ATMOSPHERE_MAX_STEPS = 32;

float AtmosphereSunVisibility(vec3 positionKm);

// In-scattered light along origin + viewDir * t for t in [tStart, tEnd] (km, planet-centred),
// with `steps` samples. `throughput` is what survives of the light behind the segment.
vec3 IntegrateAtmosphereRay(AtmosphereParams atmosphere, sampler2D transmittanceLut, sampler2D multiScatteringLut,
                            vec3 origin, vec3 viewDir, vec3 sunDir, float tStart, float tEnd, int steps,
                            out vec3 throughput) {
    // Samples bunch up where the density peaks: at the ray's closest approach to the
    // planet (the limb) or at the ground. Two halves around that point, each with a
    // quadratic warp toward it, keep 8 samples usable on a 2000 km grazing ray.
    float tPeak = clamp(-dot(origin, viewDir), tStart, tEnd);
    steps = clamp(steps, 2, ATMOSPHERE_MAX_STEPS);
    int firstHalf = tPeak > tStart ? (tPeak < tEnd ? steps / 2 : steps) : 0;
    int secondHalf = steps - firstHalf;

    float nu = dot(viewDir, sunDir);
    float rayleighPhase = RayleighPhaseNormalized(nu);
    float miePhase = CornetteShanksPhase(atmosphere.miePhaseG, nu);

    vec3 luminance = vec3(0.0);
    throughput = vec3(1.0);
    for (int i = 0; i < ATMOSPHERE_MAX_STEPS; ++i) {
        if (i >= steps)
            break;

        // Midpoint of sample i in its half, warped quadratically toward tPeak.
        float t;
        float dt;
        if (i < firstHalf) {
            float u = (float(i) + 0.5) / float(firstHalf);
            float span = tPeak - tStart;
            t = tPeak - span * (1.0 - u) * (1.0 - u);
            dt = 2.0 * span * (1.0 - u) / float(firstHalf);
        } else {
            float u = (float(i - firstHalf) + 0.5) / float(secondHalf);
            float span = tEnd - tPeak;
            t = tPeak + span * u * u;
            dt = 2.0 * span * u / float(secondHalf);
        }

        vec3 position = origin + viewDir * t;
        float r = length(position);
        float muS = dot(position / r, sunDir);

        vec3 rayleighScattering, mieScattering, extinction;
        SampleAtmosphereMedium(atmosphere, r - atmosphere.bottomRadius, rayleighScattering, mieScattering, extinction);

        vec3 sunTransmittance = RayIntersectsGround(atmosphere, r, muS)
            ? vec3(0.0)
            : texture(transmittanceLut, TransmittanceLutUv(atmosphere, r, muS)).rgb;
        vec3 multipleScattering = texture(multiScatteringLut, MultiScatteringLutUv(atmosphere, r, muS)).rgb;

        vec3 direct = sunTransmittance * AtmosphereSunVisibility(position);
        vec3 source = rayleighScattering * (rayleighPhase * direct + multipleScattering)
                    + mieScattering * (miePhase * direct + multipleScattering);

        // Exact integral of a constant source across the step (Hillaire 2015).
        vec3 stepTransmittance = exp(-extinction * dt);
        luminance += throughput * (source - source * stepTransmittance) / max(extinction, vec3(1e-7));
        throughput *= stepTransmittance;
    }
    return luminance;
}
