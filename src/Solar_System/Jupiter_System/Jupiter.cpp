#include "Jupiter.h"
#include <glm/glm.hpp>
#include "../OrbitLayout.h"

Jupiter::Jupiter(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Jupiter));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Jupiter::Diffuse.low, TexturePaths::Jupiter::Diffuse.mid,
                     TexturePaths::Jupiter::Diffuse.high, "Jupiter_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Jupiter::Normal.low, TexturePaths::Jupiter::Normal.mid,
                     TexturePaths::Jupiter::Normal.high, "Jupiter_Normal", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Jupiter::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Jupiter));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(-3.1f, glm::vec3(0, 0, 1));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Jupiter), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Jupiter::Render() const {
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

void Jupiter::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High;
}

void Jupiter::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
