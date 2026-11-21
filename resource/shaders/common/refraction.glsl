// Atmospheric refraction and extinction for Observe mode, in the horizon frame (+Y up).
// A copy of Observer::RefractionArcmin / ExtinctionMagnitudes (Observer.cpp) for what is placed
// on the GPU (stars, constellation lines); test_observer.cpp checks the coefficients agree.

const float SAEMUNDSSON_A = 1.02;
const float SAEMUNDSSON_B = 10.3;
const float SAEMUNDSSON_C = 5.11;
const float REFRACTION_HOLD_BELOW_DEG = -1.0;
const float REFRACTION_ZERO_BELOW_DEG = -5.0;
const float EXTINCTION_MAG_PER_AIRMASS = 0.2;

// Refraction in degrees for a geometric altitude in degrees, at 1010 hPa and 10 deg C.
float RefractionDeg(float geometricAltDeg) {
    float h = clamp(geometricAltDeg, REFRACTION_HOLD_BELOW_DEG, 90.0);
    float r = SAEMUNDSSON_A / tan(radians(h + SAEMUNDSSON_B / (h + SAEMUNDSSON_C)));
    float fade = clamp((geometricAltDeg - REFRACTION_ZERO_BELOW_DEG) /
                       (REFRACTION_HOLD_BELOW_DEG - REFRACTION_ZERO_BELOW_DEG), 0.0, 1.0);
    return max(r, 0.0) * fade / 60.0;
}

// A unit direction lifted by refraction along its own azimuth.
vec3 RefractDirection(vec3 d) {
    float horizontal = length(d.xz);
    if (horizontal < 1e-6)
        return d;
    float alt = degrees(atan(d.y, horizontal));
    float lifted = radians(min(alt + RefractionDeg(alt), 90.0));
    return vec3(d.xz * (cos(lifted) / horizontal), sin(lifted)).xzy;
}

// Magnitudes of airmass dimming relative to the zenith (Kasten & Young 1989) at an apparent altitude.
float ExtinctionMagnitudes(float apparentAltDeg) {
    float h = clamp(apparentAltDeg, 0.0, 90.0);
    float airmass = 1.0 / (sin(radians(h)) + 0.50572 * pow(h + 6.07995, -1.6364));
    return EXTINCTION_MAG_PER_AIRMASS * (airmass - 1.0);
}
