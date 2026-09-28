#include "CatalogBody.h"

#include <glm/glm.hpp>

CatalogBody::CatalogBody(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar,
                         const BodyCatalog::Entry& entry)
    : Planet(planetInfo, std::move(parentStar)),
      _entry(entry),
      _body(static_cast<OrbitLayout::Body>(entry.index)),
      _material(CatalogMaterial::Material::FromEntry(entry)),
      _diffuses(planetInfo.diffuseTextures),
      _normalMap(planetInfo.normalMap),
      _specular(planetInfo.specularTexture) {
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(_body));

    const TexturePaths::Paths diffuse = TexturePaths::ForTextureId(entry.lod.diffuse);
    _diffuseLOD.Configure(_diffuses.at(0), diffuse.low, diffuse.mid, diffuse.high, entry.lod.diffuse,
                          TextureLoadCategory::Planet);

    const TexturePaths::Paths normal = TexturePaths::ForTextureId(entry.lod.normal);
    _normalLOD.Configure(_normalMap, normal.low, normal.mid, normal.high, entry.lod.normal,
                         TextureLoadCategory::Planet);

    if (_material.hasSpecular) {
        const TexturePaths::Paths specular = TexturePaths::ForTextureId(entry.lod.specular);
        _specularLOD.Configure(_specular, specular.low, specular.mid, specular.high, entry.lod.specular,
                               TextureLoadCategory::Planet);
    }

#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void CatalogBody::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(_body));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(_entry.axialTiltDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    if (_entry.artTiltXDegrees != 0.0f) {
        Rotate(_entry.artTiltXDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    Rotate(OrbitLayout::GetAxialSpinDegrees(_body), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void CatalogBody::Render() const {
    // PlanetInfo::diffuseTextures is ordered [day, clouds?, night?] (see MakePlanetInfo).
    CatalogMaterial::Textures textures;
    textures.diffuse = _diffuses.at(0).GetTexture();
    textures.normal = _normalMap.GetTexture();
    if (_material.hasSpecular) {
        textures.specular = _specular.GetTexture();
    }
    size_t next = 1;
    if (_material.hasClouds && _diffuses.size() > next) {
        textures.clouds = _diffuses.at(next++).GetTexture();
    }
    if (_material.hasNight && _diffuses.size() > next) {
        textures.night = _diffuses.at(next).GetTexture();
    }

    CatalogMaterial::BindMaterial(GetShader(), _material, textures);
    SpaceObject::Render();
}

void CatalogBody::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High &&
                       _normalLOD.GetResidentTier() == TextureLodTier::High;

    if (_material.hasSpecular) {
        _specularLOD.Update(cameraPos, GetPosition(), lodThreshold);
        _isHighResLoading = _isHighResLoading || _specularLOD.IsLoading();
        _isHighResLoaded = _isHighResLoaded && _specularLOD.GetResidentTier() == TextureLodTier::High;
    }
}

void CatalogBody::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
