#include "Jupiter.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"
#include "../OrbitLayout.h"

Jupiter::Jupiter(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + OrbitLayout::GetOffset(OrbitLayout::Body::Jupiter));
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
    float distance = glm::length(cameraPos - GetPosition());
    const float lodThreshold = GetEffectiveLODThreshold();
    _lastCameraDistance = distance;

    if (_isHighResLoaded) {
        if (distance > lodThreshold * 2.0f) {
            std::cout << "[LOD] Camera distance to Jupiter: " << distance << " units. Downgrading high-res to low-res..." << std::endl;
            _diffuses.at(0).ReloadTexture(_diffuseLowPath);
            _normalMap.ReloadTexture(_normalLowPath);
            _isHighResLoaded = false;
            _isHighResLoading = false;
            _highResTexturesLoaded = 0;
            _highResTexturesProcessed = 0;
            std::cout << "[LOD] Jupiter high-res textures downgraded (VRAM freed)" << std::endl;
            return;
        }
        return;
    }

#ifdef __EMSCRIPTEN__
    if (g_qualityPreset == 0) return; // Low preset: never load high-res
#endif

    if (_isHighResLoading) {
        if (distance > lodThreshold * 1.8f) {
            std::cout << "[LOD] Camera moved away from Jupiter during load (dist=" << distance << ") — deprioritizing." << std::endl;
            auto& queue = TextureLoadingQueue::GetInstance();
            queue.CancelLoad(_diffuseHighPath);
            queue.CancelLoad(_normalHighPath);
            _isHighResLoading = false;
            // allow re-attempt later
            return;
        }
        return;
    }

    if (distance < lodThreshold) {
        std::cout << "[LOD] Camera distance to Jupiter: " << distance << " units. Queueing high-res textures..." << std::endl;
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
                    std::cout << "[LOD] Jupiter high-res textures loaded successfully" << std::endl;
                } else {
                    std::cout << "[LOD] Jupiter high-res attempt finished (some or all failed, keeping low-res permanently)" << std::endl;
                }
            }
        };

        queue.QueueTextureLoad(_diffuseHighPath, "Jupiter_Diffuse_High", &_diffuses.at(0), onLoaded);
        queue.QueueTextureLoad(_normalHighPath, "Jupiter_Normal_High", &_normalMap, onLoaded);
    }
}

void Jupiter::UnloadHighResIfFar(const glm::vec3& cameraPos) {
    LoadHighResIfClose(cameraPos);
}
