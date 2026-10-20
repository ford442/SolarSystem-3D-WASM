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
        }
    }
    return closest;
}

std::optional<double> FacetRadius(const glm::dvec3& modelDirection, double boundingRadius,
                                  const std::vector<glm::vec3>& positions, const std::vector<unsigned>& indices) {
    const double start = 2.0 * boundingRadius;
    const auto hit = RaycastTriangles(modelDirection * start, -modelDirection, positions, indices);
    if (!hit || *hit > start) {
        return std::nullopt;
    }
    return start - *hit;
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
