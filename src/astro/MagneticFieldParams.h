#pragma once

#include "../Solar_System/OrbitLayout.h"
#include <glm/vec3.hpp>

/**
 * Visual / educational magnetic-field parameters for a celestial body.
 *
 * Values are normalized for ribbon tracing and shader animation — not SI
 * magnetosphere physics. Field names used by the tracer/renderer map from the
 * data-model issue as: axisTiltDeg → dipoleTiltDeg, fieldStrength →
 * dipoleMoment, fieldExtentMultiplier → extentScale.
 *
 * SampleField / Trace use unit magnetic space (radius = 1, axis = +Y).
 * dipoleTiltDeg is applied as a model-matrix Rx at draw time, not baked
 * into traced vertices.
 */
struct MagneticFieldParams {
    bool enabled = false;
    float dipoleMoment = 0.0f;
    float dipoleTiltDeg = 0.0f;
    float toroidalStrength = 0.0f;
    float torusRadiusScale = 1.4f;
    float extentScale = 4.0f;
    int seedCount = 16;
    int samplesPerLine = 96;
    float ribbonWidth = 0.06f; // fraction of body radius
    float flowSpeed = 1.0f;
    float opacity = 0.85f;
    glm::vec3 color = {0.35f, 0.75f, 1.0f};
};

namespace MagneticFieldCatalog {

/** Quality-independent educational defaults (attach these to scene bodies). */
MagneticFieldParams IntrinsicParamsForBody(OrbitLayout::Body body);

/** Intrinsic params plus seed/sample scaling and quality enable gates. */
MagneticFieldParams ParamsForBody(OrbitLayout::Body body, int qualityPreset);

} // namespace MagneticFieldCatalog
