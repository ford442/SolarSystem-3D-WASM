#include "UranusClouds.h"
#include "../../SimState.h"

UranusClouds::UranusClouds(const CloudsInfo& cloudsInfo, std::shared_ptr<SpaceObject> parent) : Clouds(cloudsInfo, std::move(parent))
{
    ConfigureDiffuseLOD("resource/textures_low/Uranus_Clouds_Diffuse_Low.dds",
                        "resource/textures_mid/Uranus_Clouds_Diffuse_Mid.dds",
                        "resource/textures/Uranus_Clouds_Diffuse.dds", "UranusClouds");
}

void UranusClouds::AdjustToParent(float /*timeScale*/) {
    static float rotationAngle = 0;
    constexpr float kSpinDegPerSimSecond = 3.447f; // ~6*0.009575 per frame @ 60fps
    if (gSimState->simDeltaSeconds > 0.0f) {
        rotationAngle += kSpinDegPerSimSecond * gSimState->simDeltaSeconds;
    }

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(_scaleFactor));
    Rotate(81.2f, glm::vec3(1, 0, 0));
    Rotate(rotationAngle, glm::vec3(0, 1, 0));
    UpdateModelMatrix();
}

void UranusClouds::Render() const {
    GetShader().SetFloat("ambientFactor", 0.0);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("cloudsNormalMap", 1);

    glBindTextureUnit(0, _diffuse.GetTexture());
    glBindTextureUnit(1, _normal.GetTexture());

    SpaceObject::Render();
}
