#include "Venus.h"

Venus::Venus(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + glm::vec3(1125.0f, 0.0f, -1340.0f)); // Init position for light space matrix
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
#else
    _isHighResLoaded = true;
#endif
}

void Venus::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 4 *  0.00075;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(1125.0f, 0.0f, -1340.0f));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(177.3f, glm::vec3(0, 0, 1));
    Rotate(-rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Venus::Render() const {
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

void Venus::LoadHighResIfClose(const glm::vec3& cameraPos) {
#ifdef __EMSCRIPTEN__
    if (_isHighResLoaded) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Venus: " << distance << " units. Loading high-res textures..." << std::endl;

        try {
            _diffuses.at(0).ReloadTexture(_diffuseHighPath);
            _normalMap.ReloadTexture(_normalHighPath);

            _isHighResLoaded = true;
            std::cout << "[LOD] Venus high-res textures loaded successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[LOD] ERROR: Failed to load high-res textures for Venus: " << e.what() << std::endl;
        }
    }
#endif
}
