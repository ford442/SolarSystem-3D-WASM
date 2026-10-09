#include "LunarTheory.h"

#include <cmath>

namespace LunarTheory {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kArcsecToRad = kDegToRad / 3600.0;
constexpr double kKmPerAu = 149597870.7;

// Meeus Table 47.A: multiples of D, M, M', F and the coefficients of the sine (longitude, in
// 1e-6 deg) and cosine (distance, in 1e-3 km) series. Terms in M carry the eccentricity factor E.
struct LongitudeDistanceTerm {
    int d, m, mp, f;
    int sumL;
    int sumR;
};

// Meeus Table 47.B: the same arguments with the sine series for latitude (1e-6 deg).
struct LatitudeTerm {
    int d, m, mp, f;
    int sumB;
};

constexpr LongitudeDistanceTerm kTableA[] = {
    {0, 0, 1, 0, 6288774, -20905355},
    {2, 0, -1, 0, 1274027, -3699111},
    {2, 0, 0, 0,  658314, -2955968},
    {0, 0, 2, 0,  213618, -569925},
    {0, 1, 0, 0, -185116,   48888},
    {0, 0, 0, 2, -114332,   -3149},
    {2, 0, -2, 0,   58793,  246158},
    {2, -1, -1, 0,   57066, -152138},
    {2, 0, 1, 0,   53322, -170733},
    {2, -1, 0, 0,   45758, -204586},
    {0, 1, -1, 0,  -40923, -129620},
    {1, 0, 0, 0,  -34720,  108743},
    {0, 1, 1, 0,  -30383,  104755},
    {2, 0, 0, -2,   15327,   10321},
    {0, 0, 1, 2,  -12528,       0},
    {0, 0, 1, -2,   10980,   79661},
    {4, 0, -1, 0,   10675,  -34782},
    {0, 0, 3, 0,   10034,  -23210},
    {4, 0, -2, 0,    8548,  -21636},
    {2, 1, -1, 0,   -7888,   24208},
    {2, 1, 0, 0,   -6766,   30824},
    {1, 0, -1, 0,   -5163,   -8379},
    {1, 1, 0, 0,    4987,  -16675},
    {2, -1, 1, 0,    4036,  -12831},
    {2, 0, 2, 0,    3994,  -10445},
    {4, 0, 0, 0,    3861,  -11650},
    {2, 0, -3, 0,    3665,   14403},
    {0, 1, -2, 0,   -2689,   -7003},
    {2, 0, -1, 2,   -2602,       0},
    {2, -1, -2, 0,    2390,   10056},
    {1, 0, 1, 0,   -2348,    6322},
    {2, -2, 0, 0,    2236,   -9884},
    {0, 1, 2, 0,   -2120,    5751},
    {0, 2, 0, 0,   -2069,       0},
    {2, -2, -1, 0,    2048,   -4950},
    {2, 0, 1, -2,   -1773,    4130},
    {2, 0, 0, 2,   -1595,       0},
    {4, -1, -1, 0,    1215,   -3958},
    {0, 0, 2, 2,   -1110,       0},
    {3, 0, -1, 0,    -892,    3258},
    {2, 1, 1, 0,    -810,    2616},
    {4, -1, -2, 0,     759,   -1897},
    {0, 2, -1, 0,    -713,   -2117},
    {2, 2, -1, 0,    -700,    2354},
    {2, 1, -2, 0,     691,       0},
    {2, -1, 0, -2,     596,       0},
    {4, 0, 1, 0,     549,   -1423},
    {0, 0, 4, 0,     537,   -1117},
    {4, -1, 0, 0,     520,   -1571},
    {1, 0, -2, 0,    -487,   -1739},
    {2, 1, 0, -2,    -399,       0},
    {0, 0, 2, -2,    -381,   -4421},
    {1, 1, 1, 0,     351,       0},
    {3, 0, -2, 0,    -340,       0},
    {4, 0, -3, 0,     330,       0},
    {2, -1, 2, 0,     327,       0},
    {0, 2, 1, 0,    -323,    1165},
    {1, 1, -1, 0,     299,       0},
    {2, 0, 3, 0,     294,       0},
    {2, 0, -1, -2,       0,    8752},
};

constexpr LatitudeTerm kTableB[] = {
    {0, 0, 0, 1, 5128122},
    {0, 0, 1, 1,  280602},
    {0, 0, 1, -1,  277693},
    {2, 0, 0, -1,  173237},
    {2, 0, -1, 1,   55413},
    {2, 0, -1, -1,   46271},
    {2, 0, 0, 1,   32573},
    {0, 0, 2, 1,   17198},
    {2, 0, 1, -1,    9266},
    {0, 0, 2, -1,    8822},
    {2, -1, 0, -1,    8216},
    {2, 0, -2, -1,    4324},
    {2, 0, 1, 1,    4200},
    {2, 1, 0, -1,   -3359},
    {2, -1, -1, 1,    2463},
    {2, -1, 0, 1,    2211},
    {2, -1, -1, -1,    2065},
    {0, 1, -1, -1,   -1870},
    {4, 0, -1, -1,    1828},
    {0, 1, 0, 1,   -1794},
    {0, 0, 0, 3,   -1749},
    {0, 1, -1, 1,   -1565},
    {1, 0, 0, 1,   -1491},
    {0, 1, 1, 1,   -1475},
    {0, 1, 1, -1,   -1410},
    {0, 1, 0, -1,   -1344},
    {1, 0, 0, -1,   -1335},
    {0, 0, 3, 1,    1107},
    {4, 0, 0, -1,    1021},
    {4, 0, -1, 1,     833},
    {0, 0, 1, -3,     777},
    {4, 0, -2, 1,     671},
    {2, 0, 0, -3,     607},
    {2, 0, 2, -1,     596},
    {2, -1, 1, -1,     491},
    {2, 0, -2, 1,    -451},
    {0, 0, 3, -1,     439},
    {2, 0, 2, 1,     422},
    {2, 0, -3, -1,     421},
    {2, 1, -1, 1,    -366},
    {2, 1, 0, 1,    -351},
    {4, 0, 0, 1,     331},
    {2, -1, 1, 1,     315},
    {2, -2, 0, -1,     302},
    {0, 0, 1, 3,    -283},
    {2, 1, 1, -1,    -229},
    {1, 1, 0, -1,     223},
    {1, 1, 0, 1,     223},
    {0, 1, -2, -1,    -220},
    {2, 1, -1, -1,    -220},
    {1, 0, 1, 1,    -185},
    {2, -1, -2, -1,     181},
    {0, 1, 2, 1,    -177},
    {4, 0, -2, -1,     176},
    {4, -1, -1, -1,     166},
    {1, 0, 1, -1,    -164},
    {4, 0, 1, -1,     132},
    {1, 0, -1, -1,    -119},
    {4, -1, 0, -1,     115},
    {2, -2, 0, 1,     107},
};

double sinDeg(double deg) { return std::sin(deg * kDegToRad); }
double cosDeg(double deg) { return std::cos(deg * kDegToRad); }

double wrapDeg360(double deg) {
    deg = std::fmod(deg, 360.0);
    return deg < 0.0 ? deg + 360.0 : deg;
}

/** Powers of the eccentricity factor E for a term's multiple of M (0, ±1, ±2). */
double eccentricityFactor(int m, double e) {
    switch (m < 0 ? -m : m) {
        case 1: return e;
        case 2: return e * e;
        default: return 1.0;
    }
}

} // namespace

MoonOfDate MeeusMoon(double jde) {
    const double t = (jde - 2451545.0) / 36525.0;
    const double t2 = t * t, t3 = t2 * t, t4 = t3 * t;

    // Mean elements (degrees): Moon's mean longitude, mean elongation, Sun's and Moon's mean
    // anomalies, and the Moon's argument of latitude.
    const double lPrime = 218.3164477 + 481267.88123421 * t - 0.0015786 * t2 + t3 / 538841.0 - t4 / 65194000.0;
    const double d = 297.8501921 + 445267.1114034 * t - 0.0018819 * t2 + t3 / 545868.0 - t4 / 113065000.0;
    const double m = 357.5291092 + 35999.0502909 * t - 0.0001536 * t2 + t3 / 24490000.0;
    const double mp = 134.9633964 + 477198.8675055 * t + 0.0087414 * t2 + t3 / 69699.0 - t4 / 14712000.0;
    const double f = 93.2720950 + 483202.0175233 * t - 0.0036539 * t2 - t3 / 3526000.0 + t4 / 863310000.0;

    // Venus, Jupiter and the Earth's flattening.
    const double a1 = 119.75 + 131.849 * t;
    const double a2 = 53.09 + 479264.290 * t;
    const double a3 = 313.45 + 481266.484 * t;
    const double e = 1.0 - 0.002516 * t - 0.0000074 * t2;

    double sumL = 3958.0 * sinDeg(a1) + 1962.0 * sinDeg(lPrime - f) + 318.0 * sinDeg(a2);
    double sumR = 0.0;
    double sumB = -2235.0 * sinDeg(lPrime) + 382.0 * sinDeg(a3) + 175.0 * sinDeg(a1 - f) +
                  175.0 * sinDeg(a1 + f) + 127.0 * sinDeg(lPrime - mp) - 115.0 * sinDeg(lPrime + mp);

    for (const LongitudeDistanceTerm& term : kTableA) {
        const double argument = term.d * d + term.m * m + term.mp * mp + term.f * f;
        const double factor = eccentricityFactor(term.m, e);
        sumL += term.sumL * factor * sinDeg(argument);
        sumR += term.sumR * factor * cosDeg(argument);
    }
    for (const LatitudeTerm& term : kTableB) {
        const double argument = term.d * d + term.m * m + term.mp * mp + term.f * f;
        sumB += term.sumB * eccentricityFactor(term.m, e) * sinDeg(argument);
    }

    MoonOfDate moon;
    moon.lonDeg = wrapDeg360(lPrime + sumL * 1.0e-6);
    moon.latDeg = sumB * 1.0e-6;
    moon.distanceKm = 385000.56 + sumR * 1.0e-3;
    return moon;
}

void EclipticOfDateToJ2000(double jde, double lonDeg, double latDeg, double& lonJ2000Deg,
                           double& latJ2000Deg) {
    // Meeus 21.5: precession of ecliptical coordinates from the epoch of date (T) to J2000
    // (t = -T), in arcseconds for the polynomial coefficients.
    const double bigT = (jde - 2451545.0) / 36525.0;
    const double t = -bigT;
    const double eta = ((47.0029 - 0.06603 * bigT + 0.000598 * bigT * bigT) * t +
                        (-0.03302 + 0.000598 * bigT) * t * t + 0.000060 * t * t * t) * kArcsecToRad;
    const double pi = (174.876384 * 3600.0 + 3289.4789 * bigT + 0.60622 * bigT * bigT -
                       (869.8089 + 0.50491 * bigT) * t + 0.03536 * t * t) * kArcsecToRad;
    const double p = ((5029.0966 + 2.22226 * bigT - 0.000042 * bigT * bigT) * t +
                      (1.11113 - 0.000042 * bigT) * t * t - 0.000006 * t * t * t) * kArcsecToRad;

    const double lon = lonDeg * kDegToRad;
    const double lat = latDeg * kDegToRad;
    const double a = std::cos(eta) * std::cos(lat) * std::sin(pi - lon) - std::sin(eta) * std::sin(lat);
    const double b = std::cos(lat) * std::cos(pi - lon);
    const double c = std::cos(eta) * std::sin(lat) + std::sin(eta) * std::cos(lat) * std::sin(pi - lon);

    lonJ2000Deg = wrapDeg360((p + pi - std::atan2(a, b)) * kRadToDeg);
    latJ2000Deg = std::asin(c < -1.0 ? -1.0 : (c > 1.0 ? 1.0 : c)) * kRadToDeg;
}

void MoonGeocentricJ2000Au(double jde, double outXyzAu[3]) {
    const MoonOfDate moon = MeeusMoon(jde);
    double lon = 0.0, lat = 0.0;
    EclipticOfDateToJ2000(jde, moon.lonDeg, moon.latDeg, lon, lat);
    const double r = moon.distanceKm / kKmPerAu;
    outXyzAu[0] = r * cosDeg(lat) * cosDeg(lon);
    outXyzAu[1] = r * cosDeg(lat) * sinDeg(lon);
    outXyzAu[2] = r * sinDeg(lat);
}

} // namespace LunarTheory
