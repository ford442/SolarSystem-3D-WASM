#ifndef SOLARSYSTEM_PLUTO_H
#define SOLARSYSTEM_PLUTO_H
#include "../Planet.h"

class Pluto : public Planet {
public:
    explicit Pluto(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    std::string _diffuseLowPath = TexturePaths::Pluto::Diffuse.low;
    std::string _diffuseHighPath = TexturePaths::Pluto::Diffuse.high;
    std::string _normalLowPath = TexturePaths::Pluto::Normal.low;
    std::string _normalHighPath = TexturePaths::Pluto::Normal.high;
    std::string _specularLowPath = TexturePaths::Pluto::Specular.low;
    std::string _specularHighPath = TexturePaths::Pluto::Specular.high;

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 3;
};

#endif //SOLARSYSTEM_PLUTO_H
