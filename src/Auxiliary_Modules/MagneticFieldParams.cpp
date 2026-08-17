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
            p.toroidalStrength = 2.4f;
            p.torusRadiusScale = 1.55f;
            p.extentScale = 7.5f;
            p.seedCount = 24;
            p.samplesPerLine = 80;
            p.ribbonWidth = 0.045f;
            p.flowSpeed = 2.6f;
            p.color = {1.0f, 0.62f, 0.18f};
            break;
        case OrbitLayout::Body::Mercury:
            p.enabled = true;
            p.dipoleMoment = 0.22f;
            p.dipoleTiltDeg = 0.0f;
            p.extentScale = 2.4f;
            p.seedCount = 8;
            p.samplesPerLine = 48;
            p.ribbonWidth = 0.05f;
            p.flowSpeed = 0.45f;
            p.color = {0.55f, 0.7f, 0.85f};
            break;
        case OrbitLayout::Body::Earth:
            p.enabled = true;
            p.dipoleMoment = 1.0f;
            p.dipoleTiltDeg = 11.0f;
            p.extentScale = 4.2f;
            p.seedCount = 16;
            p.samplesPerLine = 64;
            p.ribbonWidth = 0.05f;
            p.flowSpeed = 1.1f;
            p.color = {0.28f, 0.78f, 1.0f};
            break;
        case OrbitLayout::Body::Jupiter:
            p.enabled = true;
            p.dipoleMoment = 1.6f;
            p.dipoleTiltDeg = 10.0f;
            p.extentScale = 5.5f;
            p.seedCount = 16;
            p.samplesPerLine = 64;
            p.ribbonWidth = 0.04f;
            p.flowSpeed = 1.6f;
            p.color = {0.55f, 0.85f, 1.0f};
            break;
        case OrbitLayout::Body::Saturn:
            p.enabled = true;
            p.dipoleMoment = 0.9f;
            p.dipoleTiltDeg = 0.0f;
            p.extentScale = 4.0f;
            p.seedCount = 12;
            p.samplesPerLine = 56;
            p.ribbonWidth = 0.04f;
            p.flowSpeed = 1.0f;
            p.color = {0.75f, 0.85f, 1.0f};
            break;
        case OrbitLayout::Body::Uranus:
            p.enabled = true;
            p.dipoleMoment = 0.85f;
            p.dipoleTiltDeg = 59.0f;
            p.extentScale = 3.8f;
            p.seedCount = 16;
            p.samplesPerLine = 64;
            p.ribbonWidth = 0.05f;
            p.flowSpeed = 0.95f;
            p.color = {0.45f, 0.95f, 0.85f};
            break;
        case OrbitLayout::Body::Neptune:
            p.enabled = true;
            p.dipoleMoment = 0.85f;
            p.dipoleTiltDeg = 47.0f;
            p.extentScale = 3.8f;
            p.seedCount = 12;
            p.samplesPerLine = 56;
            p.ribbonWidth = 0.05f;
            p.flowSpeed = 0.95f;
            p.color = {0.4f, 0.65f, 1.0f};
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
