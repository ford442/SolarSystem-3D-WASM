#include "MagneticFieldParams.h"
#include <algorithm>

namespace MagneticFieldCatalog {
namespace {

int ScaleSeeds(int base, int qualityPreset) {
    if (qualityPreset <= 0) {
        return std::max(8, base / 3);
    }
    if (qualityPreset == 1) {
        return base;
    }
    return base + base / 2;
}

int ScaleSamples(int base, int qualityPreset) {
    if (qualityPreset <= 0) {
        return std::max(32, base / 2);
    }
    if (qualityPreset == 1) {
        return base;
    }
    return base + 16;
}

} // namespace

MagneticFieldParams IntrinsicParamsForBody(OrbitLayout::Body body) {
    MagneticFieldParams p;

    switch (body) {
        case OrbitLayout::Body::Sun:
            p.enabled = true;
            p.dipoleMoment = 0.55f;
            p.dipoleTiltDeg = 7.25f;
            p.toroidalStrength = 2.85f;
            p.torusRadiusScale = 1.62f;
            p.extentScale = 8.2f;
            p.seedCount = 28;
            p.samplesPerLine = 88;
            p.ribbonWidth = 0.055f;
            p.flowSpeed = 3.1f;
            p.opacity = 0.92f;
            p.color = {1.0f, 0.52f, 0.10f};
            break;
        case OrbitLayout::Body::Mercury:
            p.enabled = true;
            p.dipoleMoment = 0.22f;
            p.dipoleTiltDeg = 0.0f;
            p.extentScale = 2.4f;
            p.seedCount = 8;
            p.samplesPerLine = 48;
            p.ribbonWidth = 0.04f;
            p.flowSpeed = 0.45f;
            p.opacity = 0.70f;
            p.color = {0.60f, 0.72f, 0.82f};
            break;
        case OrbitLayout::Body::Earth:
            p.enabled = true;
            p.dipoleMoment = 1.0f;
            p.dipoleTiltDeg = 11.0f;
            p.extentScale = 4.2f;
            p.seedCount = 18;
            p.samplesPerLine = 64;
            p.ribbonWidth = 0.055f;
            p.flowSpeed = 1.15f;
            p.color = {0.22f, 0.80f, 1.0f};
            break;
        case OrbitLayout::Body::Jupiter:
            p.enabled = true;
            p.dipoleMoment = 1.6f;
            p.dipoleTiltDeg = 10.0f;
            p.extentScale = 6.2f;
            p.seedCount = 20;
            p.samplesPerLine = 72;
            p.ribbonWidth = 0.048f;
            p.flowSpeed = 1.7f;
            p.color = {0.62f, 0.88f, 1.0f};
            break;
        case OrbitLayout::Body::Saturn:
            p.enabled = true;
            p.dipoleMoment = 0.9f;
            p.dipoleTiltDeg = 0.0f;
            p.extentScale = 4.0f;
            p.seedCount = 12;
            p.samplesPerLine = 56;
            p.ribbonWidth = 0.042f;
            p.flowSpeed = 1.0f;
            p.color = {0.85f, 0.82f, 0.65f};
            break;
        case OrbitLayout::Body::Uranus:
            p.enabled = true;
            p.dipoleMoment = 0.85f;
            p.dipoleTiltDeg = 59.0f;
            p.extentScale = 4.3f;
            p.seedCount = 20;
            p.samplesPerLine = 72;
            p.ribbonWidth = 0.07f;
            p.flowSpeed = 1.05f;
            p.opacity = 0.92f;
            p.color = {0.18f, 1.0f, 0.72f};
            break;
        case OrbitLayout::Body::Neptune:
            p.enabled = true;
            p.dipoleMoment = 0.85f;
            p.dipoleTiltDeg = 47.0f;
            p.extentScale = 3.8f;
            p.seedCount = 12;
            p.samplesPerLine = 56;
            p.ribbonWidth = 0.055f;
            p.flowSpeed = 0.95f;
            p.color = {0.28f, 0.48f, 1.0f};
            break;
        default:
            p.enabled = false;
            break;
    }

    return p;
}

MagneticFieldParams ParamsForBody(OrbitLayout::Body body, int qualityPreset) {
    MagneticFieldParams p = IntrinsicParamsForBody(body);
    if (!p.enabled) {
        return p;
    }

    const int q = qualityPreset;
    p.seedCount = ScaleSeeds(p.seedCount, q);
    p.samplesPerLine = ScaleSamples(p.samplesPerLine, q);

    if (q <= 0 && body != OrbitLayout::Body::Sun) {
        p.enabled = false;
    } else if (q == 1 && (body == OrbitLayout::Body::Mercury || body == OrbitLayout::Body::Saturn ||
                          body == OrbitLayout::Body::Neptune)) {
        p.enabled = false;
    }
    return p;
}

} // namespace MagneticFieldCatalog
