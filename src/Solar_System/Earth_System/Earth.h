#ifndef SOLARSYSTEM_EARTH_H
#define SOLARSYSTEM_EARTH_H
#include "../Planet.h"

class Earth : public Planet {
public:
    explicit Earth(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(bool isRunTime) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;
    
    // LOD texture paths
    // Note: Low-res paths are used during initialization in Application::InitEarthSystem()
    // High-res paths are used at runtime when camera zooms close (< 50 units)
    std::string _diffuseLowPath = "resource/textures_low/Earth_Day_Diffuse_Low.dds";
    std::string _diffuseHighPath = "resource/textures/Earth_Day_Diffuse.dds";
    std::string _normalLowPath = "resource/textures_low/Earth_Normal_Low.dds";
    std::string _normalHighPath = "resource/textures/Earth_Normal.dds";
    std::string _specularLowPath = "resource/textures_low/Earth_Specular_Low.dds";
    std::string _specularHighPath = "resource/textures/Earth_Specular.dds";
    
    bool _isHighResLoaded = false;
    const float _lodThreshold = 50.0f;
};

#endif //SOLARSYSTEM_EARTH_H
