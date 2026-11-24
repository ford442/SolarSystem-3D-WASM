// The flight planner's module: porkchop requests, the planned flight and its craft, the chase
// camera, and the craft's path for the mission-paths pass (render/passes/OverlayPasses.cpp).
#include "FlightController.h"
#include "ApplicationTypes.h"
#include "CameraController.h"
#include "FrameContext.h"
#include "IAppModule.h"
#include "MissionController.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/StarSystemCatalog.h"
#include "astro/MissionCatalog.h"
#include "astro/flight/FlightConstants.h"
#include "astro/flight/FlightPlan.h"
#include "astro/flight/Porkchop.h"
#include "astro/flight/Spacecraft.h"
#include "astro/flight/StateVector.h"
#include "core/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <ratio>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr int kPathSamplesPerLeg = 256;
constexpr double kChaseMaxDistance = 40.0; // scene units behind the craft in cruise
constexpr double kChaseBodyRadii = 8.0;    // never closer than this many radii of the body it nears

const char* PhaseName(Flight::Spacecraft::Phase phase) {
    switch (phase) {
        case Flight::Spacecraft::Phase::BeforeLaunch: return "before-launch";
        case Flight::Spacecraft::Phase::Cruise: return "cruise";
        case Flight::Spacecraft::Phase::Arrived: return "arrived";
    }
    return "?";
}

} // namespace

FlightController::FlightController(const ModuleContext& app, const MissionController& missions)
    : _app(app), _missions(missions) {}

std::string FlightController::ComputePorkchop(const Flight::Porkchop::Request& request) {
    const auto started = std::chrono::steady_clock::now();
    const Flight::Porkchop::Grid grid = Flight::Porkchop::Compute(request);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    _porkchopJson = Flight::Porkchop::ToJson(grid);
    LOG_INFO("Flight", "Porkchop %d→%d: %d×%d cells in %d tile(s), %.1f ms; best C3 %.2f km²/s², v∞ %.2f km/s",
             grid.request.departureBody, grid.request.arrivalBody, grid.request.departureSteps, grid.request.tofSteps,
             grid.tiles, ms, grid.bestC3, grid.bestVInfArrive);
    return _porkchopJson;
}

bool FlightController::PlanTransfer(int from, int to, double departureJd, double tofDays) {
    if (!StarSystemCatalog::IsSol()) {
        LOG_WARN("Flight", "Flights are planned in Sol only");
        return false;
    }
    Flight::Plan plan = Flight::PlanTransfer(from, to, departureJd, tofDays);
    if (plan.ok) {
        const Flight::NBodyCheck check = Flight::CheckLegNBody(plan, 0);
        if (check.ok) {
            LOG_INFO("Flight", "%s n-body check: passes %.0f km from the target (%.2f SOI radii), %d steps",
                     plan.name.c_str(), check.closestApproachKm, check.missInSoiRadii, check.steps);
        }
    }
    Adopt(std::move(plan));
    return HasFlight();
}

bool FlightController::PlanPreset(const std::string& id) {
    if (!StarSystemCatalog::IsSol()) {
        return false;
    }
    const Flight::Preset* preset = Flight::FindPreset(id);
    if (!preset) {
        LOG_WARN("Flight", "No mission preset '%s'", id.c_str());
        return false;
    }
    Flight::Plan plan = Flight::PlanPreset(*preset);
    // A preset that has a stored (baked) path too is compared with it.
    const MissionCatalog::Catalog& catalog = _missions.Catalog();
    const int stored = MissionCatalog::IndexById(catalog, id);
    if (plan.ok && stored >= 0) {
        const Flight::Residuals r =
            Flight::CompareWithMission(plan, catalog.missions[static_cast<std::size_t>(stored)]);
        LOG_INFO("Flight", "%s computed vs stored path: rms %.3f AU, max %.3f AU at JD %.1f (%d samples)", id.c_str(),
                 r.rmsAu, r.maxAu, r.maxJd, r.samples);
    }
    for (const Flight::EncounterReport& e : plan.encounters) {
        if (e.kind != Flight::EncounterReport::Kind::Flyby) {
            continue;
        }
        LOG_INFO("Flight", "%s %s: v∞ %.2f→%.2f km/s, turn %.1f°, periapsis alt %.0f km (historic %.0f), Δv %.3f km/s",
                 id.c_str(), Flight::FindBody(e.body)->name, e.flyby.vInfInKmS, e.flyby.vInfOutKmS,
                 e.flyby.turnAngleRad * 180.0 / Flight::kPi, e.flyby.altitudeKm, e.historicAltitudeKm,
                 e.flyby.poweredDeltaVKmS);
    }
    Adopt(std::move(plan));
    return HasFlight();
}

void FlightController::Adopt(Flight::Plan plan) {
    _lastPlan = plan;
    if (!plan.ok) {
        LOG_WARN("Flight", "No flight: %s", plan.error.c_str());
        return;
    }
    LOG_INFO("Flight", "%s: %zu leg(s), C3 %.2f km²/s², Δv %.2f km/s", plan.name.c_str(), plan.legs.size(), plan.c3,
             plan.totalDeltaVKmS);
    Flight::SpacecraftRow row;
    row.id = plan.id.empty() ? "craft" : plan.id;
    row.name = plan.name;
    _craft = Flight::Spacecraft(std::move(row), std::move(plan));
    RebuildPath();
}

void FlightController::Clear() {
    _craft = {};
    _lastPlan = {};
    _chase = false;
    _path.clear();
    _pathDates.clear();
    ++_version;
}

void FlightController::RebuildPath() {
    _pathDates = _craft.PathDates(kPathSamplesPerLeg);
    _path = _craft.PathScene(kPathSamplesPerLeg);
    _pathScale = OrbitLayout::GetScaleMode();
    ++_version;
}

std::string FlightController::PlanJson() const {
    return Flight::PlanJson(_lastPlan);
}

std::string FlightController::StateJson() const {
    if (!HasFlight()) {
        return R"({"valid":false})";
    }
    const double jd = OrbitLayout::GetJulianDate();
    const Flight::Plan& plan = _craft.GetPlan();
    const int legIndex = plan.LegAt(jd);
    const Flight::Leg& leg = plan.legs[static_cast<std::size_t>(legIndex)];
    const Flight::StateVector state = _craft.StateAt(jd);
    const double distanceKm = glm::length(state.r - Flight::PlanetPosition(leg.toBody, jd));
    const double progress = std::clamp((jd - plan.StartJd()) / (plan.EndJd() - plan.StartJd()), 0.0, 1.0);
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  R"({"valid":true,"id":"%s","phase":"%s","leg":%d,"toBody":%d,"jd":%.5f,"progress":%.5f,)"
                  R"("speedKmS":%.4f,"sunDistanceAu":%.5f,"targetDistanceKm":%.1f,"chasing":%s})",
                  _craft.Row().id.c_str(), PhaseName(_craft.PhaseAt(jd)), legIndex, leg.toBody, jd, progress,
                  glm::length(state.v), glm::length(state.r) / Flight::kAuKm, distanceKm, Chasing() ? "true" : "false");
    return buffer;
}

std::string FlightController::PresetsJson() {
    std::ostringstream out;
    out << '[';
    bool first = true;
    for (const Flight::Preset& preset : Flight::Presets()) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
                      R"(%s{"id":"%s","name":"%s","sequence":"%s","startJd":%.4f,"endJd":%.4f})", first ? "" : ",",
                      preset.id, preset.name, preset.sequence, preset.encounters.front().julianDate,
                      preset.encounters.back().julianDate);
        out << buffer;
        first = false;
    }
    out << ']';
    return out.str();
}

void FlightController::SetChase(bool chase) {
    _chase = chase && HasFlight() && !_app.observe.active;
}

double FlightController::BodyRadius(int body) const {
    for (const RenderableSceneComponent& component : _app.components) {
        if (!component.planet) {
            continue;
        }
        const std::wstring& wide = component.planet->GetEngName();
        if (static_cast<int>(OrbitLayout::BodyFromName(std::string(wide.begin(), wide.end()))) == body) {
            return component.planet->GetBoundingSphere().radius;
        }
    }
    return 1.0;
}

void FlightController::UpdateChase(CameraController& camera) const {
    if (!Chasing() || _app.observe.active) {
        camera.StopChase();
        return;
    }
    const double jd = OrbitLayout::GetJulianDate();
    const glm::dvec3 craft = _craft.WorldPositionAt(jd);
    const Flight::Plan& plan = _craft.GetPlan();
    // Close in on the body ahead (or the one just left) so the arrival fills the frame, but
    // never inside a few of its radii.
    const Flight::Leg& leg = plan.legs[static_cast<std::size_t>(plan.LegAt(jd))];
    double distance = kChaseMaxDistance;
    for (const int body : {leg.fromBody, leg.toBody}) {
        const double away =
            glm::length(craft - OrbitLayout::GetHeliocentricScenePosition(static_cast<OrbitLayout::Body>(body)));
        const double floor = kChaseBodyRadii * BodyRadius(body);
        distance = std::min(distance, std::max(floor, 0.5 * away));
    }
    camera.Chase({craft, _craft.SceneForwardAt(jd), distance});
}

void FlightController::Update(SimState& /*sim*/) {
    _frame = {};
    if (!HasFlight() || !StarSystemCatalog::IsSol()) {
        return;
    }
    if (OrbitLayout::GetScaleMode() != _pathScale) {
        RebuildPath(); // the path is in scene units of the mode it was built in
    }
    const double jd = OrbitLayout::GetJulianDate();
    _frame.version = _version;
    _frame.path = &_path;
    _frame.flownSamples =
        static_cast<std::size_t>(std::upper_bound(_pathDates.begin(), _pathDates.end(), jd) - _pathDates.begin());
    _frame.craftWorld = _craft.WorldPositionAt(jd);
    _frame.craftRender = OrbitLayout::ToRenderFrame(_frame.craftWorld);
    _frame.craftVisible = true;
    _frame.color = _craft.Row().color;
}

void FlightController::Contribute(SceneSnapshot& scene) {
    scene.flight = _frame.path ? &_frame : nullptr;
}
