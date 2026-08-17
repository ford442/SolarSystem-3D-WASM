#include "Saturn.h"
#include <glm/glm.hpp>
#include "../OrbitLayout.h"

Saturn::Saturn(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Saturn));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Saturn::Diffuse.low, TexturePaths::Saturn::Diffuse.mid,
                     TexturePaths::Saturn::Diffuse.high, "Saturn_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Saturn::Normal.low, TexturePaths::Saturn::Normal.mid,
                     TexturePaths::Saturn::Normal.high, "Saturn_Normal", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Saturn::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Saturn));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(-26.7f, glm::vec3(0, 0, 1));
    Rotate(-15.f, glm::vec3(1, 0, 0));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Saturn), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Saturn::Render() const {
    GetShader().SetBool("hasNightTexture", false);
    GetShader().SetBool("hasSpecularMap", false);
    GetShader().SetBool("hasSpecular", false);
    GetShader().SetBool("isUseSphereIntersect", false);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("normalMap", 1);
    GetShader().SetFloat("ambientFactor", 0.0f);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _normalMap.GetTexture());

    SpaceObject::Render();
}

void Saturn::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High;
}

void Saturn::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
