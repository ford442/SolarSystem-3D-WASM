#include "Pluto.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"

Pluto::Pluto(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap), _specular(planetInfo.specularTexture)
{
    Translate(_parentStar->GetPosition() + glm::vec3(2800.0f, 0.0f, 1757.73f));
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Pluto::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 4 *  0.0075;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(2800.0f, 0.0f, 1757.73f));
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
    if (_isHighResLoaded || _isHighResLoading) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
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
                _isHighResLoaded = true;  // Permanently finalized: prevents re-queuing regardless of outcome
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
