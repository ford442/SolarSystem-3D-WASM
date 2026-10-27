#include "CameraNavigation.h"

#include <algorithm>
#include <cmath>

namespace CameraNavigation {

namespace {

constexpr double kMinNearKm = 0.25e-3;  // 25 cm
constexpr double kMinSpeedKm = 1.0e-6;  // 1 mm per second
constexpr double kDefaultMovementSpeed = 5.0;

bool IsCandidate(double radiiAway) {
    return radiiAway >= 0.0 && std::isfinite(radiiAway);
}

/** Unit direction of `v`, or +Y for the centre itself. */
glm::dvec3 RadialDirection(const glm::dvec3& v, double length) {
    return length > 0.0 ? v / length : glm::dvec3(0.0, 1.0, 0.0);
}

/** RaycastTriangles, also reporting the first index of the triangle hit. */
std::optional<double> ClosestHit(const glm::dvec3& origin, const glm::dvec3& direction,
                                 const std::vector<glm::vec3>& positions, const std::vector<unsigned>& indices,
                                 size_t& triangle) {
    // Möller–Trumbore, both faces: the camera may be under a facet that a spike in the
    // input put it beneath, and the floor still has to find it.
    std::optional<double> closest;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        if (indices[i] >= positions.size() || indices[i + 1] >= positions.size() ||
            indices[i + 2] >= positions.size()) {
            continue;
        }
        const glm::dvec3 a(positions[indices[i]]);
        const glm::dvec3 ab = glm::dvec3(positions[indices[i + 1]]) - a;
        const glm::dvec3 ac = glm::dvec3(positions[indices[i + 2]]) - a;
        const glm::dvec3 p = glm::cross(direction, ac);
        const double det = glm::dot(ab, p);
        if (std::abs(det) < 1e-18) {
            continue;
        }
        const double inverse = 1.0 / det;
        const glm::dvec3 s = origin - a;
        const double u = glm::dot(s, p) * inverse;
        if (u < 0.0 || u > 1.0) {
            continue;
        }
        const glm::dvec3 q = glm::cross(s, ab);
        const double v = glm::dot(direction, q) * inverse;
        if (v < 0.0 || u + v > 1.0) {
            continue;
        }
        const double t = glm::dot(ac, q) * inverse;
        if (t >= 0.0 && (!closest || t < *closest)) {
            closest = t;
            triangle = i;
        }
    }
    return closest;
}

} // namespace

glm::dvec3 ToModel(const Body& body, const glm::dvec3& world) {
    return glm::inverse(body.shape) * (world - body.center);
}

double WorldScaleAlong(const Body& body, const glm::dvec3& modelDirection) {
    return glm::length(body.shape * modelDirection);
}

double Altitude(const Body& body, const glm::dvec3& eye, double surfaceModelRadius) {
    const glm::dvec3 model = ToModel(body, eye);
    const double r = glm::length(model);
    return (r - surfaceModelRadius) * WorldScaleAlong(body, RadialDirection(model, r));
}

glm::dvec3 Floor(const Body& body, const glm::dvec3& eye, double surfaceModelRadius, double margin) {
    const glm::dvec3 model = ToModel(body, eye);
    const double r = glm::length(model);
    const glm::dvec3 up = RadialDirection(model, r);
    const double scale = WorldScaleAlong(body, up);
    if (scale <= 0.0 || (r - surfaceModelRadius) * scale >= margin) {
        return eye;
    }
    return body.center + body.shape * (up * (surfaceModelRadius + margin / scale));
}

std::optional<double> RaycastTriangles(const glm::dvec3& origin, const glm::dvec3& direction,
                                       const std::vector<glm::vec3>& positions,
                                       const std::vector<unsigned>& indices) {
    size_t triangle = 0;
    return ClosestHit(origin, direction, positions, indices, triangle);
}

std::optional<double> FacetRadius(const glm::dvec3& modelDirection, double boundingRadius,
                                  const std::vector<glm::vec3>& positions, const std::vector<unsigned>& indices) {
    const auto hit = FacetAlong(modelDirection, boundingRadius, positions, indices);
    return hit ? std::optional<double>(hit->radius) : std::nullopt;
}

std::optional<FacetHit> FacetAlong(const glm::dvec3& modelDirection, double boundingRadius,
                                   const std::vector<glm::vec3>& positions, const std::vector<unsigned>& indices) {
    const double start = 2.0 * boundingRadius;
    size_t triangle = 0;
    const auto hit = ClosestHit(modelDirection * start, -modelDirection, positions, indices, triangle);
    if (!hit || *hit > start) {
        return std::nullopt;
    }
    const glm::dvec3 a(positions[indices[triangle]]);
    glm::dvec3 normal = glm::cross(glm::dvec3(positions[indices[triangle + 1]]) - a,
                                   glm::dvec3(positions[indices[triangle + 2]]) - a);
    const double length = glm::length(normal);
    normal = length > 0.0 ? normal / length : modelDirection;
    return FacetHit{start - *hit, glm::dot(normal, modelDirection) < 0.0 ? -normal : normal};
}

glm::dvec3 NormalToWorld(const Body& body, const glm::dvec3& modelNormal) {
    const glm::dvec3 world = glm::transpose(glm::inverse(body.shape)) * modelNormal;
    const double length = glm::length(world);
    return length > 0.0 ? world / length : modelNormal;
}

ClipPlanes ChooseClipPlanes(bool trueScale, double altitude, double kmPerSceneUnit) {
    if (!trueScale) {
        return {kCompressedNear, kCompressedFar, 1.0f};
    }
    const double minNear = kMinNearKm / kmPerSceneUnit;
    const double nearPlane = std::clamp(0.5 * altitude, minNear, static_cast<double>(kCompressedNear));
    return {static_cast<float>(nearPlane), kRealisticFar, static_cast<float>(1.0 / nearPlane)};
}

double SpeedScale(bool trueScale, double altitude, double kmPerSceneUnit, double sceneUnitsPerAu) {
    if (!trueScale) {
        return 1.0;
    }
    return std::clamp(altitude, kMinSpeedKm / kmPerSceneUnit, sceneUnitsPerAu) / kDefaultMovementSpeed;
}

bool CoRotates(bool current, double altitudeRadii) {
    return altitudeRadii < (current ? kHoverReleaseRadii : kHoverRadii);
}

glm::dmat3 RotationBetween(const glm::dmat3& before, const glm::dmat3& after) {
    // after = R * before when the two differ by a turn alone; a scale change (the two scale
    // modes size a body differently) is divided out column by column.
    const glm::dmat3 delta = after * glm::inverse(before);
    glm::dmat3 rotation = delta;
    for (int c = 0; c < 3; ++c) {
        const double length = glm::length(delta[c]);
        rotation[c] = length > 0.0 ? delta[c] / length : glm::dvec3(c == 0, c == 1, c == 2);
    }
    // Columns of a rotation are orthonormal: Gram–Schmidt the near-rotation the float model
    // matrices leave (their rounding is ~1e-7), so the camera's orientation stays orthonormal.
    glm::dvec3 x = glm::normalize(rotation[0]);
    glm::dvec3 y = rotation[1] - x * glm::dot(rotation[1], x);
    y = glm::normalize(y);
    return glm::dmat3(x, y, glm::cross(x, y));
}

glm::dvec3 CarryEye(const Body& before, const Body& after, const glm::dvec3& eye, bool coRotate,
                    glm::dmat3* rotation) {
    if (rotation) {
        *rotation = glm::dmat3(1.0);
    }
    if (!coRotate) {
        return eye + (after.center - before.center);
    }
    if (rotation) {
        *rotation = RotationBetween(before.shape, after.shape);
    }
    // Same point in the drawn frame: the offset in model space is unchanged, so the eye is
    // placed by the new matrix. Frame to frame this telescopes: it never drifts from the
    // surface that is drawn, whatever the matrices' float rounding does to their scale.
    return after.center + after.shape * ToModel(before, eye);
}

int ChooseAnchor(int current, const std::vector<double>& radiiAway, bool pinned) {
    const int count = static_cast<int>(radiiAway.size());
    const bool currentValid = current >= 0 && current < count && IsCandidate(radiiAway[current]);
    if (pinned && currentValid) {
        return current;
    }
    int best = -1;
    for (int i = 0; i < count; ++i) {
        if (IsCandidate(radiiAway[i]) && (best < 0 || radiiAway[i] < radiiAway[best])) {
            best = i;
        }
    }
    if (currentValid && radiiAway[current] <= kAnchorReleaseRadii) {
        if (best != current && radiiAway[best] < kAnchorSwitchRatio * radiiAway[current]) {
            return best;
        }
        return current;
    }
    return best >= 0 && radiiAway[best] < kAnchorEngageRadii ? best : -1;
}

} // namespace CameraNavigation
