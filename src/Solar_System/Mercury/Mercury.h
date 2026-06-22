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

    std::string _diffuseLowPath = "resource/textures_low/Mercury_Diffuse_Low.dds";
    std::string _diffuseHighPath = "resource/textures/Mercury_Diffuse.dds";
    std::string _normalLowPath = "resource/textures_low/Mercury_Normal_Low.dds";
    std::string _normalHighPath = "resource/textures/Mercury_Normal.dds";
    std::string _specularLowPath = "resource/textures_low/Mercury_Specular_Low.dds";
    std::string _specularHighPath = "resource/textures/Mercury_Specular.dds";

    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 3;
};


#endif //SOLARSYSTEM_MERCURY_H
