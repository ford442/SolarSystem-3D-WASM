#include "NeptuneClouds.h"

NeptuneClouds::NeptuneClouds(const CloudsInfo& cloudsInfo, std::shared_ptr<SpaceObject> parent) : Clouds(cloudsInfo, std::move(parent))
{
    ConfigureDiffuseLOD("resource/textures_low/Neptune_Clouds_Diffuse_Low.dds",
                        "resource/textures/Neptune_Clouds_Diffuse.dds", "NeptuneClouds");
}

void NeptuneClouds::AdjustToParent(float timeScale) {
    static float rotationAngle = 0;

    if (timeScale > 0.0f) {
        rotationAngle += (8 *  0.01) * timeScale;
    }

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(_scaleFactor));
    Rotate(28.3f, glm::vec3(0, 0, 1));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void NeptuneClouds::Render() const {
    GetShader().SetFloat("ambientFactor", 0.0);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("cloudsNormalMap", 1);

    glBindTextureUnit(0, _diffuse.GetTexture());
    glBindTextureUnit(1, _normal.GetTexture());

    SpaceObject::Render();
}
