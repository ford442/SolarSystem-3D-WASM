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

    std::string _diffuseLowPath = "resource/textures_low/Pluto_Diffuse_Low.dds";
    std::string _diffuseHighPath = "resource/textures/Pluto_Diffuse.dds";
    std::string _normalLowPath = "resource/textures_low/Pluto_Normal_Low.dds";
    std::string _normalHighPath = "resource/textures/Pluto_Normal.dds";
    std::string _specularLowPath = "resource/textures_low/Pluto_Specular_Low.dds";
    std::string _specularHighPath = "resource/textures/Pluto_Specular.dds";

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 3;
};

#endif //SOLARSYSTEM_PLUTO_H
