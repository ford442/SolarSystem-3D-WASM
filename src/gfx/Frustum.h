#ifndef SOLARSYSTEM_GFX_FRUSTUM_H
#define SOLARSYSTEM_GFX_FRUSTUM_H
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <limits>
#include <vector>

/** A sphere in the render frame (SpaceObject::GetBoundingSphere). */
struct BoundingSphere {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
};

/**
 * The four side planes of a perspective view-projection (Gribb–Hartmann), for culling
 * whole bodies. The near and far planes are left out on purpose: the side planes already
 * meet at the eye, so they reject everything behind the camera, and anything past the far
 * plane is clipped by GL anyway. Conservative: a sphere that touches the pyramid passes.
 */
class Frustum {
public:
    explicit Frustum(const glm::mat4& viewProjection) {
        const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]);
        const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]);
        const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);
        _planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1}; // left, right, bottom, top
        for (glm::vec4& plane : _planes) {
            plane /= glm::length(glm::vec3(plane));
        }
    }

    bool Intersects(const BoundingSphere& sphere) const {
        for (const glm::vec4& plane : _planes) {
            if (glm::dot(glm::vec3(plane), sphere.center) + plane.w < -sphere.radius) {
                return false;
            }
        }
        return true;
    }

private:
    std::array<glm::vec4, 4> _planes{};
};

/**
 * Near/far distances along `direction` (unit) from `origin` that contain every sphere, for
 * an orthographic shadow frustum fitted to exactly the bodies that cast into and receive
 * from it. `margin` pads both ends; near never drops below `minNear`.
 */
struct DepthRange {
    float nearPlane = 0.0f;
    float farPlane = 1.0f;
};

inline DepthRange FitDepthRange(const glm::vec3& origin, const glm::vec3& direction,
                                const std::vector<BoundingSphere>& spheres, float margin, float minNear) {
    float nearest = std::numeric_limits<float>::max();
    float farthest = std::numeric_limits<float>::lowest();
    for (const BoundingSphere& sphere : spheres) {
        const float along = glm::dot(sphere.center - origin, direction);
        nearest = std::min(nearest, along - sphere.radius);
        farthest = std::max(farthest, along + sphere.radius);
    }
    if (spheres.empty()) {
        return {minNear, minNear + 1.0f};
    }
    DepthRange range;
    range.nearPlane = std::max(nearest - margin, minNear);
    range.farPlane = std::max(farthest + margin, range.nearPlane + margin);
    return range;
}

#endif // SOLARSYSTEM_GFX_FRUSTUM_H
