#include "Atmosphere.h"

Atmosphere::Atmosphere(const AtmosphereInfo& atmosphereInfo, std::shared_ptr<SpaceObject> parent)
    : OuterShell(atmosphereInfo.atmosphereModel, *atmosphereInfo.atmosphereShader, std::move(parent), atmosphereInfo.scaleFactor),
      _atmosphereColor(atmosphereInfo.atmosphereColor), _mieTint(atmosphereInfo.mieTint), _innerRadius(atmosphereInfo.innerRadius), _outerRadius(atmosphereInfo.outerRadius)
{
    _atmosphereOuterBoundary *= atmosphereInfo.scaleFactor;
}

void Atmosphere::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(_scaleFactor));
}

glm::vec3 Atmosphere::GetAtmosphereColor() const {
    return _atmosphereColor;
}

glm::vec3 Atmosphere::GetMieTint() const {
    return _mieTint;
}

float Atmosphere::GetInnerRadius() const {
    return _innerRadius;
}

float Atmosphere::GetOuterRadius() const {
    return _outerRadius;
}

float Atmosphere::GetAtmosphereOuterBoundary() const {
    return _atmosphereOuterBoundary;
}
