#include "Neptune.h"

Neptune::Neptune(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + glm::vec3(-2900.0f, 0.0f, 0.0f)); // Init position for light space matrix
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
#else
    _isHighResLoaded = true;
#endif
}

void Neptune::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 4 *  0.01;
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
#ifndef __EMSCRIPTEN__
    if (_isHighResLoaded) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Neptune: " << distance << " units. Loading high-res textures..." << std::endl;

        try {
            _diffuses.at(0).ReloadTexture(_diffuseHighPath);
            _normalMap.ReloadTexture(_normalHighPath);

            _isHighResLoaded = true;
            std::cout << "[LOD] Neptune high-res textures loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[LOD] ERROR: Failed to load high-res textures for Neptune: " << e.what() << std::endl;
        }
    }
#endif
}
