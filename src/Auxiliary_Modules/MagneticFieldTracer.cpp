#include "MagneticFieldTracer.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace MagneticFieldTracer {
namespace {

constexpr float kMinB = 1.0e-7f;
constexpr float kStartRadius = 1.08f;
constexpr float kInnerStop = 1.02f;
constexpr float kStep = 0.045f;

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

glm::vec3 NormalizeB(const glm::vec3& b) {
    const float len = glm::length(b);
    if (len < kMinB) {
        return glm::vec3(0.0f);
    }
    return b / len;
}

glm::vec3 Rk4Step(const glm::vec3& p, float h, const MagneticFieldParams& params) {
    const auto deriv = [&](const glm::vec3& x) { return NormalizeB(EvaluateB(x, params)); };
    const glm::vec3 k1 = deriv(p);
    if (glm::dot(k1, k1) < 1.0e-12f) {
        return p;
    }
    const glm::vec3 k2 = deriv(p + 0.5f * h * k1);
    const glm::vec3 k3 = deriv(p + 0.5f * h * k2);
    const glm::vec3 k4 = deriv(p + h * k3);
    return p + (h / 6.0f) * (k1 + 2.0f * k2 + 2.0f * k3 + k4);
}

void TraceDirection(const glm::vec3& seed, float direction, const MagneticFieldParams& params,
                    std::vector<MagneticFieldSample>& out) {
    glm::vec3 p = seed;
    const int maxSteps = std::max(8, params.samplesPerLine);
    const float h = kStep * direction;
    const float rMax = std::max(2.0f, params.extentScale);

    for (int i = 0; i < maxSteps; ++i) {
        const glm::vec3 next = Rk4Step(p, h, params);
        if (!std::isfinite(next.x) || !std::isfinite(next.y) || !std::isfinite(next.z)) {
            break;
        }
        const float r = glm::length(next);
        if (r < kInnerStop || r > rMax) {
            break;
        }
        if (glm::length(next - p) < 1.0e-5f) {
            break;
        }
        p = next;
        MagneticFieldSample sample;
        sample.position = p;
        out.push_back(sample);
    }
}

} // namespace

glm::vec3 EvaluateB(const glm::vec3& position, const MagneticFieldParams& params) {
    return DipoleB(position, params.dipoleMoment) + ToroidalB(position, params);
}

std::vector<MagneticFieldLine> Trace(const MagneticFieldParams& params) {
    std::vector<MagneticFieldLine> lines;
    if (!params.enabled || params.seedCount <= 0) {
        return lines;
    }

    const int seeds = std::max(4, params.seedCount);
    const int lonCount = std::max(4, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(seeds)))));
    const int latCount = std::max(2, (seeds + lonCount - 1) / lonCount);

    for (int j = 0; j < latCount; ++j) {
        const float latFrac = (latCount == 1) ? 0.0f : static_cast<float>(j) / static_cast<float>(latCount - 1);
        const float lat = glm::mix(-68.0f, 68.0f, latFrac) * (static_cast<float>(M_PI) / 180.0f);
        for (int i = 0; i < lonCount; ++i) {
            if (static_cast<int>(lines.size()) >= seeds) {
                break;
            }
            const float lon = (static_cast<float>(i) / static_cast<float>(lonCount)) * glm::two_pi<float>();
            const glm::vec3 seed(
                kStartRadius * std::cos(lat) * std::cos(lon),
                kStartRadius * std::sin(lat),
                kStartRadius * std::cos(lat) * std::sin(lon));

            std::vector<MagneticFieldSample> backward;
            TraceDirection(seed, -1.0f, params, backward);
            std::reverse(backward.begin(), backward.end());

            MagneticFieldLine line;
            line.samples = std::move(backward);
            MagneticFieldSample origin;
            origin.position = seed;
            line.samples.push_back(origin);
            TraceDirection(seed, 1.0f, params, line.samples);

            if (line.samples.size() < 3) {
                continue;
            }

            float accum = 0.0f;
            line.samples[0].arcLength = 0.0f;
            for (std::size_t s = 1; s < line.samples.size(); ++s) {
                accum += glm::length(line.samples[s].position - line.samples[s - 1].position);
                line.samples[s].arcLength = accum;
            }
            if (accum > 1.0e-5f) {
                for (auto& sample : line.samples) {
                    sample.arcLength /= accum;
                }
            }
            lines.push_back(std::move(line));
        }
    }

    return lines;
}

} // namespace MagneticFieldTracer
