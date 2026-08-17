#include "Earth.h"
#include "../OrbitLayout.h"
#include <glm/glm.hpp>

Earth::Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Earth));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Earth::Diffuse.low, TexturePaths::Earth::Diffuse.mid,
                     TexturePaths::Earth::Diffuse.high, "Earth_Day_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Earth::Normal.low, TexturePaths::Earth::Normal.mid,
                     TexturePaths::Earth::Normal.high, "Earth_Normal", TextureLoadCategory::Planet);
    _specularLOD.Configure(_specular, TexturePaths::Earth::Specular.low, TexturePaths::Earth::Specular.mid,
                     TexturePaths::Earth::Specular.high, "Earth_Specular", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Earth::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Earth));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(-23.4f, glm::vec3(0, 0, 1));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Earth), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Earth::Render() const {
    GetShader().SetBool("hasNightTexture", true);
    GetShader().SetBool("hasSpecularMap", true);
    GetShader().SetBool("hasSpecular", true);
    GetShader().SetBool("hasClouds", true);
    GetShader().SetBool("isUseSphereIntersect", false);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("cloudTexture", 1);
    GetShader().SetInt("nightTexture", 2);
    GetShader().SetInt("normalMap", 3);
    GetShader().SetInt("specularMap", 4);
    GetShader().SetFloat("ambientFactor", 0.75f);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _diffuses.at(1).GetTexture());
    glBindTextureUnit(2, _diffuses.at(2).GetTexture());
    glBindTextureUnit(3, _normalMap.GetTexture());
    glBindTextureUnit(4, _specular.GetTexture());

    SpaceObject::Render();

    GetShader().SetBool("hasClouds", false);
}

void Earth::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _specularLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading() || _specularLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High && _specularLOD.GetResidentTier() == TextureLodTier::High;
}

void Earth::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
