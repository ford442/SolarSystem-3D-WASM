#ifndef SOLARSYSTEM_EPHEMERIS_H
#define SOLARSYSTEM_EPHEMERIS_H

// Standish/JPL low-precision heliocentric positions (Table 1, 1800–2050) plus fixed-element
// Keplerian approximations for Pluto and the catalog belt bodies.
// Body indices match OrbitLayout::Body / FocusPlanet (0=Sun … 9=Pluto, 10=Ceres, 11=Vesta).
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

/**
 * Heliocentric ecliptic lon/lat/radius for Mercury–Vesta; Sun/unknown return zeros.
 * Delegates to the active backend (see SetBackend); the default is Standish Table 1.
 */
HelioLB Position(int bodyIndex, double julianDate);

/**
 * Pluggable position source. Julian dates are treated as UTC-ish for visualization —
 * we do not model TDB/TT offsets (~69 s), which is well under the ~arcmin accuracy the
 * Standish series itself provides. Do not treat this as a flight-dynamics tool.
 */
class IEphemeris {
public:
    virtual ~IEphemeris() = default;

    /** Short identifier for hints/logging, e.g. "standish". */
    virtual const char* Name() const = 0;

    /** Heliocentric ecliptic position; Sun (0) and out-of-range indices return zeros. */
    virtual HelioLB PlanetHeliocentric(int bodyIndex, double julianDate) const = 0;

    /**
     * Parent-relative satellite position in AU, for a catalog satellite id.
     * No backend implements this yet — moons still use the circular SatelliteOrbit
     * helpers with per-file constants. Kept on the interface so a Keplerian satellite
     * backend can be added without changing callers. Returns {0,0,0} when unsupported.
     */
    virtual bool SatelliteParentRelative(int /*satelliteId*/, double /*julianDate*/,
                                         double outXyzAu[3]) const {
        outXyzAu[0] = outXyzAu[1] = outXyzAu[2] = 0.0;
        return false;
    }
};

/** JPL SSD "Approximate Positions of the Planets" Table 1 + Keplerian Pluto. */
const IEphemeris& StandishBackend();

/** Currently active backend (StandishBackend() unless overridden). */
const IEphemeris& GetBackend();

/** Select the active backend; nullptr restores the Standish default. */
void SetBackend(const IEphemeris* backend);

} // namespace Ephemeris

#endif // SOLARSYSTEM_EPHEMERIS_H
