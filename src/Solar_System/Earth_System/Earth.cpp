#include "Earth.h"
#include <glm/glm.hpp>
#include <iostream>

Earth::Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + glm::vec3(1900.0f, 0.0f, 0.0f)); // Init position for light space matrix
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false; // Start with low-res for web
#else
    _isHighResLoaded = true; // Desktop loads high-res directly
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
#ifdef __EMSCRIPTEN__
    // Check if already high-res
    if (_isHighResLoaded) {
        return;
    }
    
    // Calculate distance from camera to planet center
    float distance = glm::length(cameraPos - GetPosition());
    
    // If camera is close enough, load high-res textures
    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Earth: " << distance << " units. Loading high-res textures..." << std::endl;
        
        // Reload diffuse texture (day texture at index 0)
        _diffuses.at(0).ReloadTexture(_diffuseHighPath);
        std::cout << "[LOD] Earth Day Diffuse texture upgraded to high-res" << std::endl;
        
        // Reload normal map
        _normalMap.ReloadTexture(_normalHighPath);
        std::cout << "[LOD] Earth Normal Map upgraded to high-res" << std::endl;
        
        // Reload specular map
        _specular.ReloadTexture(_specularHighPath);
        std::cout << "[LOD] Earth Specular Map upgraded to high-res" << std::endl;
        
        _isHighResLoaded = true;
        std::cout << "[LOD] Earth high-res textures loaded successfully" << std::endl;
    }
#endif
}
