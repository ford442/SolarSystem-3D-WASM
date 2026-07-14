#include "Pluto.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"
#include "../OrbitLayout.h"

Pluto::Pluto(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Pluto));
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Pluto::AdjustToParent(float timeScale) {
    static float rotationAngle = 0;

    if (timeScale > 0.0f) {
        rotationAngle += (4 * 0.0075f * timeScale) * timeScale;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Pluto));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
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
    float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    if (_isHighResLoaded) {
        if (distance > lodThreshold * 2.0f) {
            std::cout << "[LOD] Camera distance to Pluto: " << distance << " units. Downgrading high-res to low-res..." << std::endl;
            _diffuses.at(0).ReloadTexture(_diffuseLowPath);
            _normalMap.ReloadTexture(_normalLowPath);
            _specular.ReloadTexture(_specularLowPath);
            _isHighResLoaded = false;
            _isHighResLoading = false;
            _highResTexturesLoaded = 0;
            _highResTexturesProcessed = 0;
            std::cout << "[LOD] Pluto high-res textures downgraded (VRAM freed)" << std::endl;
            return;
        }
        if (_isHighResLoading) return;
        return;
    }

#ifdef __EMSCRIPTEN__
    if (g_qualityPreset == 0) return; // Low preset: never load high-res
#endif

    if (_isHighResLoading) {
        if (distance > lodThreshold * 1.8f) {
            std::cout << "[LOD] Camera moved away from Pluto during load (dist=" << distance << ") — deprioritizing." << std::endl;
            auto& queue = TextureLoadingQueue::GetInstance();
            queue.CancelLoad(_diffuseHighPath);
            queue.CancelLoad(_normalHighPath);
            queue.CancelLoad(_specularHighPath);
            _isHighResLoading = false;
            return;
        }
        return;
    }

    if (distance < lodThreshold) {
        std::cout << "[LOD] Camera distance to Pluto: " << distance << " units. Queueing high-res textures..." << std::endl;
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
                    std::cout << "[LOD] Pluto high-res textures loaded successfully" << std::endl;
                } else {
                    std::cout << "[LOD] Pluto high-res attempt finished (some or all failed, keeping low-res permanently)" << std::endl;
                }
            }
        };

        queue.QueueTextureLoad(_diffuseHighPath, "Pluto_Diffuse_High", &_diffuses.at(0), onLoaded);
        queue.QueueTextureLoad(_normalHighPath, "Pluto_Normal_High", &_normalMap, onLoaded);
        queue.QueueTextureLoad(_specularHighPath, "Pluto_Specular_High", &_specular, onLoaded);
    }
}

void Pluto::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
