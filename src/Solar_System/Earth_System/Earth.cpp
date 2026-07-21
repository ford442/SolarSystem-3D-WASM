#include "Earth.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"
#include "../OrbitLayout.h"
#include <glm/glm.hpp>
#include <iostream>

Earth::Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Earth));
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
    float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    if (_isHighResLoaded) {
        if (distance > lodThreshold * 2.0f) {
            // Downgrade with hysteresis to free GPU memory. Low-res assets are already in MEMFS.
            std::cout << "[LOD] Camera distance to Earth: " << distance << " units. Downgrading high-res to low-res..." << std::endl;
            _diffuses.at(0).ReloadTexture(_diffuseLowPath);
            _normalMap.ReloadTexture(_normalLowPath);
            _specular.ReloadTexture(_specularLowPath);
            _isHighResLoaded = false;
            _isHighResLoading = false;
            _highResTexturesLoaded = 0;
            _highResTexturesProcessed = 0;
            std::cout << "[LOD] Earth high-res textures downgraded (VRAM freed)" << std::endl;
            return;
        }
        return;
    }

#ifdef __EMSCRIPTEN__
    if (g_qualityPreset == 0) return; // Low preset: never load/keep high-res
#endif

    if (_isHighResLoading) {
        // Deprioritize: if camera has moved well away, cancel any queued/in-flight applies
        if (distance > lodThreshold * 1.8f) {
            std::cout << "[LOD] Camera moved away from Earth during load (dist=" << distance
                      << ") — deprioritizing high-res." << std::endl;
            auto& queue = TextureLoadingQueue::GetInstance();
            queue.CancelLoad(_diffuseHighPath);
            queue.CancelLoad(_normalHighPath);
            queue.CancelLoad(_specularHighPath);
            _isHighResLoading = false;
            // do not force _loaded=true; allows re-attempt on next close pass
            return;
        }
        return;
    }

    if (distance < lodThreshold) {
        std::cout << "[LOD] Camera distance to Earth: " << distance << " units. Queueing high-res textures..." << std::endl;
        _isHighResLoading = true;
        _highResTexturesLoaded = 0;
        _highResTexturesProcessed = 0;
        _highResLoadProgress = 0.0f;

        auto& queue = TextureLoadingQueue::GetInstance();

        auto onLoaded = [this](bool success) {
            _highResTexturesProcessed++;
            if (success) _highResTexturesLoaded++;
            _highResLoadProgress = _highResTexturesLoaded / (float)_highResTextureCount;
            if (_highResTexturesProcessed == _highResTextureCount) {
                _isHighResLoaded = (_highResTexturesLoaded == _highResTextureCount);
                _isHighResLoading = false;
                if (_highResTexturesLoaded == _highResTextureCount) {
                    std::cout << "[LOD] Earth high-res textures loaded successfully" << std::endl;
                } else {
                    std::cout << "[LOD] Earth high-res attempt finished (some or all failed, keeping low-res permanently)" << std::endl;
                }
            }
        };

        queue.QueueTextureLoad(_diffuseHighPath, "Earth_Day_Diffuse_High", &_diffuses.at(0), onLoaded);
        queue.QueueTextureLoad(_normalHighPath, "Earth_Normal_High", &_normalMap, onLoaded);
        queue.QueueTextureLoad(_specularHighPath, "Earth_Specular_High", &_specular, onLoaded);
    }
}

void Earth::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    // Delegate to LoadHighResIfClose which now handles downgrade when far.
    // Kept for API symmetry / future.
    LoadHighResIfClose(cameraPos);
}
