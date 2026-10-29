#include "Atmosphere.h"
#include "BodyShape.h"

namespace {
glm::vec3 ToVec3(const BodyCatalog::Rgb& rgb) {
    return {rgb.r, rgb.g, rgb.b};
}
} // namespace

Atmosphere::Atmosphere(MeshHolder model, const BodyCatalog::AtmosphereRow& row,
                       std::shared_ptr<SpaceObject> parent, float artParentRadius)
    : OuterShell(std::move(model), std::move(parent), row.oneil.shellScale),
      _row(&row),
      _body(BodyCatalog::FindById(row.bodyId)),
      _parentRadius(artParentRadius),
      // Pulling the inner sphere just under the surface hides the seam where the shell
      // meets the planet mesh.
      _innerRadius(row.oneil.innerRadiusMinusEpsilon ? artParentRadius - 0.00007f : artParentRadius) {
    if (_body && BodyShape::IsOblate(*_body)) {
        _bodyToSphere = BodyShape::BodyToSphere(*_body);
        _sphereToBody = BodyShape::SphereToBody(*_body);
    }
}

void Atmosphere::SetLuts(GlTexture transmittance, GlTexture multiScattering) {
    _transmittanceLut = std::move(transmittance);
    _multiScatteringLut = std::move(multiScattering);
    if (!HasLuts()) {
        _physicalPathActive = false;
    }
}

float Atmosphere::ShellScale() const {
    if (_physicalPathActive) {
        const float topScene = GetPhysicalTopRadiusKm() / GetKmPerSceneUnit();
        return topScene / kShellMeshRadius * kPhysicalShellMargin;
    }
    return _row->oneil.shellScale * _parent->SizeFactor();
}

void Atmosphere::AdjustToParent() {
    Place(_parent->GetWorldPosition());
    Scale(glm::vec3(ShellScale()));
    // Flatten with the body so the limb follows an oblate cloud deck. The sphere mesh is
    // rotationally symmetric, so the tilt only orients the squash.
    if (_body && BodyShape::IsOblate(*_body)) {
        Rotate(_body->axialTiltDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
        if (_body->artTiltXDegrees != 0.0f) {
            Rotate(_body->artTiltXDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        Scale(BodyShape::PolarScale(*_body));
    }
}

glm::vec3 Atmosphere::GetAtmosphereColor() const {
    return ToVec3(_row->oneil.color);
}

glm::vec3 Atmosphere::GetMieTint() const {
    return ToVec3(_row->oneil.mieTint);
}

float Atmosphere::GetParentRadius() const {
    return _parentRadius * _parent->SizeFactor();
}

float Atmosphere::GetInnerRadius() const {
    return _innerRadius * _parent->SizeFactor();
}

float Atmosphere::GetOuterRadius() const {
    return _row->oneil.outerRadius * _parent->SizeFactor();
}

float Atmosphere::GetAtmosphereOuterBoundary() const {
    return kShellMeshRadius * ShellScale();
}

bool Atmosphere::Contains(const glm::vec3& point) const {
    return glm::length(_bodyToSphere * (point - GetPosition())) <= GetAtmosphereOuterBoundary();
}

float Atmosphere::GetPhysicalGroundRadiusKm() const {
    return _row->physical.groundRadiusKm;
}

float Atmosphere::GetPhysicalTopRadiusKm() const {
    const BodyCatalog::AtmospherePhysical& p = _row->physical;
    return p.groundRadiusKm + (p.topRadiusKm - p.groundRadiusKm) * p.thicknessScale;
}

float Atmosphere::GetKmPerSceneUnit() const {
    const float parentRadius = GetParentRadius();
    return parentRadius > 0.0f ? _row->physical.groundRadiusKm / parentRadius : 1.0f;
}
