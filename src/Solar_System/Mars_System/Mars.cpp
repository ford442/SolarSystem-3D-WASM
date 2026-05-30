#include "Mars.h"
#include "../../Auxiliary_Modules/TextureLoadingQueue.h"

Mars::Mars(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar) : Planet(planetInfo, std::move(parentStar)), _diffuses(planetInfo.diffuseTextures),
    _normalMap(planetInfo.normalMap)
{
    Translate(_parentStar->GetPosition() + glm::vec3(-1732.0f, 0.0f, 1000.0f));
#ifdef __EMSCRIPTEN__
    _isHighResLoaded = false;
    _isHighResLoading = false;
#else
    _isHighResLoaded = true;
#endif
}

void Mars::AdjustToParent(bool isRunTime) {
    static float rotationAngle = 0;

    if (isRunTime) {
        rotationAngle += 0.0065;
    }

    LoadIdentityModelMatrix();
    Translate(_parentStar->GetPosition() + glm::vec3(-1732.0f, 0.0f, 1000.0f));
    Rotate(-25.2f, glm::vec3(0, 0, 1));
    Scale(glm::vec3(_earthSizeCoefficient));
    Rotate(rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void Mars::Render() const {
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

void Mars::LoadHighResIfClose(const glm::vec3& cameraPos) {
    if (_isHighResLoaded || _isHighResLoading) {
        return;
    }

#ifdef __EMSCRIPTEN__
    // High-res LOD temporarily disabled on WASM for stability (see Earth.cpp for rationale).
    // Low-res + fallback textures guarantee no solid black planets/moons.
    _isHighResLoaded = true;
    return;
#endif

    float distance = glm::length(cameraPos - GetPosition());

    if (distance < _lodThreshold) {
        std::cout << "[LOD] Camera distance to Mars: " << distance << " units. Queueing high-res textures..." << std::endl;
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
                    std::cout << "[LOD] Mars high-res textures loaded successfully" << std::endl;
                } else {
                    std::cout << "[LOD] Mars high-res attempt finished (some or all failed, keeping low-res permanently)" << std::endl;
                }
            }
        };

        queue.QueueTextureLoad(_diffuseHighPath, "Mars_Diffuse_High", &_diffuses.at(0), onLoaded);
        queue.QueueTextureLoad(_normalHighPath, "Mars_Normal_High", &_normalMap, onLoaded);
    }
}
