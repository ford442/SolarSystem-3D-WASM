#ifndef SOLARSYSTEM_LUNARTHEORY_H
#define SOLARSYSTEM_LUNARTHEORY_H

/**
 * The Moon's geocentric position from the truncated ELP-2000/82 series in Meeus,
 * Astronomical Algorithms 2nd ed., chapter 47 (60 longitude/distance terms, 60 latitude
 * terms, plus the Venus/Jupiter/flattening additive terms). Pure maths, no ephemeris state.
 *
 * Accuracy per Meeus: about 10" in longitude and 4" in latitude (~0.003 deg) over the
 * present era, versus 1–2.4 deg for the J2000 mean-element Moon the catalog row carries —
 * that is the difference between an eclipse landing on the right hour and on the right month.
 * The series is in TD (use TT for the argument) and referred to the mean equinox of date,
 * without nutation; MoonGeocentricJ2000Au carries it to the J2000 ecliptic the rest of the
 * ephemeris uses.
 */
namespace LunarTheory {

struct MoonOfDate {
    double lonDeg = 0.0;  // geocentric ecliptic longitude, mean equinox of date
    double latDeg = 0.0;  // geocentric ecliptic latitude
    double distanceKm = 0.0;
};

/** Meeus ch. 47 at a Julian Ephemeris Day (TD/TT). */
MoonOfDate MeeusMoon(double julianEphemerisDay);

/**
 * Ecliptic longitude/latitude referred to the mean ecliptic and equinox of `julianEphemerisDay`
 * → the J2000 ecliptic (Meeus 21.5–21.7, which also accounts for the ecliptic's own motion).
 */
void EclipticOfDateToJ2000(double julianEphemerisDay, double lonDeg, double latDeg,
                           double& lonJ2000Deg, double& latJ2000Deg);

/** Geocentric Moon in AU on the J2000 ecliptic axes, from a Julian Ephemeris Day. */
void MoonGeocentricJ2000Au(double julianEphemerisDay, double outXyzAu[3]);

} // namespace LunarTheory

#endif // SOLARSYSTEM_LUNARTHEORY_H
