#include "CatalogBody.h"

#include <glm/glm.hpp>

CatalogBody::CatalogBody(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar,
                         const BodyCatalog::Entry& entry)
    : Planet(planetInfo, std::move(parentStar)),
      _entry(entry),
      _body(static_cast<OrbitLayout::Body>(entry.index)),
      _hasSpecular(entry.lod.specular != nullptr),
      _hasNight(entry.shaderFlags.hasNightTexture),
      _hasClouds(entry.shaderFlags.hasClouds),
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
    Rotate(_entry.axialTiltDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    if (_entry.artTiltXDegrees != 0.0f) {
        Rotate(_entry.artTiltXDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    Rotate(OrbitLayout::GetAxialSpinDegrees(_body), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void CatalogBody::Render() const {
    GetShader().SetBool("hasNightTexture", _hasNight);
    GetShader().SetBool("hasSpecularMap", _hasSpecular);
    GetShader().SetBool("hasSpecular", _hasSpecular);
    GetShader().SetBool("hasClouds", _hasClouds);
    GetShader().SetBool("isUseSphereIntersect", _entry.useSphereIntersect);
    GetShader().SetFloat("ambientFactor", _entry.ambientFactor);

    int unit = 0;
    GetShader().SetInt("mainDiffuseTexture", unit);
    glBindTextureUnit(unit++, _diffuses.at(0).GetTexture());

    if (_hasClouds && _diffuses.size() > 1) {
        GetShader().SetInt("cloudTexture", unit);
        glBindTextureUnit(unit++, _diffuses.at(1).GetTexture());
    }
    if (_hasNight) {
        const size_t nightIndex = _hasClouds ? 2 : 1;
        if (_diffuses.size() > nightIndex) {
            GetShader().SetInt("nightTexture", unit);
            glBindTextureUnit(unit++, _diffuses.at(nightIndex).GetTexture());
        }
    }

    GetShader().SetInt("normalMap", unit);
    glBindTextureUnit(unit++, _normalMap.GetTexture());
    if (_hasSpecular) {
        GetShader().SetInt("specularMap", unit);
        glBindTextureUnit(unit, _specular.GetTexture());
    }

    SpaceObject::Render();

    if (_hasClouds) {
        GetShader().SetBool("hasClouds", false);
    }
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
