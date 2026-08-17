#include "Pluto.h"
#include <glm/glm.hpp>
#include "../OrbitLayout.h"

Pluto::Pluto(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Pluto));
    _diffuseLOD.Configure(_diffuses.at(0), TexturePaths::Pluto::Diffuse.low, TexturePaths::Pluto::Diffuse.mid,
                     TexturePaths::Pluto::Diffuse.high, "Pluto_Diffuse", TextureLoadCategory::Planet);
    _normalLOD.Configure(_normalMap, TexturePaths::Pluto::Normal.low, TexturePaths::Pluto::Normal.mid,
                     TexturePaths::Pluto::Normal.high, "Pluto_Normal", TextureLoadCategory::Planet);
    _specularLOD.Configure(_specular, TexturePaths::Pluto::Specular.low, TexturePaths::Pluto::Specular.mid,
                     TexturePaths::Pluto::Specular.high, "Pluto_Specular", TextureLoadCategory::Planet);
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Pluto::AdjustToParent(float /*timeScale*/) {
    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Pluto));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(OrbitLayout::GetAxialSpinDegrees(OrbitLayout::Body::Pluto), glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Pluto::Render() const {
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

void Pluto::LoadHighResIfClose(const glm::vec3& cameraPos) {
    const float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    _diffuseLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _normalLOD.Update(cameraPos, GetPosition(), lodThreshold);
    _specularLOD.Update(cameraPos, GetPosition(), lodThreshold);

    _isHighResLoading = _diffuseLOD.IsLoading() || _normalLOD.IsLoading() || _specularLOD.IsLoading();
    _isHighResLoaded = _diffuseLOD.GetResidentTier() == TextureLodTier::High && _normalLOD.GetResidentTier() == TextureLodTier::High && _specularLOD.GetResidentTier() == TextureLodTier::High;
}

void Pluto::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
