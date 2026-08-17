#include "Neptune.h"
#include <glm/glm.hpp>
#include "../OrbitLayout.h"

Neptune::Neptune(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Neptune));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Neptune::Diffuse.low, TexturePaths::Neptune::Diffuse.mid,
                     TexturePaths::Neptune::Diffuse.high, "Neptune_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Neptune::Normal.low, TexturePaths::Neptune::Normal.mid,
                     TexturePaths::Neptune::Normal.high, "Neptune_Normal", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Neptune::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Neptune));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(28.3f, glm::vec3(0, 0, 1));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Neptune), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Neptune::Render() const {
    GetShader().SetBool("hasNightTexture", false);
    GetShader().SetBool("hasSpecularMap", false);
    GetShader().SetBool("hasSpecular", false);
    GetShader().SetBool("hasClouds", true);
    GetShader().SetBool("isUseSphereIntersect", false);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("cloudTexture", 1);
    GetShader().SetInt("normalMap", 2);
    GetShader().SetFloat("ambientFactor", 0.0f);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _diffuses.at(1).GetTexture());
    glBindTextureUnit(2, _normalMap.GetTexture());

    SpaceObject::Render();

    GetShader().SetBool("hasClouds", false);
}

void Neptune::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High;
}

void Neptune::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
