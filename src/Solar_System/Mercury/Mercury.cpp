#include "Mercury.h"

Mercury::Mercury(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + glm::vec3(1500.f, 0.0f, 350.0f)); // Init position for light space matrix
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
#else
    _isHighResLoaded = true;
#endif
}

void Mercury::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 4 *  0.00075;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(1500.f, 0.0f, 350.0f));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
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
#ifndef __EMSCRIPTEN__
    if (_isHighResLoaded) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Mercury: " << distance << " units. Loading high-res textures..." << std::endl;

        try {
            _diffuses.at(0).ReloadTexture(_diffuseHighPath);
            _normalMap.ReloadTexture(_normalHighPath);
            _specular.ReloadTexture(_specularHighPath);

            _isHighResLoaded = true;
            std::cout << "[LOD] Mercury high-res textures loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[LOD] ERROR: Failed to load high-res textures for Mercury: " << e.what() << std::endl;
        }
    }
#endif
}
