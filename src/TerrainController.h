#ifndef SOLARSYSTEM_TERRAINCONTROLLER_H
#define SOLARSYSTEM_TERRAINCONTROLLER_H
#include "FrameContext.h"
#include "IAppModule.h"
#include "SimState.h"
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Renderer;
class SpaceObject;
struct QualityTierSettings;
namespace Terrain {
class TerrainSystem;
}

/**
 * Terrain for the bodies flagged render.terrain (docs/ARCHITECTURE.md §11.3): the tile fetch
 * hook, the per-frame decision of which of them draw tiles instead of their sphere, and the
 * [Terrain] log line. The quadtree, cache and LOD are GL-free in src/render/terrain/; Renderer
 * draws the selection Contribute puts in SceneSnapshot::terrain.
 */
class TerrainController final : public IAppModule {
public:
    TerrainController(const ModuleContext& app, Renderer& renderer);
    ~TerrainController() override;

    const char* Name() const override { return "terrain"; }
    /** Budget, pixel error and in-flight cap from a quality tier (Low turns terrain off). */
    void Configure(const QualityTierSettings& settings);
    /** The terrain slot of a body flagged render.terrain that Update has seen, else -1. */
    int SlotOf(const SpaceObject& object) const;
    /** The tile surface's radius under `eye` (model units) while `object` draws tiles, else nullopt. */
    std::optional<double> SurfaceRadius(const SpaceObject& object, const glm::dvec3& eye) const;

    /** Which flagged bodies draw tiles this frame; the GPU copies of evicted tiles dropped. */
    void Update(SimState& sim) override;
    void Contribute(SceneSnapshot& scene) override;

private:
    const ModuleContext& _app;
    Renderer& _renderer;
    std::unique_ptr<Terrain::TerrainSystem> _terrain;
    std::vector<SceneSnapshot::TerrainBody> _selection;
    std::string _logged; // the last [Terrain] stats line, and when it went out
    double _loggedAt = 0.0;
};

#endif // SOLARSYSTEM_TERRAINCONTROLLER_H
