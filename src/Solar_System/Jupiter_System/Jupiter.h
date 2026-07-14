#ifndef SOLARSYSTEM_JUPITER_H
#define SOLARSYSTEM_JUPITER_H
#include "../Planet.h"

class Jupiter : public Planet {
public:
    explicit Jupiter(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    std::string _diffuseLowPath = TexturePaths::Jupiter::Diffuse.low;
    std::string _diffuseHighPath = TexturePaths::Jupiter::Diffuse.high;
    std::string _normalLowPath = TexturePaths::Jupiter::Normal.low;
    std::string _normalHighPath = TexturePaths::Jupiter::Normal.high;

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 2;
};

#endif //SOLARSYSTEM_JUPITER_H
