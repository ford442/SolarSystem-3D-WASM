#include "Uranus.h"

Uranus::Uranus(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + glm::vec3(0.0f, 0.0f, -2650.0f)); // Init position for light space matrix
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
#else
    _isHighResLoaded = true;
#endif
}

void Uranus::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 2 * 0.009575;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(0.0f, 0.0f, -2650.0f));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(81.2f, glm::vec3(1, 0, 0));
    Rotate(rotationAngle, glm::vec3(0, 1, 0));
    UpdateModelMatrix();
}

void Uranus::Render() const {
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

void Uranus::LoadHighResIfClose(const glm::vec3& cameraPos) {
#ifdef __EMSCRIPTEN__
    if (_isHighResLoaded) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Uranus: " << distance << " units. Loading high-res textures..." << std::endl;

        try {
            _diffuses.at(0).ReloadTexture(_diffuseHighPath);
            _normalMap.ReloadTexture(_normalHighPath);

            _isHighResLoaded = true;
            std::cout << "[LOD] Uranus high-res textures loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[LOD] ERROR: Failed to load high-res textures for Uranus: " << e.what() << std::endl;
        }
    }
#endif
}
