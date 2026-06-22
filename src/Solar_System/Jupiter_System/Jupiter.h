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

    std::string _diffuseLowPath = "resource/textures_low/Jupiter_Diffuse_Low.dds";
    std::string _diffuseHighPath = "resource/textures/Jupiter_Diffuse.dds";
    std::string _normalLowPath = "resource/textures_low/Jupiter_Normal_Low.dds";
    std::string _normalHighPath = "resource/textures/Jupiter_Normal.dds";

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 2;
};

#endif //SOLARSYSTEM_JUPITER_H
