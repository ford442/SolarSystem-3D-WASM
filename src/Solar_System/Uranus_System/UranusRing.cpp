#include "UranusRing.h"
#include "../BodyCatalog.generated.h"
#include "../OrbitLayout.h"

UranusRing::UranusRing(const PlanetaryRingInfo& planetaryRingInfo, std::shared_ptr<Planet> parent) : PlanetaryRing(planetaryRingInfo, std::move(parent))
{
    ConfigureDiffuseLOD("resource/textures_low/Uranus_Rings_Low.dds",
                        "resource/textures_mid/Uranus_Rings_Mid.dds",
                        "resource/textures/Uranus_Rings.dds", "UranusRing");
}

void UranusRing::AdjustToParent() {
    const BodyCatalog::Entry* entry = BodyCatalog::FindByIndex(static_cast<int>(OrbitLayout::Body::Uranus));
    const float tiltZ = entry ? entry->axialTiltDegrees : -97.8f;
    const float tiltX = entry ? entry->artTiltXDegrees : 0.0f;

    LoadIdentityModelMatrix();
    Translate(_parentPlanet->GetPosition());
    Rotate(tiltZ, glm::vec3(0, 0, 1));
    if (tiltX != 0.0f) {
        Rotate(tiltX, glm::vec3(1, 0, 0));
    }
    UpdateRingNormal();
    UpdateModelMatrix();
}

void UranusRing::Render() const {
    GetShader().SetVec3("planetPos", _parentPlanet->GetPosition());
    GetShader().SetFloat("planetRadius", _parentPlanet->GetRadius());
    GetShader().SetInt("ringDiffuse", 0);
    glBindTextureUnit(0, _ringTexture.GetTexture());
    SpaceObject::Render();
}
