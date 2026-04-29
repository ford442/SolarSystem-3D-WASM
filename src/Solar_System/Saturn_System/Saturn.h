#ifndef SOLARSYSTEM_SATURN_H
#define SOLARSYSTEM_SATURN_H
#include "../Planet.h"

class Saturn : public Planet {
public:
    explicit Saturn(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(bool isRunTime) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    std::string _diffuseHighPath = "resource/textures/Saturn_Diffuse.dds";
    std::string _normalHighPath = "resource/textures/Saturn_Normal.dds";

    bool _isHighResLoaded = false;
    bool _isHighResLoading = false;
    float _highResLoadProgress = 0.0f;
    int _highResTexturesLoaded = 0;
    const int _highResTextureCount = 2;
    const float _lodThreshold = 50.0f;
};

#endif //SOLARSYSTEM_SATURN_H
