// Lookup-table parameterisation for the physically based atmosphere, mirroring
// src/Solar_System/AtmosphereModel.cpp (which bakes the tables). Distances in km.
// Changing a mapping or size here means changing it there too and bumping both versions;
// tests/test_atmosphere_model.cpp checks that these constants agree.
#define ATMO_LUT_MAPPING_VERSION 1
const float TRANSMITTANCE_LUT_WIDTH = 256.0;
const float TRANSMITTANCE_LUT_HEIGHT = 64.0;
const float MULTISCATTERING_LUT_SIZE = 32.0;

struct AtmosphereParams {
    float bottomRadius;
    float topRadius;
    vec3 rayleighScattering;
    float rayleighScaleHeight;
    vec3 mieScattering;
    vec3 mieExtinction;
    float mieScaleHeight;
    float miePhaseG;
    vec3 absorptionExtinction;
    float absorptionCenter;
    float absorptionHalfWidth;
};

float TexCoordFromUnitRange(float x, float textureSize) {
    return 0.5 / textureSize + x * (1.0 - 1.0 / textureSize);
}

float DistanceToTopBoundary(AtmosphereParams atmosphere, float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + atmosphere.topRadius * atmosphere.topRadius;
    return max(0.0, -r * mu + sqrt(max(discriminant, 0.0)));
}

bool RayIntersectsGround(AtmosphereParams atmosphere, float r, float mu) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + atmosphere.bottomRadius * atmosphere.bottomRadius >= 0.0;
}

// Bruneton 2017 transmittance mapping: u from the distance to the top boundary relative to
// its range at this radius, v from the distance to the horizon.
vec2 TransmittanceLutUv(AtmosphereParams atmosphere, float r, float mu) {
    float bottom = atmosphere.bottomRadius;
    float top = atmosphere.topRadius;
    float H = sqrt(top * top - bottom * bottom);
    // (r - b)(r + b) keeps its precision near the ground where r^2 - b^2 would cancel.
    float rho = sqrt(max((r - bottom) * (r + bottom), 0.0));
    float d = DistanceToTopBoundary(atmosphere, r, mu);
    float dMin = top - r;
    float dMax = rho + H;
    float xMu = dMax > dMin ? (d - dMin) / (dMax - dMin) : 0.0;
    float xR = rho / H;
    return vec2(TexCoordFromUnitRange(clamp(xMu, 0.0, 1.0), TRANSMITTANCE_LUT_WIDTH),
                TexCoordFromUnitRange(clamp(xR, 0.0, 1.0), TRANSMITTANCE_LUT_HEIGHT));
}

vec2 MultiScatteringLutUv(AtmosphereParams atmosphere, float r, float muS) {
    float x = clamp(muS * 0.5 + 0.5, 0.0, 1.0);
    float y = clamp((r - atmosphere.bottomRadius) / (atmosphere.topRadius - atmosphere.bottomRadius), 0.0, 1.0);
    return vec2(TexCoordFromUnitRange(x, MULTISCATTERING_LUT_SIZE), TexCoordFromUnitRange(y, MULTISCATTERING_LUT_SIZE));
}

// Rayleigh and Mie scattering, and total extinction, at `altitude` km above the ground.
void SampleAtmosphereMedium(AtmosphereParams atmosphere, float altitude,
                            out vec3 rayleighScattering, out vec3 mieScattering, out vec3 extinction) {
    float h = max(altitude, 0.0);
    float rayleighDensity = exp(-h / atmosphere.rayleighScaleHeight);
    float mieDensity = exp(-h / atmosphere.mieScaleHeight);
    float absorptionDensity = max(0.0, 1.0 - abs(h - atmosphere.absorptionCenter) / atmosphere.absorptionHalfWidth);
    rayleighScattering = atmosphere.rayleighScattering * rayleighDensity;
    mieScattering = atmosphere.mieScattering * mieDensity;
    extinction = rayleighScattering + atmosphere.mieExtinction * mieDensity +
                 atmosphere.absorptionExtinction * absorptionDensity;
}
