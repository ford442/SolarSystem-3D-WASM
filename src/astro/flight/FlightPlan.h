#ifndef SOLARSYSTEM_ASTRO_FLIGHT_FLIGHTPLAN_H
#define SOLARSYSTEM_ASTRO_FLIGHT_FLIGHTPLAN_H
#include "astro/flight/Propagator.h"
#include "astro/flight/StateVector.h"
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace MissionCatalog {
struct Mission;
}

/**
 * A flight as patched conics (docs/ARCHITECTURE.md §14): a sequence of encounters (body, date),
 * one heliocentric Lambert arc per leg, and at each intermediate body the flyby that joins two
 * arcs — its v∞ in and out, the periapsis an unpowered pass needs for that turn, and the Δv a
 * powered one would spend at periapsis where the two v∞ differ. Planetocentric hyperbolas are
 * reported, not flown: on the heliocentric scale the legs meet at the planet's centre.
 */
namespace Flight {

struct Encounter {
    int body = 0;
    double julianDate = 0.0;
    /** The historic periapsis altitude (km), for comparison in the report; < 0 when not given. */
    double historicAltitudeKm = -1.0;
};

struct PlanOptions {
    /** Revolutions each leg may make about the Sun (multi-rev Lambert); the cheapest is kept. */
    int maxRevolutions = 2;
    double parkingAltitudeKm = 200.0; // the departure orbit the launch Δv is quoted from
    double captureAltitudeKm = 500.0; // the circular orbit the arrival Δv is quoted into
};

struct Leg {
    int fromBody = 0;
    int toBody = 0;
    double jd0 = 0.0;
    double jd1 = 0.0;
    StateVector start; // heliocentric, at jd0 (the from-body's centre)
    glm::dvec3 arriveVelocity{0.0};
    int revolutions = 0;
};

struct EncounterReport {
    enum class Kind { Launch, Flyby, Arrival };
    Kind kind = Kind::Launch;
    int body = 0;
    double julianDate = 0.0;
    glm::dvec3 vInfIn{0.0};  // planet-relative, km/s (zero at launch)
    glm::dvec3 vInfOut{0.0}; // planet-relative, km/s (zero at arrival)
    Flyby flyby;             // flybys only
    double deltaVKmS = 0.0;  // launch from parking orbit, powered flyby, or capture
    double historicAltitudeKm = -1.0;
};

struct Plan {
    std::string id;
    std::string name;
    std::vector<Leg> legs;
    std::vector<EncounterReport> encounters;
    double c3 = 0.0;
    double totalDeltaVKmS = 0.0;
    bool ok = false;
    std::string error;

    double StartJd() const { return legs.empty() ? 0.0 : legs.front().jd0; }
    double EndJd() const { return legs.empty() ? 0.0 : legs.back().jd1; }
    /** Leg flying at `jd` (clamped to the first/last). -1 for an empty plan. */
    int LegAt(double julianDate) const;
    /**
     * The craft's heliocentric state at `jd`: on its leg's conic during the flight, riding with
     * the departure body before launch and with the arrival body after arrival.
     */
    StateVector StateAt(double julianDate) const;
};

/** Patched-conic plan through `sequence` (at least two encounters, dates increasing). */
Plan PlanSequence(const std::vector<Encounter>& sequence, const PlanOptions& options = {});

/** A direct transfer: `from` at `departureJd`, `to` after `tofDays`. */
Plan PlanTransfer(int from, int to, double departureJd, double tofDays, const PlanOptions& options = {});

/** Real missions rebuilt from their published encounter dates. */
struct Preset {
    const char* id;
    const char* name;
    const char* sequence; // e.g. "EJSUN"
    std::vector<Encounter> encounters;
};
const std::vector<Preset>& Presets();
const Preset* FindPreset(const std::string& id);
/** The preset as a plan (id and name filled in). */
Plan PlanPreset(const Preset& preset, const PlanOptions& options = {});

/** How far the plan is from a stored mission path, over the dates both cover. */
struct Residuals {
    int samples = 0;
    double rmsAu = 0.0;
    double maxAu = 0.0;
    double maxJd = 0.0;
};
/** Compare at `samples` dates spread over the overlap of the plan and the mission's samples. */
Residuals CompareWithMission(const Plan& plan, const MissionCatalog::Mission& mission, int samples = 400);

/**
 * The n-body sanity check of one leg: leave the from-body's sphere of influence on the
 * planetocentric hyperbola (asymptote along v∞, speed sqrt(v∞² + 2μ/r_SOI)), then integrate
 * against the full ephemeris (DOPRI5(4)) to a few days past arrival and measure how close it
 * passes the to-body. That miss is what trajectory-correction manoeuvres would have to null.
 */
struct NBodyCheck {
    bool ok = false;
    double soiExitJd = 0.0;
    double closestApproachKm = 0.0;
    double closestApproachJd = 0.0;
    double missInSoiRadii = 0.0; // closest approach over the to-body's SOI radius
    int steps = 0;
};
NBodyCheck CheckLegNBody(const Plan& plan, int leg);

/** The plan's report for the page: legs, encounters, Δv budget. */
std::string PlanJson(const Plan& plan);

} // namespace Flight

#endif // SOLARSYSTEM_ASTRO_FLIGHT_FLIGHTPLAN_H
