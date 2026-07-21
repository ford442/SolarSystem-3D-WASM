#include "Enceladus.h"
#include "../SatelliteOrbit.h"

Enceladus::Enceladus(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent) : Satellite(satelliteInfo, std::move(parent)),
    _diffuses(satelliteInfo.diffuseTextures), _normalMap(satelliteInfo.normalMap)
{
}

void Enceladus::AdjustToParent(float /*timeScale*/) {
    static float anomaly = -1.57079633f;
    static float rotationAngle = 0.0f;
    constexpr float kOrbitRadius = 65.000000f;
    constexpr float kPeriodDays = 1.37f;

    SatelliteOrbit::AdvanceAnomaly(anomaly, kPeriodDays);
    SatelliteOrbit::AdvanceSpin(rotationAngle, -2.76f);

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition() + SatelliteOrbit::Offset(kOrbitRadius, anomaly));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(rotationAngle, glm::vec3(0, 1, 0));
    UpdateModelMatrix();
}

void Enceladus::Render() const {
    GetShader().SetBool("hasNightTexture", false);
    GetShader().SetBool("hasSpecularMap", false);
    GetShader().SetBool("hasSpecular", true);
    GetShader().SetBool("isUseSphereIntersect", true);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("normalMap", 1);
    GetShader().SetFloat("ambientFactor", 0.0f);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _normalMap.GetTexture());

    SpaceObject::Render();
}
