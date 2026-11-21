#ifndef SOLARSYSTEM_CAMERANAVIGATION_H
#define SOLARSYSTEM_CAMERANAVIGATION_H
#include <glm/glm.hpp>
#include <optional>
#include <vector>

/**
 * GL-free navigation math for a camera that flies from AU-scale distances down to a metre
 * above a surface (docs/ARCHITECTURE.md §11.1): altitude above a body, the floor that keeps
 * the camera out of it, clip planes and log-depth factor per altitude, altitude-scaled fly
 * speed, and the hysteresis that picks which body the camera co-moves with.
 *
 * Everything is in the world frame (heliocentric scene units, double). Application's
 * CameraController.cpp feeds it the scene's bodies once per frame.
 */
namespace CameraNavigation {

/** Clip planes and log-depth factor for the compressed scale: exactly what it always used. */
constexpr float kCompressedNear = 0.001f;
constexpr float kCompressedFar = 20000.0f;
/** Realistic: far enough for a probe at 250 AU looking back past the Sun. */
constexpr float kRealisticFar = 1.0e6f;

/** Above this many radii from the nearest body the camera stops co-moving with it… */
constexpr double kAnchorReleaseRadii = 80.0;
/** …and closer than this it starts: the gap is the hysteresis that stops it flickering. */
constexpr double kAnchorEngageRadii = 64.0;
/** A different body takes over only when it is this much closer (in its own radii). */
constexpr double kAnchorSwitchRatio = 0.5;

/** Below this altitude (in body radii) a body draws with the NEAR_SURFACE permutation. */
constexpr double kNearSurfaceRadii = 0.5;

/**
 * Hovering: under this altitude (in body radii) the floor ray-casts the drawn facets and the
 * camera co-rotates with the body, so the ground it hovers over stays under it with time
 * running. In orbit the anchor carries translation only and the body turns beneath. The
 * camera lets go a little higher up, so the mode does not flip on the boundary.
 */
constexpr double kHoverRadii = 0.05;
constexpr double kHoverReleaseRadii = 0.06;

/** A body the camera can fly near, as its mesh is drawn. */
struct Body {
    glm::dvec3 center{0.0};  // world position
    glm::dmat3 shape{1.0};   // model matrix's upper 3×3: model-space offsets → world offsets
    double meshRadius = 2.0; // the mesh's analytic radius in model units (sphere.obj: 2)
};

/** `world` in the body's model space (where the mesh is a sphere of meshRadius). */
glm::dvec3 ToModel(const Body& body, const glm::dvec3& world);

/** World length of one model unit along `modelDirection` (unit vector): the body's scale there. */
double WorldScaleAlong(const Body& body, const glm::dvec3& modelDirection);

/**
 * Altitude of `eye` above the surface along the line to the body's centre, where that line
 * meets the surface `surfaceModelRadius` model units out (the mesh radius for the analytic
 * figure, or a ray-cast hit on the facets actually drawn). Negative when inside.
 */
double Altitude(const Body& body, const glm::dvec3& eye, double surfaceModelRadius);

/** Altitude above the analytic figure (a sphere, or the oblate spheroid `shape` squashes it to). */
inline double AnalyticAltitude(const Body& body, const glm::dvec3& eye) {
    return Altitude(body, eye, body.meshRadius);
}

/**
 * `eye`, lifted along the line to the body's centre until it is at least `margin` above the
 * surface (`surfaceModelRadius` as in Altitude). Unchanged when already above it.
 */
glm::dvec3 Floor(const Body& body, const glm::dvec3& eye, double surfaceModelRadius, double margin);

/**
 * The closest hit of a model-space ray (origin, unit direction) with a triangle list, as a
 * distance along the ray; nullopt for a miss. Double throughout: a metre above a planet is
 * well under a float's spacing at the mesh's radius.
 */
std::optional<double> RaycastTriangles(const glm::dvec3& origin, const glm::dvec3& direction,
                                       const std::vector<glm::vec3>& positions,
                                       const std::vector<unsigned>& indices);

/**
 * How far out the drawn facets are along `modelDirection` (unit vector), in model units:
 * the surface radius for Altitude and Floor. The ray is cast inward from outside the mesh
 * (`boundingRadius` bounds its vertices), so the facet is found whether the eye is above it
 * or has ended up beneath it; nullopt when the direction misses every facet.
 */
std::optional<double> FacetRadius(const glm::dvec3& modelDirection, double boundingRadius,
                                  const std::vector<glm::vec3>& positions, const std::vector<unsigned>& indices);

/** The facet FacetRadius finds, with its outward unit normal (model space). */
struct FacetHit {
    double radius;
    glm::dvec3 normal;
};

/** FacetRadius plus the hit facet's normal: the plane a level horizon lies in near the ground. */
std::optional<FacetHit> FacetAlong(const glm::dvec3& modelDirection, double boundingRadius,
                                   const std::vector<glm::vec3>& positions, const std::vector<unsigned>& indices);

/** A model-space normal in the world frame (unit length; `shape` may squash or stretch). */
glm::dvec3 NormalToWorld(const Body& body, const glm::dvec3& modelNormal);

/** Whether to co-rotate with the body this frame: engage under kHoverRadii, release past kHoverReleaseRadii. */
bool CoRotates(bool current, double altitudeRadii);

/** The rotation that takes the body's drawn orientation `before` to `after` (scale divided out). */
glm::dmat3 RotationBetween(const glm::dmat3& before, const glm::dmat3& after);

/**
 * Where `eye` goes when `body` moves from its pose `before` to `after` (a frame's worth of
 * orbit, spin and any date jump). Translation only unless `coRotate`: then the eye keeps its
 * offset in the body's drawn frame (the model matrix's 3×3, which is what the surface is
 * drawn with), so it stays over the same ground point at the same altitude. `rotation`, if
 * given, receives the turn to apply to the camera's orientation (identity without coRotate).
 * All in double: the rotation is about the body's own axis, which is whatever the matrix
 * says, so nothing here needs to know the spin law.
 */
glm::dvec3 CarryEye(const Body& before, const Body& after, const glm::dvec3& eye, bool coRotate,
                    glm::dmat3* rotation = nullptr);

struct ClipPlanes {
    float nearPlane;
    float farPlane;
    /** C in log2(1 + C·w) / log2(1 + C·far) (common/log_depth.glsl). */
    float logDepthC;
};

/**
 * Compressed: the historical 0.001–20000 with C = 1, so its depth buffer is unchanged.
 * Realistic: near follows altitude (half of it, from 0.25 m up to 0.001 units) so a metre
 * above the ground is not clipped, and C = 1/near keeps log depth's linear toe inside the
 * near plane: about 2e-6 relative depth precision from the near plane out to the far one.
 */
ClipPlanes ChooseClipPlanes(bool trueScale, double altitude, double kmPerSceneUnit);

/**
 * Multiplier on the camera's movement speed (Camera::SetSpeedScale). Compressed: 1, the
 * historical constant speed. Realistic: altitude per second at the default speed of 5,
 * from 1 mm/s up to 1 AU/s, so the same keys cross a room or the inner system.
 */
double SpeedScale(bool trueScale, double altitude, double kmPerSceneUnit, double sceneUnitsPerAu);

/**
 * The index of the body to co-move with, given each body's distance from the camera in its
 * own radii (`radiiAway[i]`; negative or NaN = not a candidate). `current` is the previous
 * choice (-1 for none). Engage under kAnchorEngageRadii, release past kAnchorReleaseRadii,
 * hand over when another body is kAnchorSwitchRatio as many radii away. `pinned` keeps
 * `current` regardless (a focus flight, which starts outside the release distance).
 */
int ChooseAnchor(int current, const std::vector<double>& radiiAway, bool pinned);

} // namespace CameraNavigation

#endif // SOLARSYSTEM_CAMERANAVIGATION_H
