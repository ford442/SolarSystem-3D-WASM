#ifndef SOLARSYSTEM_EPHEMERIS_H
#define SOLARSYSTEM_EPHEMERIS_H

// Standish/JPL low-precision heliocentric positions (Table 1, 1800–2050)
// plus a Keplerian approximation for Pluto.
// Body indices match OrbitLayout::Body / FocusPlanet (0=Sun … 9=Pluto).
namespace Ephemeris {

constexpr double kJ2000 = 2451545.0;

struct HelioLB {
    double lonRad = 0.0; // heliocentric ecliptic longitude
    double latRad = 0.0; // heliocentric ecliptic latitude
    double rAu = 0.0;    // heliocentric distance (AU)
};

/** Gregorian calendar date (UTC civil) → Julian Date at 0h. */
double JulianDateFromYmd(int year, int month, int day);

/** Julian Date → Gregorian Y-M-D (floor of civil day). */
void YmdFromJulianDate(double jd, int& year, int& month, int& day);

/** Current UTC instant as Julian Date (including fractional day). */
double JulianDateNowUtc();

/** Heliocentric ecliptic lon/lat/radius for Mercury–Pluto; Sun/unknown return zeros. */
HelioLB Position(int bodyIndex, double julianDate);

} // namespace Ephemeris

#endif // SOLARSYSTEM_EPHEMERIS_H
