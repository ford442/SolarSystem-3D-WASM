#include "CatalogSatellite.h"
#include "SatelliteOrbit.h"
#include "SceneScale.h"

CatalogSatellite::CatalogSatellite(SatelliteInfo&& satelliteInfo,
                                   std::shared_ptr<SpaceObject> parent,
                                   const BodyCatalog::Entry& entry)
    : Satellite(satelliteInfo, std::move(parent)),
      _entry(entry),
      _material(CatalogMaterial::Material::FromEntry(entry)),
      _diffuses(std::move(satelliteInfo.diffuseTextures)),
      _normalMap(std::move(satelliteInfo.normalMap)),
      _specular(std::move(satelliteInfo.specularTexture)) {
    const TexturePaths::Paths diffuse = TexturePaths::ForTextureId(entry.lod.diffuse);
    ConfigureDiffuseLOD(_diffuses.at(0), diffuse.low, diffuse.mid, diffuse.high, entry.lod.diffuse);
}

void CatalogSatellite::AdjustToParent() {
    // Pose is a function of the simulation date only, so SetSimulationEpoch jumps and
    // reverse scrubbing are exact. Keplerian placement where the backend has a solution
    // (Moon, Galileans, Titan, Triton); otherwise the circular mean-anomaly offset.
    const double julianDate = OrbitLayout::GetJulianDate();
    glm::dvec3 offset;
    _ephemerisPlaced = SatelliteOrbit::EphemerisOffset(_entry, julianDate, offset);
    if (!_ephemerisPlaced) {
        const float anomaly = SatelliteOrbit::MeanAnomalyAt(_entry, julianDate);
        const double radius = SceneScale::SatelliteCircularRadius(_entry);
        offset = _entry.orbitPlane == BodyCatalog::OrbitPlane::XY ? SatelliteOrbit::OffsetXY(radius, anomaly)
                                                                  : SatelliteOrbit::Offset(radius, anomaly);
    }

    // Parent world position plus offset in double: on realistic scale both are large
    // numbers and the moon is a few hundred metres across on that grid.
    Place(_parent->GetWorldPosition() + offset);
    Scale(glm::vec3(_earthSizeCoefficient * SizeFactor()));
    if (_entry.artRollDegrees != 0.0f) {
        Rotate(_entry.artRollDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    }
    if (_entry.yawOffsetDegrees != 0.0f) {
        Rotate(_entry.yawOffsetDegrees, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    Rotate(SatelliteOrbit::SpinDegreesAt(_entry.spinDegPerSimSecond, julianDate), glm::vec3(0.0f, 1.0f, 0.0f));
}

float CatalogSatellite::SizeFactor() const {
    return SceneScale::BodySizeFactor(_entry);
}

float CatalogSatellite::OrbitSceneUnitsPerKm() const {
    // EphemerisOffset scales the parent-relative offset by exactly this (sceneOrbitRadius
    // over the semi-major axis on compressed scale, the true scale on realistic), so it
    // relates a real length near this orbit to its length on screen.
    return static_cast<float>(SceneScale::SatelliteSceneUnitsPerAu(_entry) / SceneScale::kKmPerAu);
}

CatalogMaterial::Textures CatalogSatellite::CurrentTextures() const {
    CatalogMaterial::Textures textures;
    textures.diffuse = _diffuses.at(0).GetTexture();
    textures.normal = _normalMap.GetTexture();
    if (_material.hasSpecular) {
        textures.specular = _specular.GetTexture();
    }
    // SatelliteInfo carries no night/cloud maps; the generator rejects a moon row that asks
    // for them, and Variant() drops both flags when their texture is 0.
    return textures;
}

void CatalogSatellite::Draw(const Shader& shader) const {
    BindMaterial(shader);
    SpaceObject::Draw(shader);
}

void CatalogSatellite::BindMaterial(const Shader& shader) const {
    CatalogMaterial::BindMaterial(shader, _material, CurrentTextures());
}
