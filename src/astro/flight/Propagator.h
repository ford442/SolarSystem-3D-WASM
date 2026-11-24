#ifndef SOLARSYSTEM_ASTRO_FLIGHT_PROPAGATOR_H
#define SOLARSYSTEM_ASTRO_FLIGHT_PROPAGATOR_H
#include "astro/flight/StateVector.h"
#include <glm/vec3.hpp>

/**
 * Orbit propagation for the flight planner: exact two-body arcs (universal variables) for the
 * patched-conic legs, and an adaptive Dormand–Prince 5(4) integrator against the ephemeris
 * (Sun + Mercury … Neptune + Pluto + the Moon) to check a leg against n-body gravity.
 * Hyperbolic flyby geometry (turn angle, periapsis, B-plane) lives here too.
 */
namespace Flight {

/** `state` about `mu` advanced by `dtSeconds` (either sign) on its conic. Universal variables, any e. */
StateVector KeplerPropagate(const StateVector& state, double mu, double dtSeconds);

// --- n-body ------------------------------------------------------------------------------

struct NBodyOptions {
    double relativeTolerance = 1e-10;
    double absoluteToleranceKm = 1.0;
    double maxStepSeconds = 2.0 * 86400.0;
    double minStepSeconds = 1.0;
    bool includeMoon = true;
    /** Skip this body's pull (the one the craft is leaving): -1 keeps them all. */
    int excludeBody = -1;
    int maxSteps = 2000000;
};

struct NBodyResult {
    StateVector state; // heliocentric at the end time
    int steps = 0;
    int rejected = 0;
    bool ok = false;
    /** Closest approach to `watchBody` (if set) during the run. */
    double closestApproachKm = 0.0;
    double closestApproachJd = 0.0;
};

/** Heliocentric acceleration (km/s^2) at `r` (km) and `julianDate`: the Sun plus every planet, direct and indirect terms. */
glm::dvec3 NBodyAcceleration(const glm::dvec3& r, double julianDate, const NBodyOptions& options);

/**
 * Integrate a heliocentric state from `jd0` to `jd1` (DOPRI5(4), local error control, FSAL).
 * `watchBody` >= 1 records the closest approach to that planet.
 */
NBodyResult PropagateNBody(const StateVector& start, double jd0, double jd1, const NBodyOptions& options = {},
                           int watchBody = -1);

// --- Flybys and spheres of influence ------------------------------------------------------

struct Flyby {
    double vInfInKmS = 0.0;
    double vInfOutKmS = 0.0;
    double turnAngleRad = 0.0;
    /** Periapsis radius (km from the centre) an unpowered flyby needs for this turn at the mean v∞. */
    double periapsisKm = 0.0;
    double altitudeKm = 0.0;
    /** |v∞out| - |v∞in|: what a powered flyby adds at periapsis, to first order. */
    double poweredDeltaVKmS = 0.0;
    /** Impact parameter and its B-plane components (T in the ecliptic, R toward south). */
    double bMagnitudeKm = 0.0;
    double bDotTKm = 0.0;
    double bDotRKm = 0.0;
    bool feasible = false; // periapsis above the body's minimum flyby altitude
};

/** Flyby of a body with `mu` and `radiusKm` that turns v∞ from `vInfIn` to `vInfOut` (planet-relative km/s). */
Flyby FlybyGeometry(const glm::dvec3& vInfIn, const glm::dvec3& vInfOut, double mu, double radiusKm,
                    double minAltitudeKm);

/** Δv from a circular orbit of radius `rpKm` about `mu` to a hyperbola with excess speed `vInf` (and back). */
double HyperbolicDeltaV(double mu, double rpKm, double vInfKmS);

/**
 * When a heliocentric arc first crosses `body`'s sphere of influence between jd0 and jd1
 * (`entering`: from outside to inside; otherwise inside to outside). Bisection on a sampled
 * distance; returns false if it does not cross.
 */
bool FindSoiCrossing(const StateVector& start, double jd0, double jd1, int body, bool entering, double& outJd);

} // namespace Flight

#endif // SOLARSYSTEM_ASTRO_FLIGHT_PROPAGATOR_H
