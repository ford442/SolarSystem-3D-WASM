#include "Atmosphere.h"

namespace {
glm::vec3 ToVec3(const BodyCatalog::Rgb& rgb) {
    return {rgb.r, rgb.g, rgb.b};
}
} // namespace

Atmosphere::Atmosphere(MeshHolder model, const Shader& shader, const BodyCatalog::AtmosphereRow& row,
                       std::shared_ptr<SpaceObject> parent, float parentRadius)
    : OuterShell(std::move(model), shader, std::move(parent), row.oneil.shellScale),
      _row(&row),
      _parentRadius(parentRadius),
      // Pulling the inner sphere just under the surface hides the seam where the shell
      // meets the planet mesh.
      _innerRadius(row.oneil.innerRadiusMinusEpsilon ? parentRadius - 0.00007f : parentRadius) {
}

Atmosphere::~Atmosphere() {
    SetLuts(0, 0);
}

void Atmosphere::SetLuts(unsigned int transmittance, unsigned int multiScattering) {
    if (_transmittanceLut != 0) {
        glDeleteTextures(1, &_transmittanceLut);
    }
    if (_multiScatteringLut != 0) {
        glDeleteTextures(1, &_multiScatteringLut);
    }
    _transmittanceLut = transmittance;
    _multiScatteringLut = multiScattering;
    if (!HasLuts()) {
        _physicalPathActive = false;
    }
}

float Atmosphere::ShellScale() const {
    if (_physicalPathActive) {
        const float topScene = GetPhysicalTopRadiusKm() / GetKmPerSceneUnit();
        return topScene / kShellMeshRadius * kPhysicalShellMargin;
    }
    return _row->oneil.shellScale;
}

void Atmosphere::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(ShellScale()));
}

glm::vec3 Atmosphere::GetAtmosphereColor() const {
    return ToVec3(_row->oneil.color);
}

glm::vec3 Atmosphere::GetMieTint() const {
    return ToVec3(_row->oneil.mieTint);
}

float Atmosphere::GetInnerRadius() const {
    return _innerRadius;
}

float Atmosphere::GetOuterRadius() const {
    return _row->oneil.outerRadius;
}

float Atmosphere::GetAtmosphereOuterBoundary() const {
    return kShellMeshRadius * ShellScale();
}

float Atmosphere::GetPhysicalGroundRadiusKm() const {
    return _row->physical.groundRadiusKm;
}

float Atmosphere::GetPhysicalTopRadiusKm() const {
    const BodyCatalog::AtmospherePhysical& p = _row->physical;
    return p.groundRadiusKm + (p.topRadiusKm - p.groundRadiusKm) * p.thicknessScale;
}

float Atmosphere::GetKmPerSceneUnit() const {
    return _parentRadius > 0.0f ? _row->physical.groundRadiusKm / _parentRadius : 1.0f;
}
