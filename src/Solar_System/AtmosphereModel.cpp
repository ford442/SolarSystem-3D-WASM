#include "AtmosphereModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace AtmosphereModel {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kIsotropicPhase = 1.0 / (4.0 * kPi);

// Multi-scattering quadrature: kMsDirections^2 directions over the sphere, kMsSteps
// samples per ray. Offline, so generous; changing either changes the output, so bump
// kBakerVersion with it.
constexpr int kMsDirections = 16;
constexpr int kMsSteps = 40;

glm::dvec3 ToVec(const BodyCatalog::Rgb& rgb) {
    return {rgb.r, rgb.g, rgb.b};
}

glm::dvec3 ExpNeg(const glm::dvec3& x) {
    return {std::exp(-x.x), std::exp(-x.y), std::exp(-x.z)};
}

// (S - S * exp(-sigma_t * dt)) / sigma_t per channel: the exact integral of a constant
// source S attenuated across one step (Hillaire 2015). Falls back to S * dt as sigma_t -> 0.
glm::dvec3 IntegrateStep(const glm::dvec3& source, const glm::dvec3& extinction, const glm::dvec3& stepTransmittance,
                         double dt) {
    glm::dvec3 result;
    for (int c = 0; c < 3; ++c) {
        result[c] = extinction[c] > 1e-12 ? (source[c] - source[c] * stepTransmittance[c]) / extinction[c]
                                          : source[c] * dt;
    }
    return result;
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
}

void HashFloat(std::uint64_t& hash, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    HashBytes(hash, &bits, sizeof(bits));
}

void HashInt(std::uint64_t& hash, std::int32_t value) {
    HashBytes(hash, &value, sizeof(value));
}

void HashRgb(std::uint64_t& hash, const BodyCatalog::Rgb& rgb) {
    HashFloat(hash, rgb.r);
    HashFloat(hash, rgb.g);
    HashFloat(hash, rgb.b);
}

} // namespace

Params FromCatalog(const BodyCatalog::AtmospherePhysical& p) {
    const double s = p.thicknessScale > 0.0f ? p.thicknessScale : 1.0;
    Params params;
    params.bottomRadius = p.groundRadiusKm;
    params.topRadius = p.groundRadiusKm + (static_cast<double>(p.topRadiusKm) - p.groundRadiusKm) * s;
    params.rayleighScattering = ToVec(p.rayleighScattering) / s;
    params.rayleighScaleHeight = p.rayleighScaleHeightKm * s;
    params.mieScattering = ToVec(p.mieScattering) / s;
    params.mieExtinction = ToVec(p.mieExtinction) / s;
    params.mieScaleHeight = p.mieScaleHeightKm * s;
    params.miePhaseG = p.miePhaseG;
    params.absorptionExtinction = ToVec(p.absorptionExtinction) / s;
    params.absorptionCenter = p.absorptionCenterKm * s;
    params.absorptionHalfWidth = p.absorptionHalfWidthKm * s;
    params.groundAlbedo = ToVec(p.groundAlbedo);
    return params;
}

Medium SampleMedium(const Params& params, double altitudeKm) {
    const double h = std::max(altitudeKm, 0.0);
    const double rayleighDensity = std::exp(-h / params.rayleighScaleHeight);
    const double mieDensity = std::exp(-h / params.mieScaleHeight);
    const double absorptionDensity =
        std::max(0.0, 1.0 - std::abs(h - params.absorptionCenter) / params.absorptionHalfWidth);

    Medium medium;
    medium.rayleighScattering = params.rayleighScattering * rayleighDensity;
    medium.mieScattering = params.mieScattering * mieDensity;
    medium.scattering = medium.rayleighScattering + medium.mieScattering;
    medium.extinction = medium.rayleighScattering + params.mieExtinction * mieDensity +
                        params.absorptionExtinction * absorptionDensity;
    return medium;
}

double DistanceToTopBoundary(const Params& params, double r, double mu) {
    const double discriminant = r * r * (mu * mu - 1.0) + params.topRadius * params.topRadius;
    return std::max(0.0, -r * mu + std::sqrt(std::max(discriminant, 0.0)));
}

double DistanceToBottomBoundary(const Params& params, double r, double mu) {
    const double discriminant = r * r * (mu * mu - 1.0) + params.bottomRadius * params.bottomRadius;
    return std::max(0.0, -r * mu - std::sqrt(std::max(discriminant, 0.0)));
}

bool RayIntersectsGround(const Params& params, double r, double mu) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + params.bottomRadius * params.bottomRadius >= 0.0;
}

double TexCoordFromUnitRange(double x, int textureSize) {
    return 0.5 / textureSize + x * (1.0 - 1.0 / textureSize);
}

double UnitRangeFromTexCoord(double u, int textureSize) {
    return (u - 0.5 / textureSize) / (1.0 - 1.0 / textureSize);
}

glm::dvec2 TransmittanceUvFromRMu(const Params& params, double r, double mu) {
    const double bottom = params.bottomRadius;
    const double top = params.topRadius;
    // Distance to the top boundary for a horizontal ray at the ground.
    const double H = std::sqrt(top * top - bottom * bottom);
    // Distance to the horizon.
    const double rho = std::sqrt(std::max((r - bottom) * (r + bottom), 0.0));
    const double d = DistanceToTopBoundary(params, r, mu);
    const double dMin = top - r;
    const double dMax = rho + H;
    const double xMu = dMax > dMin ? (d - dMin) / (dMax - dMin) : 0.0;
    const double xR = rho / H;
    return {TexCoordFromUnitRange(std::clamp(xMu, 0.0, 1.0), kTransmittanceWidth),
            TexCoordFromUnitRange(std::clamp(xR, 0.0, 1.0), kTransmittanceHeight)};
}

void RMuFromTransmittanceUv(const Params& params, const glm::dvec2& uv, double& r, double& mu) {
    const double bottom = params.bottomRadius;
    const double top = params.topRadius;
    const double xMu = UnitRangeFromTexCoord(uv.x, kTransmittanceWidth);
    const double xR = UnitRangeFromTexCoord(uv.y, kTransmittanceHeight);
    const double H = std::sqrt(top * top - bottom * bottom);
    const double rho = H * xR;
    r = std::sqrt(rho * rho + bottom * bottom);
    const double dMin = top - r;
    const double dMax = rho + H;
    const double d = dMin + xMu * (dMax - dMin);
    mu = d == 0.0 ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = std::clamp(mu, -1.0, 1.0);
}

glm::dvec2 MultiScatteringUvFromRMuS(const Params& params, double r, double muS) {
    const double x = std::clamp(muS * 0.5 + 0.5, 0.0, 1.0);
    const double y = std::clamp((r - params.bottomRadius) / (params.topRadius - params.bottomRadius), 0.0, 1.0);
    return {TexCoordFromUnitRange(x, kMultiScatteringSize), TexCoordFromUnitRange(y, kMultiScatteringSize)};
}

void RMuSFromMultiScatteringUv(const Params& params, const glm::dvec2& uv, double& r, double& muS) {
    const double x = UnitRangeFromTexCoord(uv.x, kMultiScatteringSize);
    const double y = UnitRangeFromTexCoord(uv.y, kMultiScatteringSize);
    muS = std::clamp(x * 2.0 - 1.0, -1.0, 1.0);
    r = params.bottomRadius + std::clamp(y, 0.0, 1.0) * (params.topRadius - params.bottomRadius);
}

glm::dvec3 IntegrateTransmittance(const Params& params, double r, double mu, int steps) {
    const double length = DistanceToTopBoundary(params, r, mu);
    const double dx = length / steps;
    glm::dvec3 opticalDepth(0.0);
    for (int i = 0; i <= steps; ++i) {
        const double t = i * dx;
        const double ri = std::sqrt(t * t + 2.0 * r * mu * t + r * r);
        const double weight = (i == 0 || i == steps) ? 0.5 : 1.0;
        opticalDepth += SampleMedium(params, ri - params.bottomRadius).extinction * (weight * dx);
    }
    return ExpNeg(opticalDepth);
}

glm::dvec3 Lut::Texel(int x, int y) const {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4;
    return {rgba[at], rgba[at + 1], rgba[at + 2]};
}

glm::dvec3 Lut::Sample(const glm::dvec2& uv) const {
    const double px = uv.x * width - 0.5;
    const double py = uv.y * height - 0.5;
    const double fx0 = std::floor(px);
    const double fy0 = std::floor(py);
    const double fx = px - fx0;
    const double fy = py - fy0;
    const int x0 = static_cast<int>(fx0);
    const int y0 = static_cast<int>(fy0);
    const glm::dvec3 bottom = glm::mix(Texel(x0, y0), Texel(x0 + 1, y0), fx);
    const glm::dvec3 topRow = glm::mix(Texel(x0, y0 + 1), Texel(x0 + 1, y0 + 1), fx);
    return glm::mix(bottom, topRow, fy);
}

Lut BakeTransmittance(const Params& params) {
    Lut lut;
    lut.width = kTransmittanceWidth;
    lut.height = kTransmittanceHeight;
    lut.rgba.resize(static_cast<std::size_t>(lut.width) * lut.height * 4);
    for (int y = 0; y < lut.height; ++y) {
        for (int x = 0; x < lut.width; ++x) {
            double r, mu;
            RMuFromTransmittanceUv(params, {(x + 0.5) / lut.width, (y + 0.5) / lut.height}, r, mu);
            const glm::dvec3 t = IntegrateTransmittance(params, r, mu);
            const std::size_t at = (static_cast<std::size_t>(y) * lut.width + x) * 4;
            lut.rgba[at + 0] = static_cast<float>(t.x);
            lut.rgba[at + 1] = static_cast<float>(t.y);
            lut.rgba[at + 2] = static_cast<float>(t.z);
            lut.rgba[at + 3] = 1.0f;
        }
    }
    return lut;
}

glm::dvec3 LookupTransmittance(const Params& params, const Lut& transmittance, double r, double mu) {
    if (RayIntersectsGround(params, r, mu)) {
        return glm::dvec3(0.0);
    }
    return transmittance.Sample(TransmittanceUvFromRMu(params, r, mu));
}

Lut BakeMultiScattering(const Params& params, const Lut& transmittance) {
    Lut lut;
    lut.width = kMultiScatteringSize;
    lut.height = kMultiScatteringSize;
    lut.rgba.resize(static_cast<std::size_t>(lut.width) * lut.height * 4);

    for (int y = 0; y < lut.height; ++y) {
        for (int x = 0; x < lut.width; ++x) {
            double r, muS;
            RMuSFromMultiScatteringUv(params, {(x + 0.5) / lut.width, (y + 0.5) / lut.height}, r, muS);
            // Keep the sample point strictly inside the shell so its rays have a length.
            r = std::clamp(r, params.bottomRadius + 1e-3, params.topRadius - 1e-3);
            const glm::dvec3 position(0.0, r, 0.0);
            const glm::dvec3 sunDirection(0.0, muS, std::sqrt(std::max(0.0, 1.0 - muS * muS)));

            glm::dvec3 luminanceSum(0.0);    // second-order radiance, isotropic phase
            glm::dvec3 transferSum(0.0);     // f_ms: fraction re-scattered toward the point
            for (int a = 0; a < kMsDirections; ++a) {
                for (int b = 0; b < kMsDirections; ++b) {
                    // Stratified uniform directions over the sphere.
                    const double theta = 2.0 * kPi * (a + 0.5) / kMsDirections;
                    const double cosPhi = 1.0 - 2.0 * (b + 0.5) / kMsDirections;
                    const double sinPhi = std::sqrt(std::max(0.0, 1.0 - cosPhi * cosPhi));
                    const glm::dvec3 direction(std::cos(theta) * sinPhi, cosPhi, std::sin(theta) * sinPhi);
                    const double mu = direction.y;

                    const bool hitsGround = RayIntersectsGround(params, r, mu);
                    const double tMax = hitsGround ? DistanceToBottomBoundary(params, r, mu)
                                                   : DistanceToTopBoundary(params, r, mu);
                    const double dt = tMax / kMsSteps;

                    glm::dvec3 throughput(1.0), luminance(0.0), transfer(0.0);
                    for (int s = 0; s < kMsSteps; ++s) {
                        const glm::dvec3 sample = position + direction * ((s + 0.5) * dt);
                        const double sampleR = glm::length(sample);
                        const Medium medium = SampleMedium(params, sampleR - params.bottomRadius);
                        const double sampleMuS = glm::dot(sample / sampleR, sunDirection);
                        const glm::dvec3 sunTransmittance = LookupTransmittance(params, transmittance, sampleR, sampleMuS);
                        const glm::dvec3 stepTransmittance = ExpNeg(medium.extinction * dt);

                        const glm::dvec3 source = medium.scattering * kIsotropicPhase * sunTransmittance;
                        luminance += throughput * IntegrateStep(source, medium.extinction, stepTransmittance, dt);
                        transfer += throughput * IntegrateStep(medium.scattering, medium.extinction, stepTransmittance, dt);
                        throughput *= stepTransmittance;
                    }
                    if (hitsGround) {
                        // Lambertian bounce off the ground (or Venus's cloud deck).
                        const glm::dvec3 groundPoint = position + direction * tMax;
                        const glm::dvec3 normal = glm::normalize(groundPoint);
                        const double cosSun = glm::dot(normal, sunDirection);
                        if (cosSun > 0.0) {
                            const glm::dvec3 sunTransmittance =
                                LookupTransmittance(params, transmittance, params.bottomRadius, cosSun);
                            luminance += throughput * sunTransmittance * cosSun * params.groundAlbedo / kPi;
                        }
                    }
                    luminanceSum += luminance;
                    transferSum += transfer;
                }
            }

            // Mean over the sphere: sum * (4 pi / N) * isotropic phase.
            const double samples = static_cast<double>(kMsDirections) * kMsDirections;
            const glm::dvec3 secondOrder = luminanceSum / samples;
            const glm::dvec3 fms = transferSum / samples * (4.0 * kPi) * kIsotropicPhase;
            // Infinite series of isotropic bounces: L2 * (1 + f + f^2 + ...).
            glm::dvec3 psi;
            for (int c = 0; c < 3; ++c) {
                psi[c] = secondOrder[c] / (1.0 - std::min(fms[c], 0.999));
            }

            const std::size_t at = (static_cast<std::size_t>(y) * lut.width + x) * 4;
            lut.rgba[at + 0] = static_cast<float>(psi.x);
            lut.rgba[at + 1] = static_cast<float>(psi.y);
            lut.rgba[at + 2] = static_cast<float>(psi.z);
            lut.rgba[at + 3] = 1.0f;
        }
    }
    return lut;
}

std::uint64_t ParamsHash(const BodyCatalog::AtmospherePhysical& p) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    HashInt(hash, kLutMappingVersion);
    HashInt(hash, kBakerVersion);
    HashInt(hash, kTransmittanceWidth);
    HashInt(hash, kTransmittanceHeight);
    HashInt(hash, kMultiScatteringSize);
    HashFloat(hash, p.groundRadiusKm);
    HashFloat(hash, p.topRadiusKm);
    HashFloat(hash, p.thicknessScale);
    HashRgb(hash, p.rayleighScattering);
    HashFloat(hash, p.rayleighScaleHeightKm);
    HashRgb(hash, p.mieScattering);
    HashRgb(hash, p.mieExtinction);
    HashFloat(hash, p.mieScaleHeightKm);
    HashFloat(hash, p.miePhaseG);
    HashRgb(hash, p.absorptionExtinction);
    HashFloat(hash, p.absorptionCenterKm);
    HashFloat(hash, p.absorptionHalfWidthKm);
    HashRgb(hash, p.groundAlbedo);
    // exposure is a display setting applied in the shader; it does not change the LUTs.
    return hash;
}

std::string ParamsHashHex(const BodyCatalog::AtmospherePhysical& physical) {
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << ParamsHash(physical);
    return stream.str();
}

std::string TransmittanceLutPath(const std::string& bodyId) {
    return std::string(kLutDirectory) + "/" + bodyId + "_transmittance.ktx2";
}

std::string MultiScatteringLutPath(const std::string& bodyId) {
    return std::string(kLutDirectory) + "/" + bodyId + "_multiscatter.ktx2";
}

} // namespace AtmosphereModel
