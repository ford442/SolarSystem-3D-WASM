#include "CatalogSatellite.h"
#include "SatelliteOrbit.h"

CatalogSatellite::CatalogSatellite(const SatelliteInfo& satelliteInfo,
                                   std::shared_ptr<SpaceObject> parent,
                                   const BodyCatalog::Entry& entry)
    : Satellite(satelliteInfo, std::move(parent)),
      _entry(entry),
      _material(CatalogMaterial::Material::FromEntry(entry)),
      _diffuses(satelliteInfo.diffuseTextures),
      _normalMap(satelliteInfo.normalMap),
      _specular(satelliteInfo.specularTexture) {
    const TexturePaths::Paths diffuse = TexturePaths::ForTextureId(entry.lod.diffuse);
    ConfigureDiffuseLOD(_diffuses.at(0), diffuse.low, diffuse.mid, diffuse.high, entry.lod.diffuse);
}

void CatalogSatellite::AdjustToParent(float /*timeScale*/) {
    // Pose is a function of the simulation date only, so SetSimulationEpoch jumps and
    // reverse scrubbing are exact. Keplerian placement where the backend has a solution
    // (Moon, Galileans, Titan, Triton); otherwise the circular mean-anomaly offset.
    const double julianDate = OrbitLayout::GetJulianDate();
    glm::vec3 offset;
    _ephemerisPlaced = SatelliteOrbit::EphemerisOffset(_entry, julianDate, offset);
    if (!_ephemerisPlaced) {
        const float anomaly = SatelliteOrbit::MeanAnomalyAt(_entry, julianDate);
        offset = _entry.orbitPlane == BodyCatalog::OrbitPlane::XY
                     ? SatelliteOrbit::OffsetXY(_entry.sceneOrbitRadius, anomaly)
                     : SatelliteOrbit::Offset(_entry.sceneOrbitRadius, anomaly);
    }

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition() + offset);
    Scale(glm::vec3(_earthSizeCoefficient));
    if (_entry.artRollDegrees != 0.0f) {
        Rotate(_entry.artRollDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    if (_entry.yawOffsetDegrees != 0.0f) {
        Rotate(_entry.yawOffsetDegrees, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    Rotate(SatelliteOrbit::SpinDegreesAt(_entry.spinDegPerSimSecond, julianDate), glm::vec3(0.0f, 1.0f, 0.0f));
}

float CatalogSatellite::OrbitSceneUnitsPerKm() const {
    if (_entry.keplerian.aKm <= 0.0f || _entry.sceneOrbitRadius <= 0.0f) {
        return 0.0f;
    }
    // EphemerisOffset maps the semi-major axis onto sceneOrbitRadius, so this is exactly the
    // factor that relates a real length near this orbit to its length on screen.
    return _entry.sceneOrbitRadius / _entry.keplerian.aKm;
}

void CatalogSatellite::Render() const {
    CatalogMaterial::Textures textures;
    textures.diffuse = _diffuses.at(0).GetTexture();
    textures.normal = _normalMap.GetTexture();
    if (_material.hasSpecular) {
        textures.specular = _specular.GetTexture();
    }
    // SatelliteInfo carries no night/cloud maps; the generator rejects a moon row that asks
    // for them, and BindMaterial clears both flags when their texture is 0.
    CatalogMaterial::BindMaterial(GetShader(), _material, textures);
    SpaceObject::Render();
}
