#ifndef SOLARSYSTEM_EARTH_H
#define SOLARSYSTEM_EARTH_H
#include "../Planet.h"
#include "../../Auxiliary_Modules/TextureLODController.h"

class Earth : public Planet {
public:
    explicit Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;
    float GetHighResLoadProgress() const {
        int upgraded = 0;
        if (_diffuseLOD.GetResidentTier() > TextureLodTier::Low) ++upgraded;
        if (_normalLOD.GetResidentTier() > TextureLodTier::Low) ++upgraded;
        if (_specularLOD.GetResidentTier() > TextureLodTier::Low) ++upgraded;
        return upgraded / 3.0f;
    }
    bool IsHighResLoading() const {
        return _diffuseLOD.IsLoading() || _normalLOD.IsLoading() || _specularLOD.IsLoading();
    }

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    TextureLODController _diffuseLOD;
    TextureLODController _normalLOD;
    TextureLODController _specularLOD;
};

#endif //Earth
