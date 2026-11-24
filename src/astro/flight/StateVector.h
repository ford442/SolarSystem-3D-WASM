#ifndef SOLARSYSTEM_ASTRO_FLIGHT_STATEVECTOR_H
#define SOLARSYSTEM_ASTRO_FLIGHT_STATEVECTOR_H
#include <glm/vec3.hpp>

// Heliocentric J2000-ecliptic Cartesian states for the flight planner: x toward the equinox,
// z toward the north ecliptic pole (Ephemeris's axes), kilometres and km/s. The scene's Y-up
// axes are (x, z, y) of these; FlightScene does that mapping.
namespace Flight {

struct StateVector {
    glm::dvec3 r{0.0}; // km
    glm::dvec3 v{0.0}; // km/s
};

/**
 * Heliocentric state of body `index` (OrbitLayout::Body 1 … 11) at `julianDate`, from the
 * active Ephemeris backend: the same positions the scene draws its planets at, so a transfer
 * that ends on a planet's position ends on its drawn disc. Velocity is the position's central
 * difference over ±0.01 d (the Standish series has no analytic rate); good to ~1 mm/s.
 */
StateVector PlanetState(int index, double julianDate);

/** Position only (km): PlanetState without the two extra ephemeris calls. */
glm::dvec3 PlanetPosition(int index, double julianDate);

/** Geocentric Moon position (km), from LunarTheory through Ephemeris::SatellitePosition. */
glm::dvec3 MoonGeocentricPosition(double julianDate);

/** Classical orbital elements of a state about `mu` (a < 0 for a hyperbola). */
struct Elements {
    double a = 0.0; // km
    double e = 0.0;
    double iRad = 0.0;
    double energy = 0.0; // km^2/s^2
};
Elements ElementsOf(const StateVector& state, double mu);

} // namespace Flight

#endif // SOLARSYSTEM_ASTRO_FLIGHT_STATEVECTOR_H
