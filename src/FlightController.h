#ifndef SOLARSYSTEM_FLIGHTCONTROLLER_H
#define SOLARSYSTEM_FLIGHTCONTROLLER_H
#include "FrameContext.h"
#include "IAppModule.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include "astro/flight/FlightPlan.h"
#include "astro/flight/Porkchop.h"
#include "astro/flight/Spacecraft.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class CameraController;
class MissionController;

/**
 * The flight planner, Application's side (docs/ARCHITECTURE.md §14): the last porkchop grid,
 * the planned flight and the Spacecraft flying it, the chase camera, and the craft's path and
 * position for the frame, which Contribute hands the renderer (the mission-paths pass draws
 * them). Everything is computed by the GL-free src/astro/flight/; this module only owns state
 * and moves it into the scene. Flights are Sol's: another star system has no ephemeris to plan
 * against, and a system jump clears the flight.
 */
class FlightController final : public IAppModule {
public:
    FlightController(const ModuleContext& app, const MissionController& missions);

    const char* Name() const override { return "flight"; }

    /** The porkchop grid as JSON (Flight::Porkchop::ToJson); also kept for PorkchopJson(). */
    std::string ComputePorkchop(const Flight::Porkchop::Request& request);
    const std::string& PorkchopJson() const { return _porkchopJson; }

    /** Plan a direct transfer and put the craft on it; false (and the error in PlanJson) on failure. */
    bool PlanTransfer(int from, int to, double departureJd, double tofDays);
    /** Plan a real mission from its encounter dates ("voyager2", "cassini", "new-horizons", …). */
    bool PlanPreset(const std::string& id);
    /** Remove the craft (and stop chasing it). */
    void Clear();
    bool HasFlight() const { return _craft.Valid(); }
    const Flight::Spacecraft& Craft() const { return _craft; }

    /** The current plan's report (legs, encounters, Δv), or {"ok":false,…}. */
    std::string PlanJson() const;
    /** The craft at the current date: phase, leg, speed, distance to its next body. */
    std::string StateJson() const;
    /** The presets as [{id,name,sequence,startJd,endJd}]. */
    static std::string PresetsJson();

    void SetChase(bool chase);
    void StopChase() { _chase = false; }
    bool Chasing() const { return _chase && HasFlight(); }
    /** Before input, once a frame: put the camera behind the craft while chasing. */
    void UpdateChase(CameraController& camera) const;

    void Update(SimState& sim) override;
    void Contribute(SceneSnapshot& scene) override;

private:
    void Adopt(Flight::Plan plan);
    void RebuildPath();
    /** The drawn radius of `body` in scene units (its object if loaded, else 1). */
    double BodyRadius(int body) const;

    const ModuleContext& _app;
    const MissionController& _missions;
    Flight::Spacecraft _craft;
    Flight::Plan _lastPlan; // also a failed one, for its error
    std::string _porkchopJson = "{}";
    bool _chase = false;

    // The path in heliocentric scene units for the scale mode it was built in.
    std::vector<glm::dvec3> _path;
    std::vector<double> _pathDates;
    OrbitLayout::ScaleMode _pathScale = OrbitLayout::ScaleMode::Compressed;
    std::uint32_t _version = 0;
    FlightFrame _frame;
};

#endif // SOLARSYSTEM_FLIGHTCONTROLLER_H
