#include "Clouds.h"

Clouds::Clouds(CloudsInfo&& cloudsInfo, std::shared_ptr<SpaceObject> parent) : OuterShell(cloudsInfo.cloudsModel, *cloudsInfo.cloudsShader, std::move(parent),
    cloudsInfo.scaleFactor), _diffuse(std::move(cloudsInfo.diffuseMap)), _normal(std::move(cloudsInfo.normalMap))
{
}

void Clouds::ConfigureDiffuseLOD(const std::string& lowPath, const std::string& midPath,
                                 const std::string& highPath, const std::string& label) {
    _diffuseLOD.Configure(_diffuse, lowPath, midPath, highPath, label, TextureLoadCategory::Clouds);
}

void Clouds::LoadHighResIfClose(const glm::vec3& cameraPosition, const TextureLodPolicy& policy) {
    _diffuseLOD.Update(cameraPosition, _parent->GetPosition(), 50.0f, policy.maxTier);
}
