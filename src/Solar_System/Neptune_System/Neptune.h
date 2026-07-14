#ifndef SOLARSYSTEM_NEPTUNE_H
#define SOLARSYSTEM_NEPTUNE_H
#include "../Planet.h"

class Neptune : public Planet {
public:
    explicit Neptune(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    std::string _diffuseLowPath = TexturePaths::Neptune::Diffuse.low;
    std::string _diffuseHighPath = TexturePaths::Neptune::Diffuse.high;
    std::string _normalLowPath = TexturePaths::Neptune::Normal.low;
    std::string _normalHighPath = TexturePaths::Neptune::Normal.high;

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 2;
};

#endif //SOLARSYSTEM_NEPTUNE_H
