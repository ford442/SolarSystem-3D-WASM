#ifndef SOLARSYSTEM_SATURN_H
#define SOLARSYSTEM_SATURN_H
#include "../Planet.h"

class Saturn : public Planet {
public:
    explicit Saturn(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    std::string _diffuseLowPath = "resource/textures_low/Saturn_Diffuse_Low.dds";
    std::string _diffuseHighPath = "resource/textures/Saturn_Diffuse.dds";
    std::string _normalLowPath = "resource/textures_low/Saturn_Normal_Low.dds";
    std::string _normalHighPath = "resource/textures/Saturn_Normal.dds";

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 2;
};

#endif //SOLARSYSTEM_SATURN_H
