// Auroral emission, shared by the auroral shell seen from space (aurora.fs, AuroraShell) and
// the sky seen from the ground (observeSky.fs), so the curtain overhead at Tromsø is the same
// one the orbit view shows (docs/ARCHITECTURE.md §13).
//
// Positions are in the body's sphere frame: centre at the origin, the surface the unit sphere
// (an oblate body's flattening undone). The oval comes from SpaceWeather (Feldstein–Starkov
// for Earth): boundaries in magnetic latitude at magnetic noon and midnight, cosine-blended in
// between, moving equatorward as Kp rises. Only the night side emits.
//
// Permutations (declared by the including stage): AURORA_ANIMATED moves the oval's brightness
// along it with frame time; AURORA_CURTAINS adds folded sheets with vertical rays (simplex
// noise, so the stage also includes common/noise_simplex3d.glsl). Neither: a static ring.

struct AuroraParams {
    vec3 x;          // magnetic frame: toward magnetic noon (the Sun, made perpendicular to z)
    vec3 y;          // toward dusk
    vec3 z;          // the magnetic north pole
    vec3 sunDir;     // unit vector to the Sun
    vec4 oval;       // radians: equatorward midnight, equatorward noon, poleward midnight, poleward noon
    float bottom;    // emitting layer, in planet radii from the centre
    float top;
    float kp;
    float brightness;
    float time;
    vec3 colorLow;   // lower curtain (Earth: O 557.7 nm)
    vec3 colorHigh;  // upper glow (Earth: O 630.0 nm)
    vec3 colorEdge;  // storm-time lower border (Earth: N2+ 427.8 nm)
};

// Build the magnetic frame from a pole and the Sun direction.
void AuroraFrame(inout AuroraParams a, vec3 pole, vec3 sunDir) {
    a.z = normalize(pole);
    a.sunDir = normalize(sunDir);
    vec3 noon = a.sunDir - dot(a.sunDir, a.z) * a.z;
    a.x = dot(noon, noon) > 1e-8 ? normalize(noon) : normalize(cross(a.z, vec3(0.0, 0.0, 1.0)) + vec3(1e-4, 0.0, 0.0));
    a.y = cross(a.z, a.x);
}

// How far inside the oval |lat| is at magnetic local time `mlt` (0 at noon): 0 outside, 1 in
// the band; `across` is 0 at the equatorward boundary and 1 at the poleward one.
float AuroraBand(AuroraParams a, float absLat, float mlt, out float across, out float width) {
    float night = 0.5 * (1.0 - cos(mlt));
    float equatorward = mix(a.oval.y, a.oval.x, night);
    float poleward = mix(a.oval.w, a.oval.z, night);
    width = max(poleward - equatorward, 0.01);
    across = (absLat - equatorward) / width;
    float soft = max(0.012, 0.18 * width);
    float band = smoothstep(equatorward - soft, equatorward + 0.5 * soft, absLat) *
                 (1.0 - smoothstep(poleward - 0.5 * soft, poleward + soft, absLat));
    // Substorms brighten the midnight sector.
    return band * mix(0.45, 1.0, night);
}

// Emission per planet radius of path at `p` (sphere frame).
vec3 AuroraEmission(AuroraParams a, vec3 p) {
    float r = length(p);
    float h = (r - a.bottom) / (a.top - a.bottom);
    if (h <= 0.0 || h >= 1.0) {
        return vec3(0.0);
    }
    vec3 n = p / r;
    float night = smoothstep(0.1, -0.2, dot(n, a.sunDir));
    if (night <= 0.0) {
        return vec3(0.0);
    }
    float lat = asin(clamp(dot(n, a.z), -1.0, 1.0));
    float absLat = abs(lat);
    float mlt = atan(dot(n, a.y), dot(n, a.x));
    float across;
    float width;
    float band = AuroraBand(a, absLat, mlt, across, width);
    if (band <= 0.001) {
        return vec3(0.0);
    }

    float structure = band;
#if defined(AURORA_CURTAINS)
    // A thin sheet folding along the oval, with fine vertical rays, both drifting with time.
    float hemisphere = lat >= 0.0 ? 1.7 : 5.3;
    float fold = snoise(vec3(mlt * 3.0, a.time * 0.035, hemisphere));
    float sheetAcross = 0.4 + 0.3 * fold;
    float sheet = exp(-pow((across - sheetAcross) / 0.22, 2.0));
    float rays = 0.5 + 0.5 * snoise(vec3(mlt * 70.0 + fold * 6.0, h * 1.5, a.time * 0.25 + hemisphere));
    float secondSheet = exp(-pow((across - sheetAcross - 0.35) / 0.12, 2.0)) * 0.5;
    structure = band * (0.25 + 1.6 * (sheet + secondSheet) * (0.35 + 0.65 * rays * rays));
#elif defined(AURORA_ANIMATED)
    structure = band * (0.7 + 0.3 * sin(mlt * 5.0 + a.time * 0.4) * sin(mlt * 11.0 - a.time * 0.65));
#endif

    // Vertical profile: green low, red above it (strong in big storms, the low-latitude red
    // aurora), blue-violet along the lower border when the storm is strong.
    float storm = smoothstep(4.0, 8.0, a.kp);
    float green = smoothstep(0.0, 0.1, h) * (1.0 - smoothstep(0.25, 0.6, h));
    // The red layer is fainter per unit path than the green; it shows on the limb and in
    // great storms, where it is what low-latitude observers see.
    float red = smoothstep(0.3, 0.65, h) * (1.0 - smoothstep(0.85, 1.0, h)) * (0.12 + 0.38 * storm);
    float edge = (1.0 - smoothstep(0.0, 0.15, h)) * (1.0 - smoothstep(0.0, 0.3, across)) * storm;
    float activity = 0.25 + 0.75 * pow(clamp(a.kp / 9.0, 0.0, 1.0), 1.2);
    vec3 color = a.colorLow * green + a.colorHigh * red + a.colorEdge * edge * 1.5;
    return color * structure * night * activity * a.brightness;
}

// Emission integrated along origin + t·dir (dir unit, sphere frame) up to tMax, in `steps`
// samples, stopping at the solid body. Normalised so a vertical pass through the layer gives
// about AuroraEmission's value: a slanted one, toward the horizon or the limb, is brighter.
vec3 AuroraMarch(AuroraParams a, vec3 origin, vec3 dir, float tMax, int steps) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - a.top * a.top;
    float d = b * b - c;
    if (d <= 0.0) {
        return vec3(0.0);
    }
    d = sqrt(d);
    float t0 = max(-b - d, 0.0);
    float t1 = min(-b + d, tMax);
    float cGround = dot(origin, origin) - 1.0;
    float dGround = b * b - cGround;
    if (dGround > 0.0) {
        float hit = -b - sqrt(dGround);
        if (hit > 0.0) {
            t1 = min(t1, hit);
        }
    }
    if (t1 <= t0) {
        return vec3(0.0);
    }
    float dt = (t1 - t0) / float(steps);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 32; ++i) {
        if (i >= steps) {
            break;
        }
        sum += AuroraEmission(a, origin + dir * (t0 + (float(i) + 0.5) * dt));
    }
    return sum * dt / (a.top - a.bottom);
}
