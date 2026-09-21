#include "SaturnRing.h"
#include "../TexturePaths.h"
#include "../BodyCatalog.generated.h"
#include "../OrbitLayout.h"

SaturnRing::SaturnRing(const PlanetaryRingInfo& planetaryRingInfo, std::shared_ptr<Planet> parent) : PlanetaryRing(planetaryRingInfo, std::move(parent)) {
    ConfigureDiffuseLOD(TexturePaths::Resolve("resource/textures_low/Saturn_Rings_Low.dds"),
                        TexturePaths::Resolve("resource/textures_mid/Saturn_Rings_Mid.dds"),
                        TexturePaths::Resolve("resource/textures/Saturn_Rings.dds"), "SaturnRing");
}

void SaturnRing::AdjustToParent() {
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(static_cast<int>(OrbitLayout::Body::Saturn));
    const float tiltZ = entry ? entry->axialTiltDegrees : -26.7f;
    const float tiltX = entry ? entry->artTiltXDegrees : -15.0f;

    LoadIdentityModelMatrix();
    Translate(_parentPlanet->GetPosition());
    Rotate(tiltZ, glm::vec3(0, 0, 1));
    Rotate(tiltX, glm::vec3(1, 0, 0));
    UpdateRingNormal();
    UpdateModelMatrix();
}

void SaturnRing::Render() const {
    GetShader().SetVec3("planetPos", _parentPlanet->GetPosition());
    GetShader().SetFloat("planetRadius", _parentPlanet->GetRadius());
    GetShader().SetInt("ringDiffuse", 0);
    glBindTextureUnit(0, _ringTexture.GetTexture());
    SpaceObject::Render();
}
