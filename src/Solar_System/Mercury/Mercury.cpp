#include "Mercury.h"
#include <glm/glm.hpp>
#include "../OrbitLayout.h"

Mercury::Mercury(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Mercury));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Mercury::Diffuse.low, TexturePaths::Mercury::Diffuse.mid,
                     TexturePaths::Mercury::Diffuse.high, "Mercury_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Mercury::Normal.low, TexturePaths::Mercury::Normal.mid,
                     TexturePaths::Mercury::Normal.high, "Mercury_Normal", TextureLoadCategory::Planet);
    _specularLOD.Configure(_specular, TexturePaths::Mercury::Specular.low, TexturePaths::Mercury::Specular.mid,
                     TexturePaths::Mercury::Specular.high, "Mercury_Specular", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Mercury::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Mercury));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Mercury), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Mercury::Render() const {
    GetShader().SetBool("hasNightTexture", false);
    GetShader().SetBool("hasSpecularMap", true);
    GetShader().SetBool("hasSpecular", true);
    GetShader().SetBool("isUseSphereIntersect", false);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("normalMap", 1);
    GetShader().SetInt("specularMap", 2);
    GetShader().SetFloat("ambientFactor", 0.0f);

    glBindTextureUnit(0, _diffuses.at(0).GetTexture());
    glBindTextureUnit(1, _normalMap.GetTexture());
    glBindTextureUnit(2, _specular.GetTexture());

    SpaceObject::Render();
}

void Mercury::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _specularLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading() || _specularLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High && _specularLOD.GetResidentTier() == TextureLodTier::High;
}

void Mercury::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
