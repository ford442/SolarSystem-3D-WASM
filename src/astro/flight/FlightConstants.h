#ifndef SOLARSYSTEM_ASTRO_FLIGHT_FLIGHTCONSTANTS_H
#define SOLARSYSTEM_ASTRO_FLIGHT_FLIGHTCONSTANTS_H

// Physical constants for the flight planner (docs/ARCHITECTURE.md §14). Kilometres, seconds,
// km^3/s^2 throughout; Julian dates in days. GM values are the IAU/JPL DE440 system values (the
// planets' include their moons: patched conics treats each system as one point mass).
namespace Flight {

constexpr double kPi = 3.14159265358979323846;
constexpr double kAuKm = 149597870.7;
constexpr double kSecondsPerDay = 86400.0;
constexpr double kMuSun = 1.32712440041279419e11;

/** One body the planner can depart from, fly by or arrive at (OrbitLayout::Body index). */
struct BodyConstants {
    int index = -1;                  // OrbitLayout::Body / Ephemeris index
    const char* id = "";             // lower-case planner id ("earth")
    const char* name = "";           // display name
    double mu = 0.0;                 // GM, km^3/s^2
    double radiusKm = 0.0;           // mean (equatorial for the giants) radius
    double smaAu = 0.0;              // mean semi-major axis, for the sphere of influence
    double minFlybyAltitudeKm = 0.0; // the lowest periapsis a ballistic flyby may use
};

/** Mercury (1) … Vesta (11); nullptr for the Sun or an index the planner does not know. */
const BodyConstants* FindBody(int index);
/** By id ("mars", case-insensitive); nullptr if unknown. */
const BodyConstants* FindBody(const char* id);

/** Laplace sphere of influence about the Sun, km: a (m/M)^(2/5). */
double SphereOfInfluenceKm(const BodyConstants& body);

} // namespace Flight

#endif // SOLARSYSTEM_ASTRO_FLIGHT_FLIGHTCONSTANTS_H
