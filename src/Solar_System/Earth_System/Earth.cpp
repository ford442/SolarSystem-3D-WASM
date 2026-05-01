#include "Earth.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"
#include <glm/glm.hpp>
#include <iostream>

Earth::Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + glm::vec3(1900.0f, 0.0f, 0.0f));
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Earth::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 0.0075;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(1900.0f, 0.0f, 0.0f));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(-23.4f, glm::vec3(0, 0, 1));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
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
    if (_isHighResLoaded || _isHighResLoading) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Earth: " << distance << " units. Queueing high-res textures..." << std::endl;
        _isHighResLoading = true;
        _highResTexturesLoaded = 0;
        _highResLoadProgress = 0.0f;

        auto& queue = TextureLoadingQueue::GetInstance();

        queue.QueueTextureLoad(_diffuseHighPath, "Earth_Day_Diffuse_High", &_diffuses.at(0),
            [this](bool success) {
                if (success) _highResTexturesLoaded++;
                _highResLoadProgress = _highResTexturesLoaded / (float)_highResTextureCount;
                if (_highResTexturesLoaded == _highResTextureCount) {
                    _isHighResLoaded = true;
                    _isHighResLoading = false;
                    std::cout << "[LOD] Earth high-res textures loaded successfully" << std::endl;
                }
            });

        queue.QueueTextureLoad(_normalHighPath, "Earth_Normal_High", &_normalMap,
            [this](bool success) {
                if (success) _highResTexturesLoaded++;
                _highResLoadProgress = _highResTexturesLoaded / (float)_highResTextureCount;
                if (_highResTexturesLoaded == _highResTextureCount) {
                    _isHighResLoaded = true;
                    _isHighResLoading = false;
                    std::cout << "[LOD] Earth high-res textures loaded successfully" << std::endl;
                }
            });

        queue.QueueTextureLoad(_specularHighPath, "Earth_Specular_High", &_specular,
            [this](bool success) {
                if (success) _highResTexturesLoaded++;
                _highResLoadProgress = _highResTexturesLoaded / (float)_highResTextureCount;
                if (_highResTexturesLoaded == _highResTextureCount) {
                    _isHighResLoaded = true;
                    _isHighResLoading = false;
                    std::cout << "[LOD] Earth high-res textures loaded successfully" << std::endl;
                }
            });
    }
}
