#ifndef SOLARSYSTEM_PLUTO_H
#define SOLARSYSTEM_PLUTO_H
#include "../Planet.h"
#include "../../Auxiliary_Modules/TextureLODController.h"

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

    TextureLODController _diffuseLOD;
    TextureLODController _normalLOD;
    TextureLODController _specularLOD;
};

#endif //Pluto
