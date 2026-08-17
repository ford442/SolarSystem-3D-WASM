#include "Satellite.h"

Satellite::Satellite(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent) :
    SpaceObject(satelliteInfo.satelliteModel, *satelliteInfo.satelliteShader, satelliteInfo.engName, satelliteInfo.otherLangName), _parent(std::move(parent)),
    _earthSizeCoefficient(satelliteInfo.earthSizeCoefficient)
{
    _radius *= _earthSizeCoefficient;
}

std::shared_ptr<SpaceObject> Satellite::GetParent() const {
    return _parent;
}

float Satellite::GetRadius() const {
    return _radius;
}

float Satellite::GetEarthSizeCoefficient() const {
    return _earthSizeCoefficient;
}

void Satellite::ConfigureDiffuseLOD(TextureImage2D& diffuse, const std::string& lowPath,
                                    const std::string& midPath, const std::string& highPath,
                                    const std::string& label) {
    _diffuseLOD.Configure(diffuse, lowPath, midPath, highPath, label, TextureLoadCategory::Satellite);
}

void Satellite::LoadHighResIfClose(const glm::vec3& cameraPosition) {
    _diffuseLOD.Update(cameraPosition, GetPosition());
}
