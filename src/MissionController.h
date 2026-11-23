#ifndef SOLARSYSTEM_MISSIONCONTROLLER_H
#define SOLARSYSTEM_MISSIONCONTROLLER_H
#include "FrameContext.h"
#include "IAppModule.h"
#include "SimState.h"
#include "astro/MissionCatalog.h"
#include "core/JobSystem.h"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

class Camera;

/**
 * Probe trajectories (resource/missions/catalog.json, docs/ARCHITECTURE.md §7.2): the catalog,
 * parsed in a job while the core resources load; the focused mission and the camera following
 * it; and each probe's position at the frame's date, which Contribute hands the renderer with
 * the catalog. The probes fly in Sol: another star system has none.
 */
class MissionController final : public IAppModule {
public:
    MissionController(const ModuleContext& app, Camera& camera);

    const char* Name() const override { return "missions"; }
    /** Start parsing the catalog in a job (the JSON is preloaded, so it can be read now). */
    void StartCatalogJob();
    /** Take the parsed catalog (or parse it now), keeping an empty one on failure. */
    void LoadCatalog();
    const MissionCatalog::Catalog& Catalog() const { return _catalog; }
    int Count() const { return static_cast<int>(_catalog.missions.size()); }

    /** Fly to and follow mission `idx` (false: out of range, outside Sol, or in Observe). */
    bool Focus(int idx);
    /** Stop following; tells the page if a mission was focused. */
    void Stop();
    int Focused() const { return _focused; }
    bool Following() const { return _following; }
    /** Before input, once a frame: keep the camera on the followed probe. */
    void UpdateFollow();

    /** Mission `idx`'s probe at the current date, heliocentric scene units (false: off its trajectory). */
    bool SampleHeliocentric(int idx, glm::dvec3& outScene) const;
    /** The same, in the render frame. */
    bool SampleRender(int idx, glm::vec3& outScene) const;
    std::string CatalogJson() const;
    std::string FocusedJson() const;

    /** Every probe's position at this frame's date. */
    void Update(SimState& sim) override;
    /** The catalog (Sol only), the focused mission and the probes. */
    void Contribute(SceneSnapshot& scene) override;

private:
    struct Parsed {
        bool ok = false;
        MissionCatalog::Catalog catalog;
        std::string error;
    };

    const ModuleContext& _app;
    Camera& _camera;
    MissionCatalog::Catalog _catalog;
    Jobs::Future<Parsed> _catalogJob;
    int _focused = -1;
    bool _following = false;
    std::vector<std::optional<glm::vec3>> _probes; // render frame; nullopt off the trajectory
};

#endif // SOLARSYSTEM_MISSIONCONTROLLER_H
