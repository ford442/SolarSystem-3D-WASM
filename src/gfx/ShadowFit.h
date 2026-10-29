#ifndef SOLARSYSTEM_GFX_SHADOWFIT_H
#define SOLARSYSTEM_GFX_SHADOWFIT_H
#include "Frustum.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include <vector>

/**
 * The sideways extent of a component's orthographic shadow box (docs/ARCHITECTURE.md §7.5).
 * GL-free; Application::UpdateScene builds the light matrix from the box this fits.
 *
 * From orbit the box is ±3 body radii about the Sun→body axis, which holds the rings and
 * moons that receive the body's shadow and a full-disk view's terminator. A shadow map has
 * one texel size for the whole box, so near the ground on realistic scale that is a texel of
 * kilometres. Within a few radii the box is fitted to the visible cap instead: the part of
 * the body's sphere the camera's frustum sees, plus any ring or moon in view.
 *
 * Everything is in one frame (the render frame, the light view's own) and in double: the
 * light's eye is the Sun, 1 AU from the camera.
 */
namespace ShadowFit {

/** Fit the visible cap when the camera is within this many body radii of the centre… */
constexpr double kCapEngageRadii = 3.0;
/** …and go back to the full box past this many: the gap stops the resolution pumping. */
constexpr double kCapReleaseRadii = 4.0;
/** Fraction added around the fitted cap (PCF taps, the grid missing a limb between samples). */
constexpr double kCapMargin = 0.1;
/** The box never gets tighter than this fraction of a body radius (a camera looking at the sky). */
constexpr double kMinCapRadii = 1.0e-4;
/** Closer than this fraction of the radius to the centre, the camera is inside the body. */
constexpr double kInsideRadii = 0.9;
/** How far under the camera's own radius the ground is taken when it hovers below `radius`. */
constexpr double kGroundSlack = 1.0e-5;
/** Lines of camera rays per axis across the viewport (plus its rim), and horizon samples. */
constexpr int kRayGrid = 9;
constexpr int kHorizonSamples = 128;
/** Texels the box's centre snaps to: a multiple of every shadow-map size (1024, 2048, 4096). */
constexpr double kSnapTexels = 1024.0;

inline bool UseVisibleCap(bool current, double radiiFromCentre) {
    return radiiFromCentre < (current ? kCapReleaseRadii : kCapEngageRadii);
}

/** glm::lookAt(eye, target, +Y)'s axes: light-space x is `right`, y is `up`, looking along `forward`. */
struct LightBasis {
    glm::dvec3 eye{0.0};
    glm::dvec3 right{1.0, 0.0, 0.0};
    glm::dvec3 up{0.0, 1.0, 0.0};
    glm::dvec3 forward{0.0, 0.0, -1.0};

    glm::dvec2 ToLight(const glm::dvec3& point) const {
        const glm::dvec3 offset = point - eye;
        return {glm::dot(offset, right), glm::dot(offset, up)};
    }
};

inline LightBasis LightLookAt(const glm::dvec3& eye, const glm::dvec3& target) {
    LightBasis basis;
    basis.eye = eye;
    basis.forward = glm::normalize(target - eye);
    basis.right = glm::normalize(glm::cross(basis.forward, glm::dvec3(0.0, 1.0, 0.0)));
    basis.up = glm::cross(basis.right, basis.forward);
    return basis;
}

/** A perspective camera: position, orthonormal axes, vertical half-angle tangent and aspect. */
struct View {
    glm::dvec3 eye{0.0};
    glm::dvec3 front{0.0, 0.0, -1.0};
    glm::dvec3 right{1.0, 0.0, 0.0};
    glm::dvec3 up{0.0, 1.0, 0.0};
    double tanHalfFovY = 0.4;
    double aspect = 1.0;
};

/** A square in light space: centre (x, y) and half the side. */
struct Box {
    double centerX = 0.0;
    double centerY = 0.0;
    double halfExtent = 0.0;
};

/**
 * The square that encloses where the camera sees the sphere (`center`, `radius`), with a
 * margin, and every sphere of `extras` (rings and moons the caller found in view). The
 * visible ground is bounded by the frustum's edges and by the horizon, so those are what is
 * sampled: camera rays along the frustum's rim and across a grid, where a hit is a visible
 * surface point; the point where a line of rays turns from hitting to missing is the horizon
 * (found by bisection: a ray a degree from grazing lands a hundred kilometres out); and the
 * horizon circle itself where it lies inside the view. The point under the camera is always
 * in. Returns a negative half-extent when the camera is inside the sphere, where there is
 * no cap to fit.
 */
inline Box FitVisibleCap(const LightBasis& light, const View& view, const glm::dvec3& center, double radius,
                         const std::vector<BoundingSphere>& extras = {}) {
    const glm::dvec3 toEye = view.eye - center;
    const double distanceSq = glm::dot(toEye, toEye);
    const double distance = std::sqrt(distanceSq);
    // Camera under the sphere altogether: there is no cap to fit.
    if (distance < kInsideRadii * radius) {
        return {0.0, 0.0, -1.0};
    }
    // The drawn facets sag below the analytic sphere (kilometres on realistic Earth), and the
    // floor holds a hovering camera to them, so it can sit a little under `radius`. Its ground
    // is then the sphere through the camera, a hair below it: the horizon from there is
    // sqrt(2 * kGroundSlack) radii out. (Clouds and air stand a few kilometres proud of the
    // sphere; the margin covers them.)
    const double ball = std::min(radius, distance * (1.0 - kGroundSlack));

    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    const auto include = [&](const glm::dvec3& point) {
        const glm::dvec2 p = light.ToLight(point);
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    };

    const glm::dvec3 nadir = toEye / distance;
    include(center + nadir * ball);

    // The surface point a ray through viewport position (a, b) in [-1, 1]² first meets.
    const auto probe = [&](double a, double b, glm::dvec3& hit) {
        const glm::dvec3 direction = glm::normalize(view.front + view.right * (a * view.tanHalfFovY * view.aspect) +
                                                    view.up * (b * view.tanHalfFovY));
        const double along = glm::dot(toEye, direction);
        const double discriminant = along * along - (distanceSq - ball * ball);
        const double entry = -along - std::sqrt(std::max(discriminant, 0.0));
        if (discriminant < 0.0 || entry <= 0.0) {
            return false;
        }
        hit = view.eye + direction * entry;
        return true;
    };
    // Rays along a segment of the viewport; each flip between hitting and missing is bisected
    // down to the grazing ray, whose hit is the horizon.
    const auto walk = [&](double a0, double b0, double a1, double b1) {
        constexpr int steps = 32;
        glm::dvec3 hit(0.0);
        bool previous = probe(a0, b0, hit);
        if (previous) {
            include(hit);
        }
        for (int k = 1; k <= steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            const bool now = probe(a0 + (a1 - a0) * t, b0 + (b1 - b0) * t, hit);
            if (now) {
                include(hit);
            }
            if (now != previous) {
                double lo = static_cast<double>(k - 1) / steps; // previous's side
                double hi = t;                                  // now's side
                for (int i = 0; i < 48; ++i) {
                    const double mid = 0.5 * (lo + hi);
                    glm::dvec3 midHit(0.0);
                    if (probe(a0 + (a1 - a0) * mid, b0 + (b1 - b0) * mid, midHit) == previous) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                glm::dvec3 limb(0.0);
                if (probe(a0 + (a1 - a0) * lo, b0 + (b1 - b0) * lo, limb) ||
                    probe(a0 + (a1 - a0) * hi, b0 + (b1 - b0) * hi, limb)) {
                    include(limb);
                }
            }
            previous = now;
        }
    };
    walk(-1.0, -1.0, 1.0, -1.0);
    walk(-1.0, 1.0, 1.0, 1.0);
    walk(-1.0, -1.0, -1.0, 1.0);
    walk(1.0, -1.0, 1.0, 1.0);
    for (int i = 0; i < kRayGrid; ++i) {
        const double c = -1.0 + 2.0 * i / (kRayGrid - 1);
        walk(-1.0, c, 1.0, c);
        walk(c, -1.0, c, 1.0);
    }

    // The horizon circle: ground at angle acos(R/d) from the point below the camera. Where
    // it lies inside the view its extremes are not on a ray the lines above happened to cast.
    const double cosHorizon = ball / distance;
    const double sinHorizon = std::sqrt(std::max(0.0, 1.0 - cosHorizon * cosHorizon));
    const glm::dvec3 across = std::abs(nadir.y) < 0.99 ? glm::normalize(glm::cross(nadir, glm::dvec3(0.0, 1.0, 0.0)))
                                                       : glm::dvec3(1.0, 0.0, 0.0);
    const glm::dvec3 along = glm::cross(nadir, across);
    for (int k = 0; k < kHorizonSamples; ++k) {
        const double phi = 2.0 * M_PI * k / kHorizonSamples;
        const glm::dvec3 point =
            center + ball * (nadir * cosHorizon + (across * std::cos(phi) + along * std::sin(phi)) * sinHorizon);
        const glm::dvec3 toPoint = point - view.eye;
        const double depth = glm::dot(toPoint, view.front);
        if (depth <= 0.0) {
            continue;
        }
        const double x = glm::dot(toPoint, view.right) / (depth * view.tanHalfFovY * view.aspect);
        const double y = glm::dot(toPoint, view.up) / (depth * view.tanHalfFovY);
        if (std::abs(x) <= 1.0 && std::abs(y) <= 1.0) {
            include(point);
        }
    }

    for (const BoundingSphere& sphere : extras) {
        const glm::dvec3 c(sphere.center);
        include(c + light.right * static_cast<double>(sphere.radius) + light.up * static_cast<double>(sphere.radius));
        include(c - light.right * static_cast<double>(sphere.radius) - light.up * static_cast<double>(sphere.radius));
    }

    Box box;
    box.centerX = 0.5 * (minX + maxX);
    box.centerY = 0.5 * (minY + maxY);
    box.halfExtent = 0.5 * std::max(maxX - minX, maxY - minY) * (1.0 + kCapMargin);
    return box;
}

/**
 * `box` rounded so it does not swim as the camera turns: the half-extent up to the next
 * quarter octave (at most 19 % larger), and the centre onto a grid of kSnapTexels across the
 * box, which is a whole number of texels for any shadow-map size. The box never gets smaller
 * than `minHalfExtent`, and still covers what it did.
 */
inline Box Quantize(const Box& box, double minHalfExtent) {
    const double half = std::max(box.halfExtent, minHalfExtent);
    const double stepped = std::exp2(std::ceil(std::log2(half) * 4.0) / 4.0);
    const double snap = 2.0 * stepped / kSnapTexels;
    Box out;
    out.centerX = std::round(box.centerX / snap) * snap;
    out.centerY = std::round(box.centerY / snap) * snap;
    out.halfExtent = stepped + snap;
    return out;
}

} // namespace ShadowFit

#endif // SOLARSYSTEM_GFX_SHADOWFIT_H
