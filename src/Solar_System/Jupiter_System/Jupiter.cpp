#include "Jupiter.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"

Jupiter::Jupiter(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + glm::vec3(1350.f, 0.0f, 1737.0f));
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Jupiter::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 4 *  0.014;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(1350.f, 0.0f, 1737.0f));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(-3.1f, glm::vec3(0, 0, 1));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
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
    if (_isHighResLoaded || _isHighResLoading) {
        return;
    }

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
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
                _isHighResLoaded = true;  // Permanently finalized: prevents re-queuing regardless of outcome
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
