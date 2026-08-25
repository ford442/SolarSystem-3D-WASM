#include "EarthClouds.h"
#include "../../SimState.h"

EarthClouds::EarthClouds(const CloudsInfo& cloudsInfo, std::shared_ptr<SpaceObject> parent) : Clouds(cloudsInfo, std::move(parent))
{
    ConfigureDiffuseLOD("resource/textures_low/Earth_Clouds_Diffuse_Low.dds",
                        "resource/textures_mid/Earth_Clouds_Diffuse_Mid.dds",
                        "resource/textures/Earth_Clouds_Diffuse.dds", "EarthClouds");
}

void EarthClouds::AdjustToParent(float /*timeScale*/) {
    static float rotationAngle = 0;
    // ~1.125 deg/s at 1x preserves prior ~0.01875 deg/frame @ 60fps look.
    constexpr float kSpinDegPerSimSecond = 1.125f;
    if (gSimState->simDeltaSeconds > 0.0f) {
        rotationAngle += kSpinDegPerSimSecond * gSimState->simDeltaSeconds;
    }

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(_scaleFactor));
    Rotate(-23.4f, glm::vec3(0, 0, 1));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void EarthClouds::Render() const {
    GetShader().SetFloat("ambientFactor", 1.0);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("cloudsNormalMap", 1);

    glBindTextureUnit(0, _diffuse.GetTexture());
    glBindTextureUnit(1, _normal.GetTexture());

    SpaceObject::Render();
}
