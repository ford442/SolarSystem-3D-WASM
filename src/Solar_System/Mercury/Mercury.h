#ifndef SOLARSYSTEM_MERCURY_H
#define SOLARSYSTEM_MERCURY_H
#include "../Planet.h"

class Mercury : public Planet {
public:
    explicit Mercury(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(bool isRunTime) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    std::string _diffuseHighPath = "resource/textures_low/Mercury_Diffuse_Low.dds";
    std::string _normalHighPath = "resource/textures_low/Mercury_Normal_Low.dds";
    std::string _specularHighPath = "resource/textures_low/Mercury_Specular_Low.dds";

    bool _isHighResLoaded = false;
    bool _isHighResLoading = false;
    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    int _highResTexturesProcessed = 0;
    const int _highResTextureCount = 3;
    const float _lodThreshold = 50.0f;
};


#endif //SOLARSYSTEM_MERCURY_H
