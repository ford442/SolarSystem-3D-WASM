#ifndef SOLARSYSTEM_ASTRO_FLIGHT_LAMBERT_H
#define SOLARSYSTEM_ASTRO_FLIGHT_LAMBERT_H
#include <glm/vec3.hpp>
#include <vector>

/**
 * Lambert's problem: the conic about `mu` that goes from r1 to r2 in `tof` seconds.
 *
 * Izzo's solver (D. Izzo, "Revisiting Lambert's problem", Celest. Mech. Dyn. Astr. 121, 2015):
 * the time of flight as a function of one variable x on the unit Lagrange-parameter
 * interval, a Householder (third-order) iteration from the paper's initial guesses, Battin's
 * hypergeometric series near the parabola, and every multi-revolution solution up to the most
 * revolutions the time of flight allows — two per revolution count (the "left" short-period
 * and "right" long-period branches). GL-free, allocation-free per solution.
 */
namespace Flight::Lambert {

struct Solution {
    glm::dvec3 v1{0.0}; // velocity leaving r1, km/s
    glm::dvec3 v2{0.0}; // velocity arriving at r2, km/s
    int revolutions = 0;
    bool rightBranch = false; // the long-period branch of a multi-rev pair
    int iterations = 0;
};

struct Options {
    /** Prograde about +z (ecliptic north): the short or long way is chosen so the craft goes the planets' way. */
    bool prograde = true;
    /** Most revolutions to try; the solver stops early where the time of flight allows fewer. */
    int maxRevolutions = 0;
    int maxIterations = 35;
    double tolerance = 1e-11;
};

/**
 * Every solution for r1 → r2 in `tofSeconds` (0-rev first, then each revolution count's left
 * and right branch). Empty for a degenerate geometry (r1 or r2 at the origin, tof <= 0, or a
 * transfer angle of exactly 0/180 degrees, where the transfer plane is undefined).
 */
std::vector<Solution> Solve(double mu, const glm::dvec3& r1, const glm::dvec3& r2, double tofSeconds,
                            const Options& options = {});

/** The 0-revolution prograde solution only; false where Solve would return none. */
bool SolveSingle(double mu, const glm::dvec3& r1, const glm::dvec3& r2, double tofSeconds, Solution& out);

} // namespace Flight::Lambert

#endif // SOLARSYSTEM_ASTRO_FLIGHT_LAMBERT_H
