#version 300 es

// Solar-wind particle advection (render/SolarWindRenderer, docs/ARCHITECTURE.md §13): one
// vertex per particle, read from last frame's buffer and written by transform feedback into
// this frame's, with the rasterizer off. Two kinds of group:
//
//   background (uGroup < 0): heliocentric, born at the Sun and carried radially at the wind
//     speed. A parcel leaves the corotating Sun, so where the fast streams sit traces the
//     Parker spiral; a CME is a faster, denser shell sweeping through them.
//   local (uGroup = body index): the wind as a magnetosphere meets it, in the body's frame and
//     in units of its drawn radius, so the planet's motion carries the group. Particles slow
//     and pile up behind the bow shock, slide round the Shue magnetopause, are steered along
//     the dipole field into the cusps (one B(r) per particle per frame) or captured in the
//     tail, ride a field line down to the auroral oval and flash there; then they start over.
//
// State (two vec4s, interleaved):
//   background: s0 = (position in AU, age), s1 = (alive, seed, fast-wind share, last radius AU)
//   local:      s0 = (position in drawn radii | progress, age), s1 = (mode, seed, MLT, signed latitude)
// Mode: -1 empty, 0 wind, 1 down a cusp, 2 down from the tail.

layout (location = 0) in vec4 aState0;
layout (location = 1) in vec4 aState1;

out vec4 vState0;
out vec4 vState1;

uniform int uGroup;
uniform float uReset;

#include "common/space_weather.glsl"

const float kPi = 3.14159265;
const float kTwoPi = 6.2831853;

uint gRandom;

uint SwHash(uint x) {
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}

float Random() {
    gRandom = SwHash(gRandom);
    return float(gRandom >> 8u) * (1.0 / 16777216.0);
}

// --- Background ----------------------------------------------------------------------------

float FastShare(float latitude, float sourceLongitude) {
    float polar = smoothstep(sw.step.y, sw.step.y + 0.17, abs(latitude));
    float phase = sw.step.w;
    float amplitude = 0.35 + 0.45 * exp(-pow((phase - 0.75) / 0.18, 2.0));
    float stream = amplitude * pow(max(0.0, cos(2.0 * sourceLongitude)), 4.0);
    return max(polar, stream);
}

void SpawnBackground(bool steady, float seed) {
    float spawn = sw.step.z;
    float edge = sw.wind.y;
    float lon = kTwoPi * Random();
    // Concentrated toward the ecliptic, where the planets are.
    float lat = asin(2.0 * Random() - 1.0) * 0.6;
    vec3 dir = vec3(cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon));
    // Each particle turns back at its own radius, most of them in the inner system.
    float rMax = mix(spawn * 6.0, edge, pow(fract(seed * 7.31 + 0.17), 2.2));
    float r = steady ? mix(spawn, rMax, Random()) : spawn;
    // The longitude on the Sun this parcel left from, (r - spawn) / v ago.
    float source = lon - sw.wind.z * sw.wind.w + sw.wind.z * (r - spawn) / max(sw.wind.x, 1e-6);
    float fast = FastShare(lat, source);
    vState0 = vec4(dir * r, steady ? Random() * 10.0 : 0.0);
    vState1 = vec4(0.0, seed, fast, rMax);
}

void UpdateBackground() {
    float seed = aState1.x < 0.0 ? Random() : aState1.y;
    if (uReset > 0.5 || aState1.x < 0.0) {
        SpawnBackground(true, seed);
        return;
    }
    vec3 p = aState0.xyz;
    float r = max(length(p), 1e-6);
    vec3 dir = p / r;
    float speed = sw.wind.x * mix(1.0, 1.875, aState1.z);
    for (int i = 0; i < 4; ++i) {
        if (i >= sw.counts.y) {
            break;
        }
        float strength = SwCmeSample(i, dir, r).x;
        speed = max(speed, sw.wind.x * mix(1.0, sw.cmes[i].shape.z, strength));
    }
    r += speed * sw.step.x;
    if (r > aState1.w) {
        SpawnBackground(false, seed);
        return;
    }
    vState0 = vec4(dir * r, aState0.w + sw.step.x);
    vState1 = aState1;
}

// --- Local ---------------------------------------------------------------------------------

float OvalAt(vec2 boundaries, float mlt) {
    // boundaries = (midnight, noon); mlt 0 at noon.
    return mix(boundaries.y, boundaries.x, 0.5 * (1.0 - cos(mlt)));
}

void Basis(vec3 n, out vec3 u, out vec3 v) {
    vec3 helper = abs(n.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    u = normalize(cross(helper, n));
    v = cross(n, u);
}

vec3 FieldDirection(vec3 z, vec3 q) {
    // Dipole along z: B ∝ (3 (z·q̂) q̂ − z) / r³; only the direction is needed.
    vec3 n = normalize(q);
    return normalize(3.0 * dot(z, n) * n - z);
}

vec3 PrecipitationPoint(SwBody b, float mode, float mlt, float signedLat, float progress) {
    vec3 x;
    vec3 y;
    vec3 z;
    SwMagneticFrame(b, x, y, z);
    float hemi = signedLat >= 0.0 ? 1.0 : -1.0;
    float target = max(abs(signedLat), 0.2);
    vec3 meridian = cos(mlt) * x + sin(mlt) * y;
    float shellL = 1.0 / max(cos(target) * cos(target), 1e-3);

    vec3 entry;
    if (mode < 1.5) {
        // The cusp: high on the dayside, at the magnetopause.
        float cuspLat = 1.36; // ~78°
        vec3 d = cos(cuspLat) * meridian + sin(cuspLat) * hemi * z;
        entry = d * SwMagnetopause(b.axis.w, b.sunward.w, dot(d, b.sunward.xyz)) * 0.92;
    } else {
        // The plasma sheet down the tail.
        entry = meridian * clamp(shellL, 4.0, 14.0);
    }
    float startLat = acos(sqrt(clamp(length(entry) / shellL, 0.0, 1.0)));
    float lat = mix(startLat, target, pow(clamp(progress, 0.0, 1.0), 1.3));
    float c = cos(lat);
    vec3 onLine = (c * meridian + sin(lat) * hemi * z) * (shellL * c * c);
    float bottom = max(b.shell.x, 1.0);
    if (length(onLine) < bottom) {
        onLine = normalize(onLine) * bottom;
    }
    return mix(entry, onLine, smoothstep(0.0, 0.35, progress));
}

void SpawnLocal(SwBody b, bool steady, float seed) {
    float s = b.axis.w;
    vec3 sunward = b.sunward.xyz;
    vec3 u;
    vec3 v;
    Basis(sunward, u, v);
    float angle = kTwoPi * Random();
    float radius = 5.0 * s * sqrt(Random());
    float along = steady ? mix(6.0 * s, -20.0 * s, Random()) : 6.0 * s;
    vec3 q = sunward * along + (cos(angle) * u + sin(angle) * v) * radius;
    vState0 = vec4(q, 0.0);
    vState1 = vec4(0.0, seed, 0.0, 0.0);

    // Start some of a fresh group on the way down, so the funnels show at once.
    if (steady && Random() < 0.1) {
        bool cusp = Random() < 0.45;
        float hemi = Random() < 0.5 ? 1.0 : -1.0;
        float mlt = cusp ? (Random() - 0.5) * 1.2 : kPi + (Random() - 0.5) * 2.4;
        float lat = mix(OvalAt(b.oval.xy, mlt), OvalAt(b.oval.zw, mlt), Random());
        float progress = Random();
        vState0 = vec4(PrecipitationPoint(b, cusp ? 1.0 : 2.0, mlt, hemi * lat, progress), progress);
        vState1 = vec4(cusp ? 1.0 : 2.0, seed, mlt, hemi * lat);
    }
}

void UpdateLocal(SwBody b) {
    float seed = aState1.x < 0.0 ? Random() : aState1.y;
    if (uReset > 0.5 || aState1.x < 0.0) {
        SpawnLocal(b, true, seed);
        return;
    }
    float dt = sw.step.x;
    float s = b.axis.w;
    float alpha = b.sunward.w;
    vec3 sunward = b.sunward.xyz;
    float windRatio = clamp(b.state.z, 0.5, 6.0);

    if (aState1.x > 0.5) {
        // Down a field line; a beat at the foot (the flash), then back upstream.
        float duration = mix(1.4, 2.6, fract(seed * 13.7)) / (0.7 + 0.3 * windRatio);
        float progress = aState0.w + dt / duration;
        if (progress > 1.18) {
            SpawnLocal(b, false, seed);
            return;
        }
        vState0 = vec4(PrecipitationPoint(b, aState1.x, aState1.z, aState1.w, progress), progress);
        vState1 = aState1;
        return;
    }

    vec3 q = aState0.xyz;
    float r = max(length(q), 1e-4);
    vec3 n = q / r;
    float cosTheta = dot(n, sunward);
    float magnetopause = SwMagnetopause(s, alpha, cosTheta);
    float bowShock = 1.3 * SwMagnetopause(s, alpha + 0.12, cosTheta);
    vec3 velocity = -sunward * (0.8 * s * windRatio);

    if (r < bowShock) {
        // The magnetosheath: slowed (the pile-up), turned along the boundary, and near the
        // cusps steered down the field.
        float depth = smoothstep(bowShock, magnetopause, r);
        velocity *= mix(1.0, 0.35, depth);
        float inward = dot(velocity, n);
        if (inward < 0.0) {
            velocity -= inward * n * depth;
        }
        vec3 x;
        vec3 y;
        vec3 z;
        SwMagneticFrame(b, x, y, z);
        vec3 cuspNorth = normalize(z + 0.35 * x);
        vec3 cuspSouth = normalize(-z + 0.35 * x);
        float cusp = smoothstep(0.82, 0.96, max(dot(n, cuspNorth), dot(n, cuspSouth)));
        if (cusp > 0.0) {
            vec3 field = FieldDirection(z, q);
            if (dot(field, n) > 0.0) {
                field = -field;
            }
            velocity = mix(velocity, field * length(velocity), cusp);
        }
    }

    q += velocity * dt;
    r = max(length(q), 1e-4);
    n = q / r;
    cosTheta = dot(n, sunward);
    magnetopause = SwMagnetopause(s, alpha, cosTheta);

    if (r < magnetopause) {
        vec3 x;
        vec3 y;
        vec3 z;
        SwMagneticFrame(b, x, y, z);
        float mlt = atan(dot(n, y), dot(n, x));
        float magLat = asin(clamp(dot(n, z), -1.0, 1.0));
        float hemi = magLat >= 0.0 ? 1.0 : -1.0;
        bool cusp = abs(magLat) > 0.95 && cosTheta > -0.3;
        bool tail = cosTheta < -0.4 && r > 5.0 && abs(dot(q, z)) < 2.5 && fract(seed * 3.1) < 0.6;
        if (cusp || tail) {
            float target;
            if (cusp) {
                mlt = (Random() - 0.5) * 1.2;
                target = mix(OvalAt(b.oval.xy, mlt), OvalAt(b.oval.zw, mlt), 0.3 + 0.7 * Random());
            } else {
                target = mix(OvalAt(b.oval.xy, mlt), OvalAt(b.oval.zw, mlt), Random());
            }
            vState0 = vec4(PrecipitationPoint(b, cusp ? 1.0 : 2.0, mlt, hemi * target, 0.0), 0.0);
            vState1 = vec4(cusp ? 1.0 : 2.0, seed, mlt, hemi * target);
            return;
        }
        q = n * magnetopause * 1.01;
    }

    float along = dot(q, sunward);
    vec3 lateral = q - along * sunward;
    if (along < -20.0 * s || dot(lateral, lateral) > 49.0 * s * s) {
        SpawnLocal(b, false, seed);
        return;
    }
    vState0 = vec4(q, aState0.w + dt);
    vState1 = aState1;
}

void main() {
    gRandom = SwHash(uint(gl_VertexID) * 747796405u + uint(sw.look.w) * 2891336453u + 12345u);
    if (uGroup < 0) {
        UpdateBackground();
    } else {
        UpdateLocal(sw.bodies[uGroup]);
    }
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
