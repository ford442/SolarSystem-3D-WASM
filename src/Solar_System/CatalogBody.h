#ifndef SOLARSYSTEM_CATALOGBODY_H
#define SOLARSYSTEM_CATALOGBODY_H
#include "BodyCatalog.generated.h"
#include "OrbitLayout.h"
#include "Planet.h"
#include "../Auxiliary_Modules/TextureLODController.h"

/**
 * A planet built entirely from a catalog row — no per-body C++ class.
 *
 * Mercury/Venus/… each have a hand-written class because they carry unique art decisions
 * (fixed tilts, atmospheres, clouds, rings). Bodies whose only distinguishing features fit
 * in a BodyCatalog::Entry — a diffuse/normal/optional-specular set, a few shader flags, an
 * orbit row — go through this class instead, so adding one is a catalog edit plus a factory
 * line. Ceres and Vesta are the first two.
 *
 * Orbit and spin come from OrbitLayout (ephemeris-driven), exactly as for the hand-written
 * planets. Axial tilt is deliberately not applied: catalog tilts do not match the hand-tuned
 * art rotations the existing classes use, and reconciling that is a separate pass (see
 * BodyCatalog.generated.h).
 */
class CatalogBody : public Planet {
public:
    CatalogBody(const PlanetInfo& planetInfo, std::shared_ptr<Star> parentStar,
                const BodyCatalog::Entry& entry);

    void AdjustToParent(float timeScale) override;
    void Render() const override;
    void LoadHighResIfClose(const glm::vec3& cameraPos) override;
    void UnloadHighResIfFar(const glm::vec3& cameraPos) override;

    /** Focus-index body this row describes (OrbitLayout::Body::Ceres for "ceres", …). */
    OrbitLayout::Body GetBody() const { return _body; }

private:
    const BodyCatalog::Entry& _entry;
    OrbitLayout::Body _body;
    bool _hasSpecular;

    std::vector<TextureImage2D> _diffuses;
    TextureImage2D _normalMap, _specular;

    TextureLODController _diffuseLOD;
    TextureLODController _normalLOD;
    TextureLODController _specularLOD;
};

#endif //SOLARSYSTEM_CATALOGBODY_H
