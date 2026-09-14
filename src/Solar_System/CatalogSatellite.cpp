#include "CatalogSatellite.h"
#include "SatelliteOrbit.h"

CatalogSatellite::CatalogSatellite(const SatelliteInfo& satelliteInfo,
                                   std::shared_ptr<SpaceObject> parent,
                                   const BodyCatalog::Entry& entry)
    : Satellite(satelliteInfo, std::move(parent)),
      _entry(entry),
      _hasSpecular(entry.lod.specular != nullptr),
      _anomaly(entry.initialAnomalyRad),
      _spinDegrees(0.0f),
      _diffuses(satelliteInfo.diffuseTextures),
      _normalMap(satelliteInfo.normalMap),
      _specular(satelliteInfo.specularTexture) {
    const TexturePaths::Paths diffuse = TexturePaths::ForTextureId(entry.lod.diffuse);
    ConfigureDiffuseLOD(_diffuses.at(0), diffuse.low, diffuse.mid, diffuse.high, entry.lod.diffuse);
}

void CatalogSatellite::AdjustToParent(float /*timeScale*/) {
    SatelliteOrbit::AdvanceAnomaly(_anomaly, _entry.orbitalPeriodDays);
    SatelliteOrbit::AdvanceSpin(_spinDegrees, _entry.spinDegPerSimSecond);

    const glm::vec3 offset = _entry.orbitPlane == BodyCatalog::OrbitPlane::XY
                                 ? SatelliteOrbit::OffsetXY(_entry.sceneOrbitRadius, _anomaly)
                                 : SatelliteOrbit::Offset(_entry.sceneOrbitRadius, _anomaly);

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition() + offset);
    Scale(glm::vec3(_earthSizeCoefficient));
    if (_entry.artRollDegrees != 0.0f) {
        Rotate(_entry.artRollDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    if (_entry.yawOffsetDegrees != 0.0f) {
        Rotate(_entry.yawOffsetDegrees, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    Rotate(_spinDegrees, glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void CatalogSatellite::Render() const {
    GetShader().SetBool("hasCloudTexture", false);
    GetShader().SetBool("hasNightTexture", false);
    GetShader().SetBool("hasSpecularMap", _hasSpecular);
    GetShader().SetBool("hasSpecular", _hasSpecular);
    GetShader().SetBool("isUseSphereIntersect", _entry.useSphereIntersect);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("normalMap", 1);
    GetShader().SetFloat("ambientFactor", _entry.ambientFactor);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _normalMap.GetTexture());
    if (_hasSpecular) {
        GetShader().SetInt("specularMap", 2);
        glBindTextureUnit(2, _specular.GetTexture());
    }

    SpaceObject::Render();
}
