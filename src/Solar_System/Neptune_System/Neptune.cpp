#include "Neptune.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"

Neptune::Neptune(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + glm::vec3(-2900.0f, 0.0f, 0.0f));
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Neptune::AdjustToParent(float timeScale) {
    static float rotationAngle = 0;

    if (timeScale > 0.0f) {
        rotationAngle += (4 * 0.01f * timeScale) * timeScale;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(-2900.0f, 0.0f, 0.0f));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(28.3f, glm::vec3(0, 0, 1));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
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
    float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    if (_isHighResLoaded) {
        if (distance > lodThreshold * 2.0f) {
            std::cout << "[LOD] Camera distance to Neptune: " << distance << " units. Downgrading high-res to low-res..." << std::endl;
            _diffuses.at(0).ReloadTexture(_diffuseLowPath);
            _normalMap.ReloadTexture(_normalLowPath);
            _isHighResLoaded = false;
            _isHighResLoading = false;
            _highResTexturesLoaded = 0;
            _highResTexturesProcessed = 0;
            std::cout << "[LOD] Neptune high-res textures downgraded (VRAM freed)" << std::endl;
            return;
        }
        if (_isHighResLoading) return; // shouldn't be
        return;
    }

#ifdef __EMSCRIPTEN__
    if (g_qualityPreset == 0) return; // Low preset: never load high-res
#endif

    if (_isHighResLoading) {
        if (distance > lodThreshold * 1.8f) {
            std::cout << "[LOD] Camera moved away from Neptune during load (dist=" << distance << ") — deprioritizing." << std::endl;
            auto& queue = TextureLoadingQueue::GetInstance();
            queue.CancelLoad(_diffuseHighPath);
            queue.CancelLoad(_normalHighPath);
            _isHighResLoading = false;
            return;
        }
        return;
    }

    if (distance < lodThreshold) {
        std::cout << "[LOD] Camera distance to Neptune: " << distance << " units. Queueing high-res textures..." << std::endl;
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
                    std::cout << "[LOD] Neptune high-res textures loaded successfully" << std::endl;
                } else {
                    std::cout << "[LOD] Neptune high-res attempt finished (some or all failed, keeping low-res permanently)" << std::endl;
                }
            }
        };

        queue.QueueTextureLoad(_diffuseHighPath, "Neptune_Diffuse_High", &_diffuses.at(0), onLoaded);
        queue.QueueTextureLoad(_normalHighPath, "Neptune_Normal_High", &_normalMap, onLoaded);
    }
}

void Neptune::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
