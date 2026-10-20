#include "MagneticFieldModel.h"

#include <cmath>
#include <glm/gtc/constants.hpp>

namespace MagneticFieldModel {
namespace {

glm::vec3 DipoleB(const glm::vec3& r, float moment) {
    const glm::vec3 m(0.0f, moment, 0.0f);
    const float r2 = glm::dot(r, r);
    if (r2 < 1.0e-8f) {
        return glm::vec3(0.0f);
    }
    const float r1 = std::sqrt(r2);
    const float r5 = r2 * r2 * r1;
    return (3.0f * glm::dot(m, r) * r - m * r2) / r5;
}

glm::vec3 ToroidalB(const glm::vec3& r, const MagneticFieldParams& params) {
    if (params.toroidalStrength <= 0.0f) {
        return glm::vec3(0.0f);
    }
    const float rho = std::sqrt(r.x * r.x + r.z * r.z);
    if (rho < 1.0e-5f) {
        return glm::vec3(0.0f);
    }
    const float dr = rho - params.torusRadiusScale;
    const float w = 0.38f;
    const float h = 0.50f;
    const float envelope = std::exp(-(dr * dr) / (w * w)) * std::exp(-(r.y * r.y) / (h * h));
    const glm::vec3 ePhi(-r.z / rho, 0.0f, r.x / rho);
    return ePhi * (params.toroidalStrength * envelope);
}

} // namespace

glm::vec3 SampleField(const glm::vec3& magneticPos, const MagneticFieldParams& params) {
    return DipoleB(magneticPos, params.dipoleMoment) + ToroidalB(magneticPos, params);
}

glm::mat3 TiltMatrix(float dipoleTiltDeg) {
    const float rad = dipoleTiltDeg * (glm::pi<float>() / 180.0f);
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, c, s), glm::vec3(0.0f, -s, c));
}

} // namespace MagneticFieldModel
