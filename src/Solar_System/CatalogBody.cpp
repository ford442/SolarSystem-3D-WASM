#include "CatalogBody.h"

#include <glm/glm.hpp>

CatalogBody::CatalogBody(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar,
                         const BodyCatalog::Entry& entry)
    : Planet(planetInfo, std::move(parentStar)),
      _entry(entry),
      _body(static_cast<OrbitLayout::Body>(entry.index)),
      _hasSpecular(entry.lod.specular != nullptr),
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

    if (_hasSpecular) {
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
    Rotate(OrbitLayout::GetAxialSpinDegrees(_body), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void CatalogBody::Render() const {
    GetShader().SetBool("hasNightTexture", _entry.shaderFlags.hasNightTexture);
    GetShader().SetBool("hasSpecularMap", _hasSpecular);
    GetShader().SetBool("hasSpecular", _hasSpecular);
    GetShader().SetBool("isUseSphereIntersect", false);
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

void CatalogBody::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High &&
                       _normalLOD.GetResidentTier() == TextureLodTier::High;

    if (_hasSpecular) {
        _specularLOD.Update(cameraPos, GetPosition(), lodThreshold);
        _isHighResLoading = _isHighResLoading || _specularLOD.IsLoading();
        _isHighResLoaded = _isHighResLoaded && _specularLOD.GetResidentTier() == TextureLodTier::High;
    }
}

void CatalogBody::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
