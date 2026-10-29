#ifndef SOLARSYSTEM_OBSERVER_H
#define SOLARSYSTEM_OBSERVER_H

#include <array>
#include <string>

/**
 * A ground observer on Earth: where the Sun, Moon, planets and stars are in the local sky.
 *
 * Pure maths (doubles, no GL), derived from the active Ephemeris backend and the GMST series.
 * It never touches the Earth model's matrix — the model's pole and prime meridian are art —
 * so everything here goes ecliptic J2000 → equatorial J2000 → equatorial of date → horizon.
 *
 * Horizon frame (right-handed): +X = East, +Y = Up, +Z = -North. With the scene Camera's
 * Euler angles that means yaw -90 looks north and azimuth = yaw + 90.
 *
 * Accuracy: a visualization, not an almanac. Time is UTC fed straight into GMST (see the
 * UTC/TT note in Ephemeris.h), nutation (~17") and aberration are ignored, and planet positions
 * inherit the ~arcmin Standish series. Only the Moon gets topocentric parallax (up to ~1 deg),
 * because it matters for eclipses; the Sun and planets are treated as geocentric.
 *
 * Refraction is the one atmospheric term that is modelled, because at the horizon (~0.5 deg) it
 * is larger than every other error here and it is what a ground observer checks first: without
 * it the Sun sets early by about its own diameter. Each SkyBody carries both the geometric
 * (`dir`/`altDeg`) and the apparent (`apparentDir`/`apparentAltDeg`) position; draw the apparent
 * one, do geometry (eclipses, phases, lighting) with the geometric one.
 */
namespace Observer {

struct Site {
    double latDeg = 0.0;     // geodetic latitude, north positive
    double lonDeg = 0.0;     // east longitude, east positive
    double altM = 0.0;       // height above the ellipsoid
};

/** Row-major 3x3 rotation matrix. */
struct Mat3 {
    double m[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

    void Apply(const double in[3], double out[3]) const;
};

/** Local mean sidereal time in degrees [0, 360) at an east longitude. */
double LocalSiderealTimeDeg(double julianDateUtc, double lonDegEast);

/** Rotation taking equatorial J2000 vectors to the mean equator/equinox of date (Meeus 21.2). */
Mat3 PrecessionJ2000ToDate(double julianDate);

/** Fixed rotation from J2000 ecliptic axes to J2000 equatorial axes (obliquity 23.4392911 deg). */
Mat3 EclipticToEquatorialJ2000();

/** Equatorial J2000 → horizon frame for this site and instant. Use it for fixed stars. */
Mat3 HorizonFromEquatorialJ2000(double julianDateUtc, const Site& site);

/** Ecliptic J2000 → horizon frame (the composition with EclipticToEquatorialJ2000). */
Mat3 HorizonFromEclipticJ2000(double julianDateUtc, const Site& site);

/** Altitude and azimuth (from north through east) of an equatorial position of date. */
void EquatorialToHorizontal(double raDeg, double decDeg, double lstDeg, double latDeg,
                            double& altDeg, double& azDeg);

/** Altitude/azimuth in degrees of a horizon-frame direction (any length). */
void AltAzFromDirection(const double dir[3], double& altDeg, double& azDeg);

/**
 * Fraction [0, 1] of disc A's area covered by disc B, for angular radii and centre separation
 * in degrees. 1 means A is fully hidden (total eclipse), 0 means the discs do not overlap.
 */
double DiscOverlapFraction(double radiusADeg, double radiusBDeg, double separationDeg);

/**
 * Lunar body-fixed axes in equatorial J2000, as the columns of a rotation (m[row][col] with
 * col 0 = X toward selenographic lon 0 / lat 0, col 1 = Y toward lon +90 east, col 2 = Z the
 * north pole). IAU 2009 WGCCRE orientation: pole (alpha0, delta0) and prime-meridian angle W
 * including the E1..E13 libration terms. This is the full lunar orientation, so optical
 * librations (up to ~7 deg in longitude, ~6.7 deg in latitude) come out of the Moon's real
 * geometry instead of being a separate correction.
 */
Mat3 MoonBodyAxesEquatorialJ2000(double julianDate);

// Standard atmosphere for refraction: 1010 hPa and 10 deg C, the conditions Saemundsson's and
// Bennett's formulae are normalised to (Meeus, Astronomical Algorithms, ch. 16).
constexpr double kRefractionPressureHpa = 1010.0;
constexpr double kRefractionTemperatureC = 10.0;
// Saemundsson (1986): R = A / tan(h + B / (h + C)) arcmin for a geometric altitude h in degrees.
// common/refraction.glsl repeats these numbers; test_observer.cpp checks that they agree.
constexpr double kSaemundssonA = 1.02;
constexpr double kSaemundssonB = 10.3;
constexpr double kSaemundssonC = 5.11;
// Below this geometric altitude the formula is held at its value here and faded to zero by
// kRefractionZeroBelowDeg: it diverges near h = -C, and everything down there is under the ground.
constexpr double kRefractionHoldBelowDeg = -1.0;
constexpr double kRefractionZeroBelowDeg = -5.0;
// V-band extinction per airmass at a good sea-level site (0.2 mag is the usual textbook value).
constexpr double kExtinctionMagPerAirmass = 0.2;

/**
 * Atmospheric refraction in arcminutes for a body at `geometricAltDeg` (Saemundsson), scaled for
 * pressure and temperature. ~29' at the geometric horizon, ~1' at 45 deg, 0 at the zenith.
 */
double RefractionArcmin(double geometricAltDeg, double pressureHpa = kRefractionPressureHpa,
                        double temperatureC = kRefractionTemperatureC);

/** Geometric altitude plus refraction, both in degrees, at the standard atmosphere. */
double ApparentAltitudeDeg(double geometricAltDeg);

/** A horizon-frame unit direction lifted by refraction along its own azimuth. */
void RefractDirection(const double geometric[3], double apparent[3]);

/** Relative air mass at an apparent altitude (Kasten & Young 1989): 1 at the zenith, ~38 at the horizon. */
double Airmass(double apparentAltDeg);

/** How many magnitudes the air takes off a body at an apparent altitude, relative to the zenith. */
double ExtinctionMagnitudes(double apparentAltDeg);

/** "YYYY-MM-DD HH:MM:SS" for a UTC Julian Date, rounded to the nearest second. */
std::string FormatUtc(double julianDateUtc);

/** Bodies in a sky snapshot, in this order. */
enum SkyIndex : int {
    kSkySun = 0,
    kSkyMoon,
    kSkyMercury,
    kSkyVenus,
    kSkyMars,
    kSkyJupiter,
    kSkySaturn,
    kSkyBodyCount,
};

struct SkyBody {
    int ephemerisIndex = 0;            // Ephemeris / SkyEvents body index (Moon = 12)
    const char* name = "";
    double dir[3] = {0.0, 1.0, 0.0};   // unit vector, horizon frame, geometric (no refraction)
    double altDeg = 0.0;               // geometric
    double azDeg = 0.0;                // refraction leaves the azimuth alone
    double apparentDir[3] = {0.0, 1.0, 0.0}; // `dir` lifted by refraction: where it is drawn
    double apparentAltDeg = 0.0;
    double extinctionMag = 0.0;        // airmass dimming at the apparent altitude, magnitudes
    double distanceKm = 0.0;           // topocentric for the Moon, geocentric otherwise
    double angRadiusDeg = 0.0;         // planets: equatorial radius (Saturn without rings)
    double magnitude = 0.0;
    double phaseAngleDeg = 0.0;        // Sun–body–Earth angle
    double illuminatedFraction = 1.0;  // [0, 1]
    double brightLimbDir[3] = {0.0, 1.0, 0.0}; // unit, horizon frame, toward the Sun's side
};

struct Sky {
    double julianDate = 0.0;   // the UTC instant and site this snapshot was computed for
    Site site;
    double lstDeg = 0.0;
    // Moon body axes (columns: lon 0 / lon +90 east / north pole) in the horizon frame. Rotate a
    // surface normal with the transpose to get its selenographic longitude and latitude.
    Mat3 moonBodyToHorizon;
    // Topocentric angle between the Sun's and Moon's centres, and the fraction of the Sun's disc
    // the Moon covers (area, 0..1). 1 is totality; with the Moon's disc bigger than the Sun's
    // it holds for a range of separations, which is what makes totality last minutes.
    double sunMoonSeparationDeg = 180.0;
    double sunCoverage = 0.0;
    // Selenographic position of the sub-Earth point: the libration the observer sees.
    double moonSubEarthLonDeg = 0.0;
    double moonSubEarthLatDeg = 0.0;
    std::array<SkyBody, kSkyBodyCount> bodies;
};

/** Where every naked-eye body is for this site at this UTC instant. */
Sky ComputeSky(double julianDateUtc, const Site& site);

/**
 * Apparent (refracted) altitude in degrees of an Ephemeris body index (0 Sun, 12 Moon, 1/2/4/5/6
 * the naked-eye planets) at an instant: what decides whether an observer can see it. False for bodies the sky snapshot does not carry (Earth, the outer
 * planets, asteroids), leaving `altDeg` untouched.
 */
bool BodyAltitudeDeg(int ephemerisIndex, double julianDateUtc, const Site& site, double& altDeg);

} // namespace Observer

#endif // SOLARSYSTEM_OBSERVER_H
