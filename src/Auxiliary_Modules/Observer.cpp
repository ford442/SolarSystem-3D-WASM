#include "Observer.h"

#include "Ephemeris.h"
#include "SkyEvents.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Observer {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kArcsecToRad = kDegToRad / 3600.0;

constexpr double kObliquityJ2000Deg = 23.4392911;

// WGS84.
constexpr double kEarthEquatorialRadiusKm = 6378.137;
constexpr double kEarthFlattening = 1.0 / 298.257223563;

constexpr double kKmPerAu = 149597870.7;
constexpr double kSunRadiusKm = 695700.0;
constexpr double kMoonRadiusKm = 1737.4;

constexpr int kSunIndex = 0;
constexpr int kMoonIndex = SkyEvents::kMoon;

double wrapDeg(double deg) {
    const double w = std::fmod(deg, 360.0);
    return w < 0.0 ? w + 360.0 : w;
}

double dot3(const double a[3], const double b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

double length3(const double v[3]) { return std::sqrt(dot3(v, v)); }

void normalize3(const double in[3], double out[3]) {
    const double len = length3(in);
    if (len < 1e-300) {
        out[0] = 0.0;
        out[1] = 1.0;
        out[2] = 0.0;
        return;
    }
    out[0] = in[0] / len;
    out[1] = in[1] / len;
    out[2] = in[2] / len;
}

Mat3 multiply(const Mat3& a, const Mat3& b) {
    Mat3 r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

/** Active rotation by `angleRad` about +Z. */
Mat3 rotZ(double angleRad) {
    const double c = std::cos(angleRad);
    const double s = std::sin(angleRad);
    Mat3 r;
    r.m[0][0] = c;  r.m[0][1] = -s; r.m[0][2] = 0.0;
    r.m[1][0] = s;  r.m[1][1] = c;  r.m[1][2] = 0.0;
    r.m[2][0] = 0.0; r.m[2][1] = 0.0; r.m[2][2] = 1.0;
    return r;
}

/**
 * Equatorial of date → horizon frame (X = East, Y = Up, Z = -North).
 *
 * Rotating by -LST about the pole puts +x on the local meridian and +y toward the east, so
 * the horizon basis is East = y, Up = cos(lat) x + sin(lat) z, -North = sin(lat) x - cos(lat) z.
 */
Mat3 horizonFromEquatorialOfDate(double lstDeg, double latDeg) {
    const double sinLat = std::sin(latDeg * kDegToRad);
    const double cosLat = std::cos(latDeg * kDegToRad);
    Mat3 lat;
    lat.m[0][0] = 0.0;    lat.m[0][1] = 1.0; lat.m[0][2] = 0.0;
    lat.m[1][0] = cosLat; lat.m[1][1] = 0.0; lat.m[1][2] = sinLat;
    lat.m[2][0] = sinLat; lat.m[2][1] = 0.0; lat.m[2][2] = -cosLat;
    return multiply(lat, rotZ(-lstDeg * kDegToRad));
}

/** Observer's geocentric position in km, equatorial-of-date axes (Meeus ch. 11). */
void observerEquatorialKm(const Site& site, double lstDeg, double out[3]) {
    const double lat = site.latDeg * kDegToRad;
    const double ratio = 1.0 - kEarthFlattening;
    const double u = std::atan(ratio * std::tan(lat));
    const double h = site.altM / 1000.0 / kEarthEquatorialRadiusKm;
    const double rhoSin = ratio * std::sin(u) + h * std::sin(lat);
    const double rhoCos = std::cos(u) + h * std::cos(lat);
    const double lst = lstDeg * kDegToRad;
    out[0] = kEarthEquatorialRadiusKm * rhoCos * std::cos(lst);
    out[1] = kEarthEquatorialRadiusKm * rhoCos * std::sin(lst);
    out[2] = kEarthEquatorialRadiusKm * rhoSin;
}

double angleBetweenDeg(const double a[3], const double b[3]) {
    const double denom = length3(a) * length3(b);
    if (denom < 1e-300) {
        return 0.0;
    }
    return std::acos(std::clamp(dot3(a, b) / denom, -1.0, 1.0)) * kRadToDeg;
}

/** Astronomical Almanac visual magnitudes; the Saturn value leaves out the ring term. */
double planetMagnitude(int ephemerisIndex, double sunDistAu, double earthDistAu,
                       double phaseDeg) {
    const double i = phaseDeg;
    const double dist = 5.0 * std::log10(std::max(sunDistAu * earthDistAu, 1e-9));
    switch (ephemerisIndex) {
        case 1: return -0.42 + dist + 0.0380 * i - 0.000273 * i * i + 0.000002 * i * i * i;
        case 2: return -4.40 + dist + 0.0009 * i + 0.000239 * i * i - 0.00000065 * i * i * i;
        case 4: return -1.52 + dist + 0.016 * i;
        case 5: return -9.40 + dist + 0.005 * i;
        case 6: return -8.88 + dist + 0.044 * i;
        default: return 0.0;
    }
}

} // namespace

void Mat3::Apply(const double in[3], double out[3]) const {
    const double x = in[0], y = in[1], z = in[2];
    out[0] = m[0][0] * x + m[0][1] * y + m[0][2] * z;
    out[1] = m[1][0] * x + m[1][1] * y + m[1][2] * z;
    out[2] = m[2][0] * x + m[2][1] * y + m[2][2] * z;
}

double LocalSiderealTimeDeg(double julianDateUtc, double lonDegEast) {
    return wrapDeg(Ephemeris::GreenwichMeanSiderealTimeDeg(julianDateUtc) + lonDegEast);
}

Mat3 PrecessionJ2000ToDate(double julianDate) {
    const double t = (julianDate - Ephemeris::kJ2000) / 36525.0;
    const double zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t * t * t) * kArcsecToRad;
    const double z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t * t * t) * kArcsecToRad;
    const double theta = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t * t * t) * kArcsecToRad;

    // Meeus 21.4: RA += zeta, tilt the pole by theta about Y, RA += z.
    Mat3 tilt;
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    tilt.m[0][0] = c;   tilt.m[0][1] = 0.0; tilt.m[0][2] = -s;
    tilt.m[1][0] = 0.0; tilt.m[1][1] = 1.0; tilt.m[1][2] = 0.0;
    tilt.m[2][0] = s;   tilt.m[2][1] = 0.0; tilt.m[2][2] = c;
    return multiply(rotZ(z), multiply(tilt, rotZ(zeta)));
}

Mat3 EclipticToEquatorialJ2000() {
    const double c = std::cos(kObliquityJ2000Deg * kDegToRad);
    const double s = std::sin(kObliquityJ2000Deg * kDegToRad);
    Mat3 r;
    r.m[0][0] = 1.0; r.m[0][1] = 0.0; r.m[0][2] = 0.0;
    r.m[1][0] = 0.0; r.m[1][1] = c;   r.m[1][2] = -s;
    r.m[2][0] = 0.0; r.m[2][1] = s;   r.m[2][2] = c;
    return r;
}

Mat3 HorizonFromEquatorialJ2000(double julianDateUtc, const Site& site) {
    const double lst = LocalSiderealTimeDeg(julianDateUtc, site.lonDeg);
    return multiply(horizonFromEquatorialOfDate(lst, site.latDeg),
                    PrecessionJ2000ToDate(julianDateUtc));
}

Mat3 HorizonFromEclipticJ2000(double julianDateUtc, const Site& site) {
    return multiply(HorizonFromEquatorialJ2000(julianDateUtc, site), EclipticToEquatorialJ2000());
}

void EquatorialToHorizontal(double raDeg, double decDeg, double lstDeg, double latDeg,
                            double& altDeg, double& azDeg) {
    const double h = (lstDeg - raDeg) * kDegToRad;
    const double dec = decDeg * kDegToRad;
    const double lat = latDeg * kDegToRad;
    const double sinAlt = std::sin(dec) * std::sin(lat) + std::cos(dec) * std::cos(lat) * std::cos(h);
    altDeg = std::asin(std::clamp(sinAlt, -1.0, 1.0)) * kRadToDeg;
    const double y = -std::cos(dec) * std::sin(h);
    const double x = std::sin(dec) * std::cos(lat) - std::cos(dec) * std::sin(lat) * std::cos(h);
    azDeg = wrapDeg(std::atan2(y, x) * kRadToDeg);
}

void AltAzFromDirection(const double dir[3], double& altDeg, double& azDeg) {
    double u[3];
    normalize3(dir, u);
    altDeg = std::asin(std::clamp(u[1], -1.0, 1.0)) * kRadToDeg;
    azDeg = wrapDeg(std::atan2(u[0], -u[2]) * kRadToDeg);
}

double DiscOverlapFraction(double radiusADeg, double radiusBDeg, double separationDeg) {
    const double r1 = radiusADeg;
    const double r2 = radiusBDeg;
    const double d = std::fabs(separationDeg);
    if (r1 <= 0.0 || r2 <= 0.0 || d >= r1 + r2) {
        return 0.0;
    }
    if (d <= std::fabs(r1 - r2)) {
        return r2 >= r1 ? 1.0 : (r2 * r2) / (r1 * r1);
    }
    const double a1 = std::acos(std::clamp((d * d + r1 * r1 - r2 * r2) / (2.0 * d * r1), -1.0, 1.0));
    const double a2 = std::acos(std::clamp((d * d + r2 * r2 - r1 * r1) / (2.0 * d * r2), -1.0, 1.0));
    const double kite = 0.5 * std::sqrt(std::max(
                                  0.0, (-d + r1 + r2) * (d + r1 - r2) * (d - r1 + r2) * (d + r1 + r2)));
    const double area = r1 * r1 * a1 + r2 * r2 * a2 - kite;
    return std::clamp(area / (kPi * r1 * r1), 0.0, 1.0);
}

Mat3 MoonBodyAxesEquatorialJ2000(double julianDate) {
    const double d = julianDate - Ephemeris::kJ2000;
    const double t = d / 36525.0;
    auto sinDeg = [](double deg) { return std::sin(deg * kDegToRad); };
    auto cosDeg = [](double deg) { return std::cos(deg * kDegToRad); };

    // IAU 2009 libration arguments E1..E13 (degrees; d in days from J2000).
    const double e1 = 125.045 - 0.0529921 * d;
    const double e2 = 250.089 - 0.1059842 * d;
    const double e3 = 260.008 + 13.0120009 * d;
    const double e4 = 176.625 + 13.3407154 * d;
    const double e5 = 357.529 + 0.9856003 * d;
    const double e6 = 311.589 + 26.4057084 * d;
    const double e7 = 134.963 + 13.0649930 * d;
    const double e8 = 276.617 + 0.3287146 * d;
    const double e9 = 34.226 + 1.7484877 * d;
    const double e10 = 15.134 - 0.1589763 * d;
    const double e11 = 119.743 + 0.0036096 * d;
    const double e12 = 239.961 + 0.1643573 * d;
    const double e13 = 25.053 + 12.9590088 * d;

    const double alpha0 = 269.9949 + 0.0031 * t - 3.8787 * sinDeg(e1) - 0.1204 * sinDeg(e2) +
                          0.0700 * sinDeg(e3) - 0.0172 * sinDeg(e4) + 0.0072 * sinDeg(e6) -
                          0.0052 * sinDeg(e10) + 0.0043 * sinDeg(e13);
    const double delta0 = 66.5392 + 0.0130 * t + 1.5419 * cosDeg(e1) + 0.0239 * cosDeg(e2) -
                          0.0278 * cosDeg(e3) + 0.0068 * cosDeg(e4) - 0.0029 * cosDeg(e6) +
                          0.0009 * cosDeg(e7) + 0.0008 * cosDeg(e10) - 0.0009 * cosDeg(e13);
    const double w = 38.3213 + 13.17635815 * d - 1.4e-12 * d * d + 3.5610 * sinDeg(e1) +
                     0.1208 * sinDeg(e2) - 0.0642 * sinDeg(e3) + 0.0158 * sinDeg(e4) +
                     0.0252 * sinDeg(e5) - 0.0066 * sinDeg(e6) - 0.0047 * sinDeg(e7) -
                     0.0046 * sinDeg(e8) + 0.0028 * sinDeg(e9) + 0.0052 * sinDeg(e10) +
                     0.0040 * sinDeg(e11) + 0.0019 * sinDeg(e12) - 0.0044 * sinDeg(e13);

    // Pole Z, ascending node Q of the lunar equator on the ICRF equator, then the prime
    // meridian X is Q turned by W about Z (counter-clockwise seen from the north pole).
    const double a = alpha0 * kDegToRad;
    const double dl = delta0 * kDegToRad;
    const double z[3] = {std::cos(dl) * std::cos(a), std::cos(dl) * std::sin(a), std::sin(dl)};
    const double q[3] = {-std::sin(a), std::cos(a), 0.0};
    const double zq[3] = {z[1] * q[2] - z[2] * q[1], z[2] * q[0] - z[0] * q[2], z[0] * q[1] - z[1] * q[0]};
    const double cw = std::cos(w * kDegToRad);
    const double sw = std::sin(w * kDegToRad);
    double x[3], y[3];
    for (int i = 0; i < 3; ++i) {
        x[i] = q[i] * cw + zq[i] * sw;
    }
    y[0] = z[1] * x[2] - z[2] * x[1];
    y[1] = z[2] * x[0] - z[0] * x[2];
    y[2] = z[0] * x[1] - z[1] * x[0];

    Mat3 axes;
    for (int i = 0; i < 3; ++i) {
        axes.m[i][0] = x[i];
        axes.m[i][1] = y[i];
        axes.m[i][2] = z[i];
    }
    return axes;
}

std::string FormatUtc(double julianDateUtc) {
    // Round to the second first so 23:59:59.7 rolls into the next day instead of printing 60 s.
    const double shifted = julianDateUtc + 0.5 + 0.5 / 86400.0;
    const double dayFloor = std::floor(shifted);
    const int secondOfDay = std::min(86399, static_cast<int>((shifted - dayFloor) * 86400.0));
    int year = 0, month = 0, day = 0;
    Ephemeris::YmdFromJulianDate(dayFloor, year, month, day); // floor(dayFloor + 0.5) == dayFloor
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day,
                  secondOfDay / 3600, (secondOfDay / 60) % 60, secondOfDay % 60);
    return buffer;
}

Sky ComputeSky(double julianDateUtc, const Site& site) {
    Sky sky;
    sky.julianDate = julianDateUtc;
    sky.site = site;
    sky.lstDeg = LocalSiderealTimeDeg(julianDateUtc, site.lonDeg);

    const Mat3 precession = PrecessionJ2000ToDate(julianDateUtc);
    const Mat3 eclToEq = EclipticToEquatorialJ2000();
    const Mat3 toDate = multiply(precession, eclToEq);                 // ecliptic J2000 → equatorial of date
    const Mat3 toHorizon = horizonFromEquatorialOfDate(sky.lstDeg, site.latDeg);

    double obs[3];
    observerEquatorialKm(site, sky.lstDeg, obs);

    // Moon orientation: body axes from the IAU model, carried into the horizon frame.
    {
        const Mat3 axesEq = MoonBodyAxesEquatorialJ2000(julianDateUtc);
        const Mat3 eqToHorizon = HorizonFromEquatorialJ2000(julianDateUtc, site);
        for (int col = 0; col < 3; ++col) {
            const double axisEq[3] = {axesEq.m[0][col], axesEq.m[1][col], axesEq.m[2][col]};
            double axisHorizon[3];
            eqToHorizon.Apply(axisEq, axisHorizon);
            for (int row = 0; row < 3; ++row) {
                sky.moonBodyToHorizon.m[row][col] = axisHorizon[row];
            }
        }
        // Sub-Earth point: the direction from the Moon to the geocentre, in body coordinates.
        double moonEclKm[3] = {0.0, 0.0, 0.0};
        SkyEvents::GeocentricEclipticKm(kMoonIndex, julianDateUtc, moonEclKm);
        double towardEarthEcl[3] = {-moonEclKm[0], -moonEclKm[1], -moonEclKm[2]};
        double towardEarthEq[3];
        EclipticToEquatorialJ2000().Apply(towardEarthEcl, towardEarthEq);
        double u[3];
        normalize3(towardEarthEq, u);
        const double bx = dot3(u, std::array<double, 3>{axesEq.m[0][0], axesEq.m[1][0], axesEq.m[2][0]}.data());
        const double by = dot3(u, std::array<double, 3>{axesEq.m[0][1], axesEq.m[1][1], axesEq.m[2][1]}.data());
        const double bz = dot3(u, std::array<double, 3>{axesEq.m[0][2], axesEq.m[1][2], axesEq.m[2][2]}.data());
        sky.moonSubEarthLonDeg = std::atan2(by, bx) * kRadToDeg;
        sky.moonSubEarthLatDeg = std::asin(std::clamp(bz, -1.0, 1.0)) * kRadToDeg;
    }

    // Geocentric vectors in km, ecliptic J2000.
    double sunEcl[3] = {0.0, 0.0, 0.0};
    SkyEvents::GeocentricEclipticKm(kSunIndex, julianDateUtc, sunEcl);

    // The Moon is the only body with parallax; its phase is measured from the geocentre.
    double moonEcl[3] = {0.0, 0.0, 0.0};
    SkyEvents::GeocentricEclipticKm(kMoonIndex, julianDateUtc, moonEcl);

    struct Entry {
        SkyIndex slot;
        int ephemerisIndex;
        const char* name;
    };
    const Entry entries[] = {
        {kSkySun, kSunIndex, "Sun"},       {kSkyMoon, kMoonIndex, "Moon"},
        {kSkyMercury, 1, "Mercury"},       {kSkyVenus, 2, "Venus"},
        {kSkyMars, 4, "Mars"},             {kSkyJupiter, 5, "Jupiter"},
        {kSkySaturn, 6, "Saturn"},
    };

    double sunTopoHorizon[3] = {0.0, 1.0, 0.0};

    for (const Entry& entry : entries) {
        SkyBody& body = sky.bodies[entry.slot];
        body.ephemerisIndex = entry.ephemerisIndex;
        body.name = entry.name;

        double ecl[3] = {0.0, 0.0, 0.0};
        SkyEvents::GeocentricEclipticKm(entry.ephemerisIndex, julianDateUtc, ecl);
        const double geoDistKm = length3(ecl);

        double eqDate[3];
        toDate.Apply(ecl, eqDate);
        if (entry.ephemerisIndex == kMoonIndex) {
            eqDate[0] -= obs[0];
            eqDate[1] -= obs[1];
            eqDate[2] -= obs[2];
        }
        double horizon[3];
        toHorizon.Apply(eqDate, horizon);

        body.distanceKm = length3(horizon);
        normalize3(horizon, body.dir);
        AltAzFromDirection(body.dir, body.altDeg, body.azDeg);

        if (entry.slot == kSkySun) {
            body.angRadiusDeg = std::asin(std::min(1.0, kSunRadiusKm / geoDistKm)) * kRadToDeg;
            body.magnitude = -26.74 + 5.0 * std::log10(geoDistKm / kKmPerAu);
            body.phaseAngleDeg = 0.0;
            body.illuminatedFraction = 1.0;
            sunTopoHorizon[0] = body.dir[0];
            sunTopoHorizon[1] = body.dir[1];
            sunTopoHorizon[2] = body.dir[2];
        } else if (entry.slot == kSkyMoon) {
            body.angRadiusDeg = std::asin(std::min(1.0, kMoonRadiusKm / body.distanceKm)) * kRadToDeg;
            // Phase angle at the Moon: between the Moon→Sun and Moon→Earth directions.
            const double toSun[3] = {sunEcl[0] - moonEcl[0], sunEcl[1] - moonEcl[1],
                                     sunEcl[2] - moonEcl[2]};
            const double toEarth[3] = {-moonEcl[0], -moonEcl[1], -moonEcl[2]};
            body.phaseAngleDeg = angleBetweenDeg(toSun, toEarth);
            body.illuminatedFraction = 0.5 * (1.0 + std::cos(body.phaseAngleDeg * kDegToRad));
            const double i = body.phaseAngleDeg;
            body.magnitude = -12.73 + 0.026 * i + 4.0e-9 * i * i * i * i;
        } else {
            const double helio[3] = {ecl[0] - sunEcl[0], ecl[1] - sunEcl[1], ecl[2] - sunEcl[2]};
            const double toSun[3] = {-helio[0], -helio[1], -helio[2]};
            const double toEarth[3] = {-ecl[0], -ecl[1], -ecl[2]};
            body.phaseAngleDeg = angleBetweenDeg(toSun, toEarth);
            body.illuminatedFraction = 0.5 * (1.0 + std::cos(body.phaseAngleDeg * kDegToRad));
            body.magnitude = planetMagnitude(entry.ephemerisIndex, length3(helio) / kKmPerAu,
                                             geoDistKm / kKmPerAu, body.phaseAngleDeg);
        }
    }

    // Bright limb: the Sun's direction with the Moon-ward component removed, per body. For the
    // Moon this is the line the terminator is perpendicular to; planets get it for free.
    for (int slot = kSkyMoon; slot < kSkyBodyCount; ++slot) {
        SkyBody& body = sky.bodies[slot];
        const double along = dot3(sunTopoHorizon, body.dir);
        const double perp[3] = {sunTopoHorizon[0] - along * body.dir[0],
                                sunTopoHorizon[1] - along * body.dir[1],
                                sunTopoHorizon[2] - along * body.dir[2]};
        if (length3(perp) < 1e-9) {
            // Sun and body coincide or oppose: no preferred limb, point it up the sky.
            body.brightLimbDir[0] = 0.0;
            body.brightLimbDir[1] = 1.0;
            body.brightLimbDir[2] = 0.0;
        } else {
            normalize3(perp, body.brightLimbDir);
        }
    }

    // Eclipse geometry from the topocentric directions (the Moon's parallax is already in its).
    {
        const SkyBody& sun = sky.bodies[kSkySun];
        const SkyBody& moon = sky.bodies[kSkyMoon];
        sky.sunMoonSeparationDeg = std::acos(std::clamp(dot3(sun.dir, moon.dir), -1.0, 1.0)) * kRadToDeg;
        sky.sunCoverage = DiscOverlapFraction(sun.angRadiusDeg, moon.angRadiusDeg, sky.sunMoonSeparationDeg);
    }

    return sky;
}

bool BodyAltitudeDeg(int ephemerisIndex, double julianDateUtc, const Site& site, double& altDeg) {
    const Sky sky = ComputeSky(julianDateUtc, site);
    for (const SkyBody& body : sky.bodies) {
        if (body.ephemerisIndex == ephemerisIndex) {
            altDeg = body.altDeg;
            return true;
        }
    }
    return false;
}

} // namespace Observer
