#ifndef SOLARSYSTEM_CATALOGSATELLITE_H
#define SOLARSYSTEM_CATALOGSATELLITE_H
#include "BodyCatalog.generated.h"
#include "CatalogMaterial.h"
#include "Satellite.h"

/**
 * A moon built from a catalog row — no per-body C++ class.
 *
 * Scene motion prefers the active ephemeris backend's Keplerian solution for the row
 * (inclined, eccentric, epoch-driven — Moon, the Galileans, Titan, Triton today). Rows the
 * backend has no solution for fall back to the circular SatelliteOrbit offset (XZ or XY)
 * using catalog sceneOrbitRadius / orbitalPeriodDays. Either way the pose (and spin) is a
 * function of OrbitLayout::GetJulianDate(), never of how many frames have run.
 */
class CatalogSatellite : public Satellite {
public:
    CatalogSatellite(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent,
                     const BodyCatalog::Entry& entry);

    void AdjustToParent(float timeScale) override;
    void Render() const override;

    bool IsEphemerisPlaced() const override { return _ephemerisPlaced; }
    float OrbitSceneUnitsPerKm() const override;

    const BodyCatalog::Entry& GetEntry() const { return _entry; }

private:
    const BodyCatalog::Entry& _entry;
    CatalogMaterial::Material _material;
    bool _ephemerisPlaced = false;

    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;
    TextureImage2D _specular;
};

#endif //SOLARSYSTEM_CATALOGSATELLITE_H
