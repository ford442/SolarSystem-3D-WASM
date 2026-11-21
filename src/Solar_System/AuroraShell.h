#ifndef SOLARSYSTEM_AURORASHELL_H
#define SOLARSYSTEM_AURORASHELL_H
#include "BodyCatalog.h"
#include "OrbitLayout.h"
#include "OuterShell.h"
#include "Planet.h"
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>
#include <memory>

/**
 * The auroral shell of a body with a render.aurora row (Earth and the giants): a sphere at
 * the top of the emitting layer whose fragments ray-march the oval (aurora.fs,
 * docs/ARCHITECTURE.md §13). Placed like the cloud deck — the parent's position, tilt and
 * flattening — but not spun: the oval hangs on the magnetic pole in the Sun's frame while the
 * planet turns under it. Where the oval sits comes from SpaceWeather each frame.
 */
class AuroraShell : public OuterShell {
public:
    AuroraShell(MeshHolder model, std::shared_ptr<Planet> planet, const BodyCatalog::Entry& parentEntry,
                const BodyCatalog::AuroraRow& row);

    void AdjustToParent() override;

    const BodyCatalog::AuroraRow& Row() const { return _row; }
    const BodyCatalog::Entry& ParentEntry() const { return _parentEntry; }
    OrbitLayout::Body Body() const { return _body; }
    const Planet& GetPlanet() const { return *_planet; }

    /** The emitting layer in equatorial radii from the centre (Earth: 1.016 – 1.047). */
    float BottomRatio() const { return _bottomRatio; }
    float TopRatio() const { return _topRatio; }

    /** Render-frame offset from the parent's centre → its sphere frame (surface = unit sphere). */
    glm::mat3 ToSphere() const;
    /** Whether a render-frame point is inside the shell (the pass then draws its far side). */
    bool Contains(const glm::vec3& point) const;

private:
    std::shared_ptr<Planet> _planet;
    const BodyCatalog::Entry& _parentEntry;
    const BodyCatalog::AuroraRow& _row;
    OrbitLayout::Body _body;
    float _bottomRatio;
    float _topRatio;
};

#endif // SOLARSYSTEM_AURORASHELL_H
