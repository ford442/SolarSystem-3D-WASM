#ifndef SOLARSYSTEM_EARTH_H
#define SOLARSYSTEM_EARTH_H
#include "../Planet.h"

class Earth : public Planet {
public:
    explicit Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;
    float GetHighResLoadProgress() const { return _highResLoadProgress; }
    bool IsHighResLoading() const { return _isHighResLoading; }

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    // LOD texture paths
    std::string _diffuseLowPath = TexturePaths::Earth::Diffuse.low;
    std::string _diffuseHighPath = TexturePaths::Earth::Diffuse.high;
    std::string _normalLowPath = TexturePaths::Earth::Normal.low;
    std::string _normalHighPath = TexturePaths::Earth::Normal.high;
    std::string _specularLowPath = TexturePaths::Earth::Specular.low;
    std::string _specularHighPath = TexturePaths::Earth::Specular.high;

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 3;
};

#endif //SOLARSYSTEM_EARTH_H
