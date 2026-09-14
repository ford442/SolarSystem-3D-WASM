#ifndef SOLARSYSTEM_CATALOGSATELLITE_H
#define SOLARSYSTEM_CATALOGSATELLITE_H
#include "BodyCatalog.generated.h"
#include "Satellite.h"

/**
 * A moon built from a catalog row — no per-body C++ class.
 *
 * Scene motion is still the circular SatelliteOrbit offset (XZ or XY) using
 * catalog sceneOrbitRadius / orbitalPeriodDays. Keplerian elements are stored on
 * the row for the eclipse follow-up and are not integrated here.
 */
class CatalogSatellite : public Satellite {
public:
    CatalogSatellite(const SatelliteInfo& satelliteInfo, std::shared_ptr<SpaceObject> parent,
                     const BodyCatalog::Entry& entry);

    void AdjustToParent(float timeScale) override;
    void Render() const override;

    const BodyCatalog::Entry& GetEntry() const { return _entry; }

private:
    const BodyCatalog::Entry& _entry;
    bool _hasSpecular;
    float _anomaly;
    float _spinDegrees;

    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap;
    TextureImage2D _specular;
};

#endif //SOLARSYSTEM_CATALOGSATELLITE_H
