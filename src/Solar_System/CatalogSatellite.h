#ifndef SOLARSYSTEM_CATALOGSATELLITE_H
#define SOLARSYSTEM_CATALOGSATELLITE_H
#include "BodyCatalog.generated.h"
#include "Satellite.h"

/**
 * A moon built from a catalog row — no per-body C++ class.
 *
 * Scene motion prefers the active ephemeris backend's Keplerian solution for the row
 * (inclined, eccentric, epoch-driven — Moon, the Galileans, Titan, Triton today). Rows the
 * backend has no solution for fall back to the circular SatelliteOrbit offset (XZ or XY)
 * using catalog sceneOrbitRadius / orbitalPeriodDays.
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
    bool _hasSpecular;
    float _anomaly;
    float _spinDegrees;
    bool _ephemerisPlaced = false;

    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;
    TextureImage2D _specular;
};

#endif //SOLARSYSTEM_CATALOGSATELLITE_H
