// Scattering phase functions. c = cos(angle between view ray and light ray).

// O'Neil's (GPU Gems 2, ch. 16) unnormalized forms, kept exactly for atmosphere.fs.
// Mie, g in (-0.75, -0.999):
//      3 * ( 1 - g^2 )               1 + c^2
// F = ----------------- * -------------------------------
//      2 * ( 2 + g^2 )     ( 1 + g^2 - 2 * g * c )^(3/2)
float miePhase(float g, float c, float cc) {
    float gg = g * g;

    float a = (1.0 - gg) * (1.0 + cc);

    float b = 1.0 + gg - 2.0 * g * c;
    b *= sqrt(b);
    b *= 2.0 + gg;

    return 1.5 * a / b;
}

float rayleighPhase(float cc) {
    return 0.75 * (1.0 + cc);
}

// Normalized over the sphere (integrate to 1), for the physically based path.
const float PHASE_INV_4PI = 0.07957747154594767;

float RayleighPhaseNormalized(float c) {
    return 3.0 / (16.0 * 3.14159265359) * (1.0 + c * c);
}

// Cornette-Shanks: Henyey-Greenstein with Rayleigh-like 1 + c^2 shaping. g > 0 is forward.
float CornetteShanksPhase(float g, float c) {
    float gg = g * g;
    float k = 3.0 / (8.0 * 3.14159265359) * (1.0 - gg) / (2.0 + gg);
    float denom = 1.0 + gg - 2.0 * g * c;
    return k * (1.0 + c * c) / (denom * sqrt(denom));
}
