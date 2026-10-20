// The fly camera's per-frame navigation (docs/ARCHITECTURE.md §11.1): it co-moves with the
// body it is hovering near, flies at a speed set by its altitude, never goes below a
// surface, and sets the clip planes and log-depth curve for that altitude. The math is
// GL-free in core/CameraNavigation.h; this is where the scene's bodies are fed to it.
#include "Application.h"
#include "Solar_System/SceneScale.h"
#include "core/CameraNavigation.h"
#include <cmath>
#include <limits>

namespace {

// How far above the drawn surface the floor holds the camera. Compressed scale keeps its
// fixed 0.001 near plane, so twice that; realistic stops half a metre up.
constexpr double kCompressedFloorMargin = 0.002;
constexpr double kRealisticFloorMarginKm = 0.5e-3;
// Analytic altitude (in body radii) under which the floor ray-casts the mesh facets: they
// sit below the analytic sphere between vertices, by kilometres on realistic scale.
constexpr double kFacetCastRadii = 0.05;

CameraNavigation::Body NavigationBody(const SpaceObject& object) {
    CameraNavigation::Body body;
    body.center = object.GetWorldPosition();
    body.shape = glm::dmat3(glm::mat3(object.GetModelMatrix()));
    body.meshRadius = object.GetModel().BoundingRadius();
    return body;
}

/** Distance from the centre to the drawn surface along the line to `eye`, in model units. */
double SurfaceModelRadius(const SpaceObject& object, const CameraNavigation::Body& body, const glm::dvec3& eye) {
    const glm::dvec3 model = CameraNavigation::ToModel(body, eye);
    const double r = glm::length(model);
    if (r <= 0.0) {
        return body.meshRadius;
    }
    const MeshHolder::Triangles& triangles = object.GetModel().GetTriangles();
    return CameraNavigation::FacetRadius(model / r, body.meshRadius, triangles.positions, triangles.indices)
        .value_or(body.meshRadius);
}

} // namespace

glm::vec3 Application::CameraRenderPosition() const {
    return OrbitLayout::ToRenderFrame(_camera.GetWorldPosition());
}

std::vector<std::shared_ptr<const SpaceObject>> Application::NavigationBodies() const {
    std::vector<std::shared_ptr<const SpaceObject>> bodies;
    if (_sun) {
        bodies.push_back(_sun);
    }
    for (const auto& component : _renderableSceneComponents) {
        if (component.planet) {
            bodies.push_back(component.planet);
        }
        for (const auto& satellite : component.satellites) {
            bodies.push_back(satellite);
        }
    }
    return bodies;
}

void Application::SetCameraAnchor(std::shared_ptr<const SpaceObject> body, bool pinned) {
    _cameraAnchor.pinned = pinned && body != nullptr;
    if (body != _cameraAnchor.body) {
        _cameraAnchor.lastWorld = body ? body->GetWorldPosition() : glm::dvec3(0.0);
    }
    _cameraAnchor.body = std::move(body);
}

void Application::CarryCameraWithAnchor() {
    if (_cameraAnchor.body) {
        const glm::dvec3 now = _cameraAnchor.body->GetWorldPosition();
        // Bodies were placed for this frame's date just before; a date jump carries the
        // camera with its body too, so the scrubber keeps it in view.
        _camera.Carry(now - _cameraAnchor.lastWorld);
        _cameraAnchor.lastWorld = now;
    }

    double altitude = std::numeric_limits<double>::max();
    for (const auto& object : NavigationBodies()) {
        altitude = std::min(altitude, CameraNavigation::AnalyticAltitude(NavigationBody(*object),
                                                                         _camera.GetWorldPosition()));
    }
    _camera.SetSpeedScale(CameraNavigation::SpeedScale(SceneScale::IsTrueScale(), altitude, SceneScale::kKmPerSceneUnit,
                                                       OrbitLayout::kAuToSceneUnits));
}

void Application::ConstrainCamera() {
    const bool trueScale = SceneScale::IsTrueScale();
    const std::vector<std::shared_ptr<const SpaceObject>> bodies = NavigationBodies();
    glm::dvec3 eye = _camera.GetWorldPosition();

    // The body whose surface is nearest: the only one the camera can be under.
    const SpaceObject* nearest = nullptr;
    double nearestAltitude = std::numeric_limits<double>::max();
    for (const auto& object : bodies) {
        const double altitude = CameraNavigation::AnalyticAltitude(NavigationBody(*object), eye);
        if (altitude < nearestAltitude) {
            nearestAltitude = altitude;
            nearest = object.get();
        }
    }

    double altitude = nearestAltitude;
    if (nearest) {
        const CameraNavigation::Body body = NavigationBody(*nearest);
        const double radius = nearest->GetBoundingSphere().radius;
        const double surface =
            nearestAltitude < kFacetCastRadii * radius ? SurfaceModelRadius(*nearest, body, eye) : body.meshRadius;
        // Observe parks the camera on Earth's surface itself and draws only the sky.
        if (!_observe.active) {
            const double margin = trueScale ? SceneScale::KmToScene(kRealisticFloorMarginKm) : kCompressedFloorMargin;
            eye = CameraNavigation::Floor(body, eye, surface, margin);
            _camera.SetWorldPosition(eye);
        }
        altitude = CameraNavigation::Altitude(body, eye, surface);
    }

    const CameraNavigation::ClipPlanes clip =
        CameraNavigation::ChooseClipPlanes(trueScale, altitude, SceneScale::kKmPerSceneUnit);
    _camera.SetNear(clip.nearPlane);
    _camera.SetFar(clip.farPlane);
    _logDepthC = clip.logDepthC;

    // Mission follow and Observe pose the camera themselves every frame.
    if (_observe.active || _missionFollowActive) {
        SetCameraAnchor(nullptr, false);
        return;
    }
    if (!_camera.IsTransitionActive()) {
        _cameraAnchor.pinned = false;
    }
    std::vector<double> radiiAway;
    int current = -1;
    for (size_t i = 0; i < bodies.size(); ++i) {
        const double radius = bodies[i]->GetBoundingSphere().radius;
        radiiAway.push_back(radius > 0.0 ? glm::distance(eye, bodies[i]->GetWorldPosition()) / radius : -1.0);
        if (bodies[i] == _cameraAnchor.body) {
            current = static_cast<int>(i);
        }
    }
    const int chosen = CameraNavigation::ChooseAnchor(current, radiiAway, _cameraAnchor.pinned);
    SetCameraAnchor(chosen >= 0 ? bodies[static_cast<size_t>(chosen)] : nullptr, _cameraAnchor.pinned);
}

Application::CameraBodyFrame Application::CaptureCameraBodyFrame() const {
    CameraBodyFrame frame;
    double best = std::numeric_limits<double>::max();
    for (const auto& object : NavigationBodies()) {
        const double radius = object->GetBoundingSphere().radius;
        if (radius <= 0.0) {
            continue;
        }
        const glm::dvec3 offset = (_camera.GetWorldPosition() - object->GetWorldPosition()) / radius;
        if (glm::length(offset) < best) {
            best = glm::length(offset);
            frame.body = object;
            frame.offsetInRadii = offset;
        }
    }
    return frame;
}

void Application::RestoreCameraBodyFrame(const CameraBodyFrame& frame) {
    if (frame.body) {
        const double radius = frame.body->GetBoundingSphere().radius;
        _camera.SetWorldPosition(frame.body->GetWorldPosition() + frame.offsetInRadii * radius);
    }
    // Its target was in the other scale; and the anchor's last position jumped with it.
    _camera.CancelTransition();
    _cameraAnchor.pinned = false;
    if (_cameraAnchor.body) {
        _cameraAnchor.lastWorld = _cameraAnchor.body->GetWorldPosition();
    }
}
