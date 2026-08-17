#include "MagneticFieldTracer.h"

#include "MagneticFieldModel.h"
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
constexpr int kMaxSeeds = 64;
constexpr int kMinSamplesPerLine = 8;
constexpr int kMaxSamplesPerLine = 256;

constexpr float kColatitudesDeg[] = {18.0f, 35.0f, 52.0f, 68.0f};
constexpr int kRingCount = static_cast<int>(sizeof(kColatitudesDeg) / sizeof(kColatitudesDeg[0]));

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
    const int maxSteps = std::clamp(params.samplesPerLine, kMinSamplesPerLine, kMaxSamplesPerLine);
    const float h = kStep * direction;
    const float rMax = std::max(2.0f, params.extentScale);

    for (int i = 0; i < maxSteps; ++i) {
        if (glm::length(EvaluateB(p, params)) < kMinB) {
            break;
        }
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
    return MagneticFieldModel::SampleField(position, params);
}

std::vector<MagneticFieldLine> Trace(const MagneticFieldParams& params) {
    std::vector<MagneticFieldLine> lines;
    if (!params.enabled || params.seedCount <= 0) {
        return lines;
    }

    MagneticFieldParams clamped = params;
    clamped.seedCount = std::clamp(params.seedCount, 0, kMaxSeeds);
    clamped.samplesPerLine = std::clamp(params.samplesPerLine, kMinSamplesPerLine, kMaxSamplesPerLine);

    const int seeds = clamped.seedCount;
    const int lonCount = std::max(3, static_cast<int>(std::ceil(static_cast<float>(seeds) / static_cast<float>(kRingCount))));

    for (int ring = 0; ring < kRingCount; ++ring) {
        const float colat = kColatitudesDeg[ring] * (static_cast<float>(M_PI) / 180.0f);
        const float sinC = std::sin(colat);
        const float cosC = std::cos(colat);
        for (int i = 0; i < lonCount; ++i) {
            if (static_cast<int>(lines.size()) >= seeds) {
                break;
            }
            const float lon = (static_cast<float>(i) / static_cast<float>(lonCount)) * glm::two_pi<float>();
            const glm::vec3 seed(kStartRadius * sinC * std::cos(lon), kStartRadius * cosC,
                                 kStartRadius * sinC * std::sin(lon));

            std::vector<MagneticFieldSample> backward;
            TraceDirection(seed, -1.0f, clamped, backward);
            std::reverse(backward.begin(), backward.end());

            MagneticFieldLine line;
            line.samples = std::move(backward);
            MagneticFieldSample origin;
            origin.position = seed;
            line.samples.push_back(origin);
            TraceDirection(seed, 1.0f, clamped, line.samples);

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
        if (static_cast<int>(lines.size()) >= seeds) {
            break;
        }
    }

    return lines;
}

} // namespace MagneticFieldTracer
