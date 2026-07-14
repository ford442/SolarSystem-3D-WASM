#include "Clouds.h"

Clouds::Clouds(const CloudsInfo& cloudsInfo, std::shared_ptr<SpaceObject> parent) : OuterShell(cloudsInfo.cloudsModel, *cloudsInfo.cloudsShader, std::move(parent),
    cloudsInfo.scaleFactor), _diffuse(cloudsInfo.diffuseMap), _normal(cloudsInfo.normalMap)
{
}

void Clouds::ConfigureDiffuseLOD(const std::string& lowPath, const std::string& highPath, const std::string& label) {
    _diffuseLOD.Configure(_diffuse, lowPath, highPath, label, TextureLoadCategory::Clouds);
}

void Clouds::LoadHighResIfClose(const glm::vec3& cameraPosition) {
    _diffuseLOD.Update(cameraPosition, _parent->GetPosition());
}
