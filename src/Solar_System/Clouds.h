#ifndef SOLARSYSTEM_CLOUDS_H
#define SOLARSYSTEM_CLOUDS_H
#include "OuterShell.h"
#include "../Auxiliary_Modules/TextureImage2D.h"
#include "../Auxiliary_Modules/TextureLODController.h"

struct CloudsInfo {
    MeshHolder cloudsModel;
    const Shader* cloudsShader;
    float scaleFactor;
    TextureImage2D diffuseMap, normalMap;

    explicit CloudsInfo(MeshHolder model, const Shader& shader, float earthScaleFactor, const TextureImage2D& diffuse, const TextureImage2D& normal)
        : cloudsModel(std::move(model)), cloudsShader(&shader), scaleFactor(earthScaleFactor), diffuseMap(diffuse), normalMap(normal) {}
};

class Clouds : public OuterShell {
public:
    explicit Clouds(const CloudsInfo& cloudsInfo, std::shared_ptr<SpaceObject> parent);
    void AdjustToParent(float timeScale) = 0;
    void LoadHighResIfClose(const glm::vec3& cameraPosition);

protected:
    TextureImage2D _diffuse, _normal;
    void ConfigureDiffuseLOD(const std::string& lowPath, const std::string& midPath, const std::string& highPath, const std::string& label);

private:
    TextureLODController _diffuseLOD;
};

#endif //SOLARSYSTEM_CLOUDS_H
