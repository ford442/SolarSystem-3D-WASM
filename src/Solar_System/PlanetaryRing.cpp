#include "PlanetaryRing.h"

PlanetaryRing::PlanetaryRing(const PlanetaryRingInfo& planetaryRingInfo, std::shared_ptr<Planet> parentPlanet) :
    SpaceObject(planetaryRingInfo.ringModel, *planetaryRingInfo.ringShader), _ringTexture(planetaryRingInfo.ringDiffuse),
    _parentPlanet(std::move(parentPlanet)), _ringInnerRadius(planetaryRingInfo.ringInnerRadius), _ringOuterRadius(planetaryRingInfo.ringOuterRadius), _ringNormal(_upVector)
{
}

std::shared_ptr<Planet> PlanetaryRing::GetParent() const {
    return _parentPlanet;
}

GLuint PlanetaryRing::GetRingTexture() const {
    return _ringTexture.GetTexture();
}

glm::vec3 PlanetaryRing::GetRingNormal() const {
    return _ringNormal;
}

float PlanetaryRing::GetInnerRadius() const {
    return _ringInnerRadius;
}

float PlanetaryRing::GetOuterRadius() const {
    return _ringOuterRadius;
}

void PlanetaryRing::UpdateRingNormal() {
    _ringNormal = glm::vec3(GetRotationMatrix() * glm::vec4(_upVector, 1.0));
}

void PlanetaryRing::ConfigureDiffuseLOD(const std::string& lowPath, const std::string& midPath,
                                       const std::string& highPath, const std::string& label) {
    _diffuseLOD.Configure(_ringTexture, lowPath, midPath, highPath, label, TextureLoadCategory::Ring);
}

void PlanetaryRing::LoadHighResIfClose(const glm::vec3& cameraPosition) {
    _diffuseLOD.Update(cameraPosition, _parentPlanet->GetPosition());
}
