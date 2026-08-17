#ifndef SOLARSYSTEM_URANUS_H
#define SOLARSYSTEM_URANUS_H
#include "../Planet.h"
#include "../../Auxiliary_Modules/TextureLODController.h"

class Uranus : public Planet {
public:
    explicit Uranus(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

private:
    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;

    TextureLODController _diffuseLOD;
    TextureLODController _normalLOD;
};

#endif //Uranus
