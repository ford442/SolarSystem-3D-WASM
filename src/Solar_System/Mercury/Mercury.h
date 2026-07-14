#ifndef SOLARSYSTEM_MERCURY_H
#define SOLARSYSTEM_MERCURY_H
#include "../Planet.h"

class Mercury : public Planet {
public:
    explicit Mercury(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    std::string _diffuseLowPath = TexturePaths::Mercury::Diffuse.low;
    std::string _diffuseHighPath = TexturePaths::Mercury::Diffuse.high;
    std::string _normalLowPath = TexturePaths::Mercury::Normal.low;
    std::string _normalHighPath = TexturePaths::Mercury::Normal.high;
    std::string _specularLowPath = TexturePaths::Mercury::Specular.low;
    std::string _specularHighPath = TexturePaths::Mercury::Specular.high;

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 3;
};


#endif //SOLARSYSTEM_MERCURY_H
