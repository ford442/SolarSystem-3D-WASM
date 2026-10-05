#version 300 es

//
// Physically based atmosphere from baked lookup tables (Medium/Full presets).
//
// One view ray per fragment of the shell (sphere.obj, drawn at the top-of-atmosphere
// radius). Sun transmittance and multiple scattering come from LUTs baked offline by
// tools/atmosphere_lut_baker (Bruneton 2017 transmittance mapping, Hillaire 2020
// multi-scattering), so the only loop is the view ray itself: no nested march, no compute,
// no float render target. Inputs are the catalog's render.atmosphere.physical block.
//
// Output is premultiplied: rgb = in-scattered light, a = how much of what lies behind
// survives. Drawn with glBlendFunc(GL_ONE, GL_SRC_ALPHA), so the planet (and the stars at
// the limb) are dimmed by the atmosphere they are seen through, not just tinted.
// See docs/ARCHITECTURE.md §9.2.
//

in vec3 fWorldPosition;
in vec3 fPosition;
in mat3 modelMat3;
in vec4 fragPosLightSpace;

out vec4 fragColor;

#include "common/raytrace.glsl"
#include "common/ring.glsl"
#include "common/eclipse.glsl"
#include "common/phase.glsl"
#include "common/atmosphere_lut.glsl"

uniform AtmosphereParams uAtmosphere;  // km, thicknessScale already applied
uniform sampler2D transmittanceLut;
uniform sampler2D multiScatteringLut;

uniform vec3 camPosition;      // camera relative to the atmosphere centre, scene units
uniform vec3 lightPos;         // Sun relative to the atmosphere centre, scene units
uniform float kmPerSceneUnit;
uniform bool isCameraInsideShell;
uniform int uSteps;            // view-ray samples (quality tier)
uniform float uExposure;
uniform float uSurfaceDim;

// Other bodies' shadows. The planet's own shadow is exact per sample (a sun ray that hits
// the ground gets no direct light), so the shared shadow map is not used here: it also
// contains this planet, and would black out the lit half of every limb ray.
uniform bool isNearbyPlanetaryRing;
uniform bool isUseSphereIntersect;
uniform vec3 ringParentPlanetCenter;   // world space
uniform float ringParentPlanetRadiusSquared;
uniform vec3 ringCenter;               // world space
uniform vec3 ringNormal;
uniform vec2 ringInnerOuterRadiuses;
uniform sampler2D ringDiffuse;

uniform bool hasEclipseCaster;
uniform vec3 eclipseCasterCenter;      // relative to the atmosphere centre, scene units
uniform float eclipseCasterRadius;
uniform float eclipseStarRadius;

const int MAX_STEPS = 32;

// Direct sunlight reaching the shell fragment past the ring and (for a moon) its ringed
// parent. Evaluated once per fragment: rings are far larger than an atmosphere's depth.
float RingVisibility() {
    if (!isNearbyPlanetaryRing)
        return 1.0;
    vec3 atmosphereCenter = fWorldPosition - modelMat3 * fPosition;
    vec3 sunWorld = atmosphereCenter + lightPos;
    vec3 toSun = normalize(sunWorld - fWorldPosition);
    if (isUseSphereIntersect && intersectSphereAhead(fWorldPosition, toSun, ringParentPlanetCenter, ringParentPlanetRadiusSquared))
        return 0.0;
    float u;
    if (RingCrossing(fWorldPosition, toSun, toSun, ringCenter, ringNormal, ringInnerOuterRadiuses, u))
        return clamp(1.0 - RingOpacity(texture(ringDiffuse, vec2(u, 0.0))), 0.0, 1.0);
    return 1.0;
}

void main() {
    float bottom = uAtmosphere.bottomRadius;
    float top = uAtmosphere.topRadius;

    // Planet-centred km. From outside, start at the shell fragment rather than the camera:
    // both lie on the ray, and the fragment is close to the sphere, which keeps the
    // intersection precise when the camera is thousands of radii away.
    vec3 fragmentOffset = modelMat3 * fPosition;
    vec3 viewDir = normalize(fragmentOffset - camPosition);
    vec3 origin = (isCameraInsideShell ? camPosition : fragmentOffset) * kmPerSceneUnit;
    vec3 sunDir = normalize(lightPos);

    vec2 topHit = raySphere(origin, viewDir, top);
    if (topHit.x > topHit.y || topHit.y <= 0.0)
        discard;
    float tStart = max(topHit.x, 0.0);
    float tEnd = topHit.y;
    vec2 groundHit = raySphere(origin, viewDir, bottom);
    if (groundHit.x <= groundHit.y && groundHit.x > 0.0)
        tEnd = min(tEnd, groundHit.x);
    if (tEnd <= tStart)
        discard;

    // Samples bunch up where the density peaks: at the ray's closest approach to the
    // planet (the limb) or at the ground. Two halves around that point, each with a
    // quadratic warp toward it, keep 8 samples usable on a 2000 km grazing ray.
    float tPeak = clamp(-dot(origin, viewDir), tStart, tEnd);
    int steps = clamp(uSteps, 2, MAX_STEPS);
    int firstHalf = tPeak > tStart ? (tPeak < tEnd ? steps / 2 : steps) : 0;
    int secondHalf = steps - firstHalf;

    float nu = dot(viewDir, sunDir);
    float rayleighPhase = RayleighPhaseNormalized(nu);
    float miePhase = CornetteShanksPhase(uAtmosphere.miePhaseG, nu);
    float ringVisibility = RingVisibility();

    vec3 luminance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    for (int i = 0; i < MAX_STEPS; ++i) {
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
        SampleAtmosphereMedium(uAtmosphere, r - bottom, rayleighScattering, mieScattering, extinction);

        vec3 sunTransmittance = RayIntersectsGround(uAtmosphere, r, muS)
            ? vec3(0.0)
            : texture(transmittanceLut, TransmittanceLutUv(uAtmosphere, r, muS)).rgb;
        float sunVisibility = ringVisibility;
        if (hasEclipseCaster)
            sunVisibility *= EclipseVisibilityAt(position / kmPerSceneUnit, lightPos, eclipseCasterCenter,
                                                 eclipseCasterRadius, eclipseStarRadius);
        vec3 multipleScattering = texture(multiScatteringLut, MultiScatteringLutUv(uAtmosphere, r, muS)).rgb;

        vec3 direct = sunTransmittance * sunVisibility;
        vec3 source = rayleighScattering * (rayleighPhase * direct + multipleScattering)
                    + mieScattering * (miePhase * direct + multipleScattering);

        // Exact integral of a constant source across the step (Hillaire 2015).
        vec3 stepTransmittance = exp(-extinction * dt);
        luminance += throughput * (source - source * stepTransmittance) / max(extinction, vec3(1e-7));
        throughput *= stepTransmittance;
    }

    // The default framebuffer is LDR and gamma-encoded: expose, compress, encode. The
    // planet behind is already encoded, so its attenuation is encoded the same way.
    vec3 color = 1.0 - exp(-luminance * uExposure);
    color = pow(color, vec3(1.0 / 2.2)) * uSurfaceDim;
    float transmittance = dot(throughput, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(color, pow(clamp(transmittance, 0.0, 1.0), 1.0 / 2.2));
}
