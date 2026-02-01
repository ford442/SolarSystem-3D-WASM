#ifndef SOLARSYSTEM_PLUTO_H
#define SOLARSYSTEM_PLUTO_H
#include "../Planet.h"

class Pluto : public Planet {
public:
    explicit Pluto(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(bool isRunTime) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    std::string _diffuseHighPath = "resource/textures/Pluto_Diffuse.dds";
    std::string _normalHighPath = "resource/textures/Pluto_Normal.dds";
    std::string _specularHighPath = "resource/textures/Pluto_Specular.dds";

    bool _isHighResLoaded = false;
    const float _lodThreshold = 50.0f;
};

#endif //SOLARSYSTEM_PLUTO_H
