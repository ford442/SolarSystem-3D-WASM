#include "Moon.h"
#include "../SatelliteOrbit.h"

Moon::Moon(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent) : Satellite(satelliteInfo, std::move(parent)),
    _diffuses(satelliteInfo.diffuseTextures), _normalMap(satelliteInfo.normalMap)
{
    ConfigureDiffuseLOD(_diffuses.at(0), TexturePaths::Moon::Diffuse.low,
                        TexturePaths::Moon::Diffuse.mid,
                        TexturePaths::Moon::Diffuse.high, "Moon");
}

void Moon::AdjustToParent(float /*timeScale*/) {
    static float anomaly = -1.57079633f;
    static float rotationAngle = 0.0f;
    constexpr float kOrbitRadius = 25.000000f;
    constexpr float kPeriodDays = 27.3f;

    SatelliteOrbit::AdvanceAnomaly(anomaly, kPeriodDays);
    SatelliteOrbit::AdvanceSpin(rotationAngle, -0.69f);

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition() + SatelliteOrbit::Offset(kOrbitRadius, anomaly));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(-75, glm::vec3(0, 1, 0));
    Rotate(rotationAngle, glm::vec3(0, 1, 0));
    UpdateModelMatrix();
}

void Moon::Render() const {
    GetShader().SetBool("hasCloudTexture", false);
    GetShader().SetBool("hasNightTexture", false);
    GetShader().SetBool("hasSpecularMap", false);
    GetShader().SetBool("hasSpecular", false);
    GetShader().SetBool("isUseSphereIntersect", true);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("normalMap", 1);
    GetShader().SetFloat("ambientFactor", 0.0f);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _normalMap.GetTexture());

    SpaceObject::Render();
}
