#include "Uranus.h"
#include <glm/glm.hpp>
#include "../OrbitLayout.h"

Uranus::Uranus(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Uranus));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Uranus::Diffuse.low, TexturePaths::Uranus::Diffuse.mid,
                     TexturePaths::Uranus::Diffuse.high, "Uranus_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Uranus::Normal.low, TexturePaths::Uranus::Normal.mid,
                     TexturePaths::Uranus::Normal.high, "Uranus_Normal", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Uranus::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Uranus));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(81.2f, glm::vec3(1, 0, 0));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Uranus), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Uranus::Render() const {
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

void Uranus::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High;
}

void Uranus::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
