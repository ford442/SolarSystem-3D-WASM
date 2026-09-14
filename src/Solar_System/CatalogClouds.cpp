#include "CatalogClouds.h"
#include "TexturePaths.h"
#include "../SimState.h"

CatalogClouds::CatalogClouds(const CloudsInfo& cloudsInfo, std::shared_ptr<SpaceObject> parent,
                             const BodyCatalog::Entry& parentEntry)
    : Clouds(cloudsInfo, std::move(parent)), _parentEntry(parentEntry) {
    const TexturePaths::Paths diffuse =
        TexturePaths::ForTextureId(parentEntry.cloudLayer.diffuse ? parentEntry.cloudLayer.diffuse : "");
    ConfigureDiffuseLOD(diffuse.low, diffuse.mid, diffuse.high,
                        parentEntry.cloudLayer.diffuse ? parentEntry.cloudLayer.diffuse : "Clouds");
}

void CatalogClouds::AdjustToParent(float /*timeScale*/) {
    if (gSimState->simDeltaSeconds > 0.0f) {
        _spinDegrees += _parentEntry.cloudLayer.spinDegPerSimSecond * gSimState->simDeltaSeconds;
    }

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(_scaleFactor));
    Rotate(_parentEntry.axialTiltDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    if (_parentEntry.artTiltXDegrees != 0.0f) {
        Rotate(_parentEntry.artTiltXDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    Rotate(_spinDegrees, glm::vec3(0.0f, 1.0f, 0.0f));
    UpdateModelMatrix();
}

void CatalogClouds::Render() const {
    GetShader().SetFloat("ambientFactor", _parentEntry.cloudLayer.ambientFactor);
    GetShader().SetInt("mainDiffuseTexture", 0);
    GetShader().SetInt("cloudsNormalMap", 1);

    glBindTextureUnit(0, _diffuse.GetTexture());
    glBindTextureUnit(1, _normal.GetTexture());

    SpaceObject::Render();
}
