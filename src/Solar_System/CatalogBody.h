#ifndef SOLARSYSTEM_CATALOGBODY_H
#define SOLARSYSTEM_CATALOGBODY_H
#include "BodyCatalog.generated.h"
#include "OrbitLayout.h"
#include "Planet.h"
#include "../Auxiliary_Modules/TextureLODController.h"

/**
 * A planet built entirely from a catalog row — no per-body C++ class.
 *
 * Mercury–Pluto, Ceres, and Vesta all go through this class. Unique shader layouts
 * (Earth night+clouds, Uranus/Neptune cloud maps) are selected from BodyCatalog::Entry
 * flags and extra LOD ids, not from subclasses. Axial tilt is the catalog SSOT:
 * Rotate(axialTiltDegrees, Z) then optional artTiltXDegrees (Saturn).
 *
 * Atmospheres, ring geometry, and cloud *shells* are attached by the factory
 * (SystemVisuals + CatalogClouds), not here. The Sun stays a dedicated class.
 */
class CatalogBody : public Planet {
public:
    CatalogBody(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar,
                const BodyCatalog::Entry& entry);

    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

    OrbitLayout::Body GetBody() const { return _body; }
    const BodyCatalog::Entry& GetEntry() const { return _entry; }

private:
    const BodyCatalog::Entry& _entry;
    OrbitLayout::Body _body;
    bool _hasSpecular;
    bool _hasNight;
    bool _hasClouds;

    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    TextureLODController _diffuseLOD;
    TextureLODController _normalLOD;
    TextureLODController _specularLOD;
};

#endif //SOLARSYSTEM_CATALOGBODY_H
