#include "CatalogClouds.h"
#include "../gfx/TextureUnits.h"
#include "BodyShape.h"
#include "TexturePaths.h"
#include "SatelliteOrbit.h"

CatalogClouds::CatalogClouds(CloudsInfo&& cloudsInfo, std::shared_ptr<SpaceObject> parent,
                             const BodyCatalog::Entry& parentEntry)
    : Clouds(std::move(cloudsInfo), std::move(parent)), _parentEntry(parentEntry) {
    const TexturePaths::Paths diffuse =
        TexturePaths::ForTextureId(parentEntry.cloudLayer.diffuse ? parentEntry.cloudLayer.diffuse : "");
    ConfigureDiffuseLOD(diffuse.low, diffuse.mid, diffuse.high,
                        parentEntry.cloudLayer.diffuse ? parentEntry.cloudLayer.diffuse : "Clouds");
}

void CatalogClouds::AdjustToParent() {
    // A function of the date, like the parent's spin, so a date jump doesn't leave the
    // cloud deck where the previous date had it.
    const float spinDegrees = SatelliteOrbit::SpinDegreesAt(_parentEntry.cloudLayer.spinDegPerSimSecond,
                                                            OrbitLayout::GetJulianDate());

    LoadIdentityModelMatrix();
    Translate(_parent->GetPosition());
    Scale(glm::vec3(_scaleFactor));
    Rotate(_parentEntry.axialTiltDegrees, glm::vec3(0.0f, 0.0f, 1.0f));
    if (_parentEntry.artTiltXDegrees != 0.0f) {
        Rotate(_parentEntry.artTiltXDegrees, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (BodyShape::IsOblate(_parentEntry)) {
        Scale(BodyShape::PolarScale(_parentEntry));
    }
    Rotate(spinDegrees, glm::vec3(0.0f, 1.0f, 0.0f));
}

void CatalogClouds::Render() const {
    GetShader().SetFloat("ambientFactor", _parentEntry.cloudLayer.ambientFactor);
    glBindTextureUnit(Unit(TextureUnit::Diffuse), _diffuse.GetTexture());
    glBindTextureUnit(Unit(TextureUnit::Normal), _normal.GetTexture());

    SpaceObject::Render();
}
