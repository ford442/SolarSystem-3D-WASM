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
 * Greenwich Mean Sidereal Time in degrees [0, 360) for a UTC Julian Date (IAU 1982 series).
 *
 * Time scale note: we feed UTC straight in. GMST is formally a function of UT1, and the
 * sidereal series' argument is really TT-based, so ignoring UT1-UTC (< 0.9 s) and the
 * TT-UTC offset (~69 s in 2026, leap seconds and all) costs at most ~0.3 arcsec of Earth
 * rotation. That is far below the ~arcmin the Standish planet series itself is good for,
 * so the night side lands where a viewer expects at a given UTC. Not a flight-dynamics
 * tool — see docs/ARCHITECTURE.md § Ephemeris accuracy.
 */
double GreenwichMeanSiderealTimeDeg(double julianDate);

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
     * Parent-relative satellite position in AU, for a catalog satellite id
     * (BodyCatalog::Entry::index, e.g. 12 = Moon, 15 = Io).
     *
     * Axes are the J2000 ecliptic, same convention as PlanetHeliocentric: x toward the
     * equinox, z toward the north ecliptic pole. The Standish backend implements this for
     * the moons whose catalog rows carry real (non-placeholder) node and periapsis angles
     * — see kKeplerianSatellites in Ephemeris.cpp. Every other satellite returns false and
     * keeps its circular SatelliteOrbit offset.
     */
    virtual bool SatelliteParentRelative(int /*satelliteId*/, double /*julianDate*/,
                                         double outXyzAu[3]) const {
        outXyzAu[0] = outXyzAu[1] = outXyzAu[2] = 0.0;
        return false;
    }
};

/**
 * Parent-relative satellite position in AU from the active backend.
 * False (and {0,0,0}) when that satellite has no Keplerian solution.
 */
bool SatellitePosition(int satelliteId, double julianDate, double outXyzAu[3]);

/** JPL SSD "Approximate Positions of the Planets" Table 1 + Keplerian Pluto. */
const IEphemeris& StandishBackend();

/** Currently active backend (StandishBackend() unless overridden). */
const IEphemeris& GetBackend();

/** Select the active backend; nullptr restores the Standish default. */
void SetBackend(const IEphemeris* backend);

} // namespace Ephemeris

#endif // SOLARSYSTEM_EPHEMERIS_H
