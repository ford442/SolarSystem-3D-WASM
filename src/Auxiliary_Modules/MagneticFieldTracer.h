#pragma once

#include "MagneticFieldParams.h"
#include <glm/vec3.hpp>
#include <vector>

struct MagneticFieldSample {
    glm::vec3 position{0.0f};
    float arcLength = 0.0f;
};

struct MagneticFieldLine {
    std::vector<MagneticFieldSample> samples;
};

namespace MagneticFieldTracer {

/** Evaluate B in the magnetic-axis frame (Y = magnetic axis, body radius = 1). */
glm::vec3 EvaluateB(const glm::vec3& position, const MagneticFieldParams& params);

/**
 * Trace dipole + optional toroidal field lines from a unit-radius body.
 * Returns polylines in local magnetic space (Y-up magnetic axis).
 */
std::vector<MagneticFieldLine> Trace(const MagneticFieldParams& params);

} // namespace MagneticFieldTracer
