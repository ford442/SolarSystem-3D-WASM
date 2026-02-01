#ifndef SOLARSYSTEM_MARS_H
#define SOLARSYSTEM_MARS_H
#include "../Planet.h"

class Mars : public Planet {
public:
    explicit Mars(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(bool isRunTime) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    std::string _diffuseHighPath = "resource/textures/Mars_Diffuse.dds";
    std::string _normalHighPath = "resource/textures/Mars_Normal.dds";

    bool _isHighResLoaded = false;
    const float _lodThreshold = 50.0f;
};

#endif //SOLARSYSTEM_MARS_H
