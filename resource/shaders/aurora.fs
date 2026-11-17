#version 300 es

// The auroral oval of one body (common/aurora.glsl), added onto the scene after its
// atmosphere and clouds. The ray from the eye through this fragment is marched through the
// emitting layer in the body's sphere frame and stops at the surface.
//
//   (none)           Low: a static ring, one sample at the green layer, no time
//   AURORA_ANIMATED  Medium: the oval's brightness drifts along it, a short march
//   AURORA_CURTAINS  Full: folded curtains with vertical rays, a longer march (and bloom)
// permutations: AURORA_ANIMATED AURORA_CURTAINS

in vec3 vWorld;
in float vLogDepth;

out vec4 fragColor;

uniform vec3 uCenter;     // body centre, render frame
uniform mat3 uToSphere;   // render-frame offset from the centre → sphere frame (radius 1)
uniform vec3 uPole;       // magnetic north, sphere frame
uniform vec3 uSunDir;     // to the Sun, sphere frame
uniform vec4 uOval;
uniform vec2 uShell;      // bottom, top (planet radii)
uniform float uKp;
uniform float uBrightness;
uniform vec3 uColorLow;
uniform vec3 uColorHigh;
uniform vec3 uColorEdge;
uniform int uSteps;

#include "common/frame.glsl"
#include "common/log_depth.glsl"
#include "common/noise_simplex3d.glsl"
#include "common/aurora.glsl"

void main() {
    AuroraParams a;
    AuroraFrame(a, uPole, uSunDir);
    a.oval = uOval;
    a.bottom = uShell.x;
    a.top = uShell.y;
    a.kp = uKp;
    a.brightness = uBrightness;
#if defined(AURORA_ANIMATED) || defined(AURORA_CURTAINS)
    a.time = frame.time;
#else
    a.time = 0.0;
#endif
    a.colorLow = uColorLow;
    a.colorHigh = uColorHigh;
    a.colorEdge = uColorEdge;

    vec3 emission;
#if defined(AURORA_ANIMATED) || defined(AURORA_CURTAINS)
    vec3 origin = uToSphere * (frame.cameraPos.xyz - uCenter);
    vec3 through = uToSphere * (vWorld - uCenter);
    vec3 dir = normalize(through - origin);
    emission = AuroraMarch(a, origin, dir, 1e9, uSteps);
#else
    // The ring itself, view-independent: the green layer under this point of the shell.
    vec3 n = normalize(uToSphere * (vWorld - uCenter));
    emission = AuroraEmission(a, n * mix(a.bottom, a.top, 0.2));
#endif

    gl_FragDepth = LogFragDepth(vLogDepth);
    float strength = max(max(emission.r, emission.g), emission.b);
    if (strength < 1e-4) {
        discard;
    }
    fragColor = vec4(emission, 1.0);
}
