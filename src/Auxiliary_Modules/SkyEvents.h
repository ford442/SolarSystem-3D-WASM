#ifndef SOLARSYSTEM_SKYEVENTS_H
#define SOLARSYSTEM_SKYEVENTS_H

#include <string>

/**
 * Sky events derived from the active Ephemeris backend — nothing is baked or animated.
 *
 * Accuracy inherits from the backend (Standish Table 1, ~arcmin for the planets), so event
 * times are good to within an hour or so, which is what an educational "next conjunction"
 * hint needs. Not a flight-dynamics or observing-grade tool.
 */
namespace SkyEvents {

/** Two bodies sharing a geocentric ecliptic longitude — the classical conjunction. */
struct Conjunction {
    bool valid = false;
    int bodyA = 0;              // Ephemeris / OrbitLayout::Body index, lower index first
    int bodyB = 0;
    double julianDate = 0.0;    // instant of equal geocentric ecliptic longitude
    double separationDeg = 0.0; // apparent separation at that instant (latitudes differ)
};

/** Geocentric ecliptic longitude and latitude of a body in degrees, as seen from Earth. */
void GeocentricLonLatDeg(int bodyIndex, double julianDate, double& lonDeg, double& latDeg);

/** Apparent angular separation of two bodies in degrees, as seen from Earth. */
double ApparentSeparationDeg(int bodyA, int bodyB, double julianDate);

/**
 * Next conjunction between two of the inner planets (Mercury, Venus, Mars) strictly after
 * fromJd, searching at most searchDays ahead. Returns an invalid result when none is found
 * in the window (which for these three would mean the window is only a few weeks long).
 */
Conjunction NextInnerPlanetConjunction(double fromJd, double searchDays = 730.0);

/** English body name for an index, e.g. "Venus"; empty for indices with no body. */
const char* BodyName(int bodyIndex);

/**
 * One-line hint, e.g. "Next conjunction: Venus-Mars, 2027-02-24 (1.2 deg apart)".
 * ASCII only, so callers can widen it with a plain char-by-char copy.
 * Empty for an invalid conjunction.
 */
std::string Format(const Conjunction& conjunction);

/** Format(NextInnerPlanetConjunction(...)); empty when the window holds no conjunction. */
std::string FormatNextInnerPlanetConjunction(double fromJd, double searchDays = 730.0);

} // namespace SkyEvents

#endif // SOLARSYSTEM_SKYEVENTS_H
