#ifndef SOLARSYSTEM_VENUS_H
#define SOLARSYSTEM_VENUS_H
#include "../Planet.h"

class Venus : public Planet {
public:
    explicit Venus(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    std::string _diffuseLowPath = TexturePaths::Venus::Diffuse.low;
    std::string _diffuseHighPath = TexturePaths::Venus::Diffuse.high;
    std::string _normalLowPath = TexturePaths::Venus::Normal.low;
    std::string _normalHighPath = TexturePaths::Venus::Normal.high;

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 2;
};

#endif //SOLARSYSTEM_VENUS_H
