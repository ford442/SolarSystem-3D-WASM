#version 300 es
// NEAR_SURFACE (atmosphere.vs) writes depth per fragment.
// permutations: NEAR_SURFACE
#ifdef NEAR_SURFACE
in float vLogDepth;
#endif

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

#include "common/log_depth.glsl"
#include "common/raytrace.glsl"
#include "common/ring.glsl"
#include "common/eclipse.glsl"
#include "common/phase.glsl"
#include "common/atmosphere_lut.glsl"

uniform AtmosphereParams uAtmosphere;  // km, thicknessScale already applied
layout(binding = TU_TRANSMITTANCE_LUT) uniform sampler2D transmittanceLut;
layout(binding = TU_MULTI_SCATTERING_LUT) uniform sampler2D multiScatteringLut;

uniform vec3 camPosition;      // camera relative to the atmosphere centre, scene units
uniform vec3 lightPos;         // Sun relative to the atmosphere centre, scene units
uniform float kmPerSceneUnit;
uniform bool isCameraInsideShell;
uniform int uSteps;            // view-ray samples (quality tier)
uniform float uExposure;
uniform float uSurfaceDim;

// Offsets from the atmosphere centre (scene units) to the frame in which an oblate body's
// cloud deck is a sphere of its equatorial radius, and back (BodyShape.h). The shell mesh
// is flattened with the body; the ray is traced in the sphere frame, where the LUTs apply.
// Identity for a spherical body.
uniform mat3 bodyToSphere;
uniform mat3 sphereToBody;

// Other bodies' shadows. The planet's own shadow is exact per sample (a sun ray that hits
// the ground gets no direct light), so the shared shadow map is not used here: it also
// contains this planet, and would black out the lit half of every limb ray.
uniform bool isNearbyPlanetaryRing;
uniform bool isUseSphereIntersect;
uniform bool isRingShadowPerSample;    // this atmosphere's own planet carries the ring
uniform vec3 ringParentPlanetCenter;   // world space
uniform float ringParentPlanetRadiusSquared;
uniform vec3 ringCenter;               // world space
uniform vec3 ringNormal;
uniform vec2 ringInnerOuterRadiuses;
layout(binding = TU_RING) uniform sampler2D ringDiffuse;

uniform bool hasEclipseCaster;
uniform vec3 eclipseCasterCenter;      // relative to the atmosphere centre, scene units
uniform float eclipseCasterRadius;
uniform float eclipseStarRadius;

#include "common/atmosphere_march.glsl"

// Ring shadow state for this fragment, set in main() before the march calls back into
// AtmosphereSunVisibility. On a moon's air (Titan) the ring is far larger than the
// atmosphere, so one test at the shell fragment serves the whole ray. On the ringed planet
// itself the shadow is a band across the disc, and a ray entering the shell above it can
// reach the cloud deck inside it, so each sample is tested where it is.
vec3 gAtmosphereCenter;   // world space
vec3 gSunWorld;           // world space
bool gRingPerSample;
float gRingVisibility;

// Direct sunlight reaching `worldPoint` past the ring and (for a moon) its ringed parent.
// The sun direction is taken from the point itself, so the ring's opening angle toward the
// Sun follows the date through the planet's orbital position. Called inside the view-ray
// loop under non-uniform control flow, so the ring texture is read at an explicit LOD.
float RingVisibilityAt(vec3 worldPoint, vec3 sunWorld) {
    vec3 toSun = normalize(sunWorld - worldPoint);
    if (isUseSphereIntersect && intersectSphereAhead(worldPoint, toSun, ringParentPlanetCenter, ringParentPlanetRadiusSquared))
        return 0.0;
    float u;
    if (RingCrossing(worldPoint, toSun, toSun, ringCenter, ringNormal, ringInnerOuterRadiuses, u))
        return clamp(1.0 - RingOpacity(textureLod(ringDiffuse, vec2(u, 0.0), 0.0)), 0.0, 1.0);
    return 1.0;
}

float AtmosphereSunVisibility(vec3 positionKm) {
    vec3 sampleOffset = sphereToBody * (positionKm / kmPerSceneUnit);  // back to scene units
    float sunVisibility = gRingVisibility;
    if (gRingPerSample)
        sunVisibility *= RingVisibilityAt(gAtmosphereCenter + sampleOffset, gSunWorld);
    if (hasEclipseCaster)
        sunVisibility *= EclipseVisibilityAt(sampleOffset, lightPos, eclipseCasterCenter,
                                             eclipseCasterRadius, eclipseStarRadius);
    return sunVisibility;
}

void main() {
#ifdef NEAR_SURFACE
    gl_FragDepth = LogFragDepth(vLogDepth); // first: every path that is not discarded writes it
#endif
    float bottom = uAtmosphere.bottomRadius;
    float top = uAtmosphere.topRadius;

    // Planet-centred km in the sphere frame. From outside, start at the shell fragment
    // rather than the camera: both lie on the ray, and the fragment is close to the sphere,
    // which keeps the intersection precise when the camera is thousands of radii away.
    // The map is linear, so the mapped view ray is still a straight line.
    vec3 worldOffset = modelMat3 * fPosition;
    vec3 fragmentOffset = bodyToSphere * worldOffset;
    vec3 camera = bodyToSphere * camPosition;
    vec3 viewDir = normalize(fragmentOffset - camera);
    vec3 origin = (isCameraInsideShell ? camera : fragmentOffset) * kmPerSceneUnit;
    vec3 sunDir = normalize(bodyToSphere * lightPos);

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

    gAtmosphereCenter = fWorldPosition - worldOffset;
    gSunWorld = gAtmosphereCenter + lightPos;
    gRingPerSample = isNearbyPlanetaryRing && isRingShadowPerSample;
    gRingVisibility = isNearbyPlanetaryRing && !gRingPerSample ? RingVisibilityAt(fWorldPosition, gSunWorld) : 1.0;

    vec3 throughput;
    vec3 luminance = IntegrateAtmosphereRay(uAtmosphere, transmittanceLut, multiScatteringLut, origin, viewDir,
                                            sunDir, tStart, tEnd, uSteps, throughput);

    // The default framebuffer is LDR, and the planet shaders write texture colour times
    // lighting with no gamma step, so stay in that same space: expose and compress only.
    // (Gamma-encoding here would lift the faint over-the-disc haze to a grey veil.)
    vec3 color = (1.0 - exp(-luminance * uExposure)) * uSurfaceDim;
    float transmittance = dot(throughput, vec3(0.2126, 0.7152, 0.0722));
    fragColor = vec4(color, clamp(transmittance, 0.0, 1.0));
}
