#ifndef SOLARSYSTEM_ATMOSPHERE_MODEL_H
#define SOLARSYSTEM_ATMOSPHERE_MODEL_H

#include "BodyCatalog.generated.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

/**
 * GL-free physically based atmosphere model shared by the offline LUT baker
 * (tools/atmosphere_lut_baker), the unit tests, and — through its mirror in
 * resource/shaders/common/atmosphere_lut.glsl — the runtime shader.
 *
 * Two lookup tables per body, after Hillaire 2020 ("A Scalable and Production Ready
 * Sky and Atmosphere Rendering Technique") with Bruneton 2017's transmittance mapping:
 *  - transmittance(r, mu): view/sun transmittance to the top of the atmosphere for rays
 *    that do not hit the ground; kTransmittanceWidth x kTransmittanceHeight.
 *  - multi-scattering Psi_ms(r, mu_s): second-and-higher-order in-scattered radiance per
 *    unit sun illuminance and unit scattering coefficient; kMultiScatteringSize^2.
 * Both are RGBA16F; alpha is 1.
 *
 * Units are km. Everything here runs in double precision; the baker writes half floats.
 * Changing any mapping or table size means bumping kLutMappingVersion here AND
 * ATMO_LUT_MAPPING_VERSION in atmosphere_lut.glsl (a test pins them together).
 */
namespace AtmosphereModel {

constexpr int kTransmittanceWidth = 256;
constexpr int kTransmittanceHeight = 64;
constexpr int kMultiScatteringSize = 32;
constexpr int kLutMappingVersion = 1;
// Bump when the integration itself changes (step counts, quadrature), so the hash stored
// in the committed LUTs goes stale and CI asks for a rebake.
constexpr int kBakerVersion = 1;

/** The catalog row with thicknessScale applied: what both the LUTs and the shader use. */
struct Params {
    double bottomRadius = 0.0;
    double topRadius = 0.0;
    glm::dvec3 rayleighScattering{0.0};
    double rayleighScaleHeight = 1.0;
    glm::dvec3 mieScattering{0.0};
    glm::dvec3 mieExtinction{0.0};
    double mieScaleHeight = 1.0;
    double miePhaseG = 0.0;
    glm::dvec3 absorptionExtinction{0.0};
    double absorptionCenter = 0.0;
    double absorptionHalfWidth = 1.0;
    glm::dvec3 groundAlbedo{0.0};
};

/**
 * thicknessScale s stretches the shell (top - ground, scale heights and the absorption
 * layer) by s and divides every coefficient by s, so vertical optical depth — the zenith
 * colour and the transmittance at the terminator — is unchanged while the visible limb
 * widens. An art knob for bodies whose real atmosphere is a sub-pixel line on screen.
 */
Params FromCatalog(const BodyCatalog::AtmospherePhysical& physical);

struct Medium {
    glm::dvec3 rayleighScattering{0.0};
    glm::dvec3 mieScattering{0.0};
    glm::dvec3 scattering{0.0};
    glm::dvec3 extinction{0.0};
};

/** Coefficients at `altitudeKm` above the ground: exponential Rayleigh/Mie, tent absorption. */
Medium SampleMedium(const Params& params, double altitudeKm);

double DistanceToTopBoundary(const Params& params, double r, double mu);
double DistanceToBottomBoundary(const Params& params, double r, double mu);
bool RayIntersectsGround(const Params& params, double r, double mu);

// Texel-centre-exact remap between [0, 1] and texture coordinates (Bruneton 2017 §4.1).
double TexCoordFromUnitRange(double x, int textureSize);
double UnitRangeFromTexCoord(double u, int textureSize);

glm::dvec2 TransmittanceUvFromRMu(const Params& params, double r, double mu);
void RMuFromTransmittanceUv(const Params& params, const glm::dvec2& uv, double& r, double& mu);

glm::dvec2 MultiScatteringUvFromRMuS(const Params& params, double r, double muS);
void RMuSFromMultiScatteringUv(const Params& params, const glm::dvec2& uv, double& r, double& muS);

/** Reference transmittance to the top boundary by direct integration (trapezoid). */
glm::dvec3 IntegrateTransmittance(const Params& params, double r, double mu, int steps = 500);

/** A baked table, row-major from the bottom row up (GL upload order), RGBA. */
struct Lut {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;

    glm::dvec3 Texel(int x, int y) const;
    /** GL-style bilinear sample with CLAMP_TO_EDGE. */
    glm::dvec3 Sample(const glm::dvec2& uv) const;
};

Lut BakeTransmittance(const Params& params);

/** Transmittance from the baked table, 0 when the ray from (r, mu) hits the ground. */
glm::dvec3 LookupTransmittance(const Params& params, const Lut& transmittance, double r, double mu);

Lut BakeMultiScattering(const Params& params, const Lut& transmittance);

/**
 * FNV-1a over the catalog row's float bit patterns, the table sizes, kLutMappingVersion
 * and kBakerVersion. Stored in each LUT's KTX2 key/value data, so a stale bake is
 * detectable without re-running it.
 */
std::uint64_t ParamsHash(const BodyCatalog::AtmospherePhysical& physical);
std::string ParamsHashHex(const BodyCatalog::AtmospherePhysical& physical);

// File layout (relative to the resource root) and KTX2 key/value names.
std::string TransmittanceLutPath(const std::string& bodyId);
std::string MultiScatteringLutPath(const std::string& bodyId);
constexpr const char* kLutDirectory = "resource/atmosphere";
constexpr const char* kKeyParamsHash = "SolarSystem.atmosphere.paramsHash";
constexpr const char* kKeyMappingVersion = "SolarSystem.atmosphere.mappingVersion";

} // namespace AtmosphereModel

#endif // SOLARSYSTEM_ATMOSPHERE_MODEL_H
