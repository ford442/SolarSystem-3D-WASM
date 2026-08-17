#ifndef SOLARSYSTEM_SATURN_H
#define SOLARSYSTEM_SATURN_H
#include "../Planet.h"
#include "../../Auxiliary_Modules/TextureLODController.h"

class Saturn : public Planet {
public:
    explicit Saturn(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar);
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

#endif //Saturn
