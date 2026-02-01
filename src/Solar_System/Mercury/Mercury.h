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

    std::string _diffuseHighPath = "resource/textures/Mercury_Diffuse.dds";
    std::string _normalHighPath = "resource/textures/Mercury_Normal.dds";
    std::string _specularHighPath = "resource/textures/Mercury_Specular.dds";

    bool _isHighResLoaded = false;
    const float _lodThreshold = 50.0f;
};


#endif //SOLARSYSTEM_MERCURY_H
