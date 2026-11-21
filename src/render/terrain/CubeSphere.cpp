#include "CubeSphere.h"

#include <algorithm>
#include <cmath>

namespace Terrain {

namespace {

constexpr double kQuarterPi = 0.25 * 3.14159265358979323846;

/**
 * sin and cos of a small angle by their series, as terrain.vs evaluates them: a GPU's sin is
 * only good to an absolute ~1e-6, which is all of a deep tile's offset. Terms to b^11 keep
 * the relative error under 2e-9 up to π/4, the largest offset a level-0 tile has.
 */
float SmallSin(float b) {
    const float b2 = b * b;
    return b * (1.0f - b2 / 6.0f * (1.0f - b2 / 20.0f * (1.0f - b2 / 42.0f * (1.0f - b2 / 72.0f * (1.0f - b2 / 110.0f)))));
}

float SmallCos(float b) {
    const float b2 = b * b;
    return 1.0f - b2 / 2.0f * (1.0f - b2 / 12.0f * (1.0f - b2 / 30.0f * (1.0f - b2 / 56.0f * (1.0f - b2 / 90.0f))));
}

} // namespace

FaceBasis Basis(int face) {
    switch (face) {
        case 0: return {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}};
        case 1: return {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
        case 2: return {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}};
        case 3: return {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};
        case 4: return {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}};
        default: return {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}};
    }
}

TileExtent Extent(const TileKey& key) {
    const double size = 2.0 / static_cast<double>(1u << key.level);
    return {-1.0 + size * key.x, -1.0 + size * key.y, size};
}

glm::dvec3 Direction(int face, double s, double t) {
    const FaceBasis basis = Basis(face);
    return glm::normalize(basis.n + Warp(s) * basis.u + Warp(t) * basis.v);
}

FacePoint FromDirection(const glm::dvec3& direction) {
    const glm::dvec3 a = glm::abs(direction);
    int face;
    if (a.x >= a.y && a.x >= a.z) {
        face = direction.x >= 0.0 ? 0 : 1;
    } else if (a.y >= a.z) {
        face = direction.y >= 0.0 ? 2 : 3;
    } else {
        face = direction.z >= 0.0 ? 4 : 5;
    }
    const FaceBasis basis = Basis(face);
    const glm::dvec3 cube = direction / glm::dot(direction, basis.n);
    const double s = std::atan(glm::dot(cube, basis.u)) / kQuarterPi;
    const double t = std::atan(glm::dot(cube, basis.v)) / kQuarterPi;
    return {face, std::clamp(s, -1.0, 1.0), std::clamp(t, -1.0, 1.0)};
}

TileKey TileAt(const FacePoint& point, int level) {
    const double tiles = static_cast<double>(1u << level);
    const auto index = [tiles](double coordinate) {
        return static_cast<uint32_t>(std::clamp(std::floor((coordinate + 1.0) * 0.5 * tiles), 0.0, tiles - 1.0));
    };
    return {static_cast<uint8_t>(point.face), static_cast<uint8_t>(level), index(point.s), index(point.t)};
}

TileFrame MakeTileFrame(const TileKey& key, double meshRadius) {
    const TileExtent extent = Extent(key);
    const double sc = extent.s0 + 0.5 * extent.size;
    const double tc = extent.t0 + 0.5 * extent.size;
    const double ws = Warp(sc), wt = Warp(tc);
    TileFrame frame;
    frame.centerWarp = glm::vec2(static_cast<float>(ws), static_cast<float>(wt));
    frame.warpCorrection = glm::vec2(static_cast<float>(ws - static_cast<double>(frame.centerWarp.x)),
                                     static_cast<float>(wt - static_cast<double>(frame.centerWarp.y)));
    frame.centerCos = glm::vec2(static_cast<float>(std::cos(kQuarterPi * sc)), static_cast<float>(std::cos(kQuarterPi * tc)));
    frame.centerSin = glm::vec2(static_cast<float>(std::sin(kQuarterPi * sc)), static_cast<float>(std::sin(kQuarterPi * tc)));
    frame.size = static_cast<float>(extent.size);
    const FaceBasis basis = Basis(key.face);
    // From the float warps, exactly as the shader forms its reference point.
    const glm::dvec3 reference = basis.n + static_cast<double>(frame.centerWarp.x) * basis.u +
                                 static_cast<double>(frame.centerWarp.y) * basis.v;
    frame.centerModel = meshRadius * glm::normalize(reference);
    return frame;
}

glm::vec3 GridOffset(const TileFrame& frame, int face, glm::vec2 grid, float meshRadius, float heightModel) {
    const FaceBasis basis = Basis(face);
    const glm::vec3 n(basis.n), u(basis.u), v(basis.v);
    const glm::vec2 d = (grid / static_cast<float>(kTileQuads) - 0.5f) * frame.size;
    const glm::vec2 b = static_cast<float>(kQuarterPi) * d;
    const glm::vec2 sinB(SmallSin(b.x), SmallSin(b.y));
    const glm::vec2 cosB(SmallCos(b.x), SmallCos(b.y));
    const glm::vec2 cosAB = frame.centerCos * cosB - frame.centerSin * sinB;
    const glm::vec2 dw = sinB / (frame.centerCos * cosAB) + frame.warpCorrection;
    const glm::vec3 delta = dw.x * u + dw.y * v;
    const glm::vec3 reference = n + frame.centerWarp.x * u + frame.centerWarp.y * v;
    const float referenceLength2 = glm::dot(reference, reference);
    const float referenceLength = std::sqrt(referenceLength2);
    const float lift = 2.0f * glm::dot(reference, delta) + glm::dot(delta, delta); // |a|² - |ref|²
    const float length = std::sqrt(referenceLength2 + lift);
    const float inverseDifference = -lift / (length * referenceLength * (length + referenceLength)); // 1/|a| - 1/|ref|
    const glm::vec3 directionDifference = delta / length + reference * inverseDifference;
    const glm::vec3 direction = (reference + delta) / length;
    return meshRadius * directionDifference + heightModel * direction;
}

glm::dvec3 GridPointExact(const TileKey& key, double gx, double gy, double meshRadius, double heightModel) {
    const TileExtent extent = Extent(key);
    const double s = extent.s0 + gx / kTileQuads * extent.size;
    const double t = extent.t0 + gy / kTileQuads * extent.size;
    return (meshRadius + heightModel) * Direction(key.face, s, t);
}

} // namespace Terrain
