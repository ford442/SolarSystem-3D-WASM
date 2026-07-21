#include "Ephemeris.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace Ephemeris {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kDaysPerCentury = 36525.0;

struct Elements {
    double a0, aDot;   // AU, AU/Cy
    double e0, eDot;   // eccentricity
    double I0, IDot;   // deg, deg/Cy
    double L0, LDot;   // deg, deg/Cy
    double wBar0, wBarDot; // longitude of perihelion deg, deg/Cy
    double Om0, OmDot; // longitude of ascending node deg, deg/Cy
};

// JPL SSD Approximate Positions of the Planets — Table 1 (1800–2050 AD).
// Order: Mercury, Venus, EM Bary (Earth), Mars, Jupiter, Saturn, Uranus, Neptune.
constexpr Elements kStandish[] = {
    // Mercury
    {0.38709927, 0.00000037, 0.20563593, 0.00001906, 7.00497902, -0.00594749,
     252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081},
    // Venus
    {0.72333566, 0.00000390, 0.00677672, -0.00004107, 3.39467605, -0.00078890,
     181.97909950, 58517.81538729, 131.60246718, 0.00268329, 76.67984255, -0.27769418},
    // Earth (EM Bary)
    {1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668,
     100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0},
    // Mars
    {1.52371034, 0.00001847, 0.09339410, 0.00007882, 1.84969142, -0.00813131,
     -4.55343205, 19140.30268499, -23.94362959, 0.44441088, 49.55953891, -0.29257343},
    // Jupiter
    {5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714,
     34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106},
    // Saturn
    {9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609,
     49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794},
    // Uranus
    {19.18916464, -0.00196176, 0.04725744, -0.00004397, 0.77263783, -0.00242939,
     313.23810451, 428.48202785, 170.95427630, 0.40805281, 74.01692503, 0.04240589},
    // Neptune
    {30.06992276, 0.00026291, 0.00859048, 0.00005105, 1.77004347, 0.00035372,
     -55.12002969, 218.45945325, 44.96476227, -0.32241464, 131.78422574, -0.00508664},
};

// Pluto Keplerian elements at J2000.0 (approx.); mean motion from sidereal period.
constexpr double kPlutoA = 39.482;
constexpr double kPlutoE = 0.2488;
constexpr double kPlutoIDeg = 17.16;
constexpr double kPlutoOmDeg = 110.299;
constexpr double kPlutoWDeg = 113.834;
constexpr double kPlutoM0Deg = 14.53;
constexpr double kPlutoPeriodDays = 90465.0; // ~248.0 yr

double wrapDeg180(double deg) {
    deg = std::fmod(deg + 180.0, 360.0);
    if (deg < 0.0) {
        deg += 360.0;
    }
    return deg - 180.0;
}

double wrapRadTwoPi(double rad) {
    rad = std::fmod(rad, kTwoPi);
    if (rad < 0.0) {
        rad += kTwoPi;
    }
    return rad;
}

/** Solve Kepler's equation M = E - e sin E (radians). */
double solveKepler(double MRad, double e) {
    MRad = wrapRadTwoPi(MRad);
    if (MRad > kPi) {
        MRad -= kTwoPi;
    }

    double E = MRad + e * std::sin(MRad); // good starter for e < ~0.3
    for (int i = 0; i < 12; ++i) {
        const double dM = MRad - (E - e * std::sin(E));
        const double dE = dM / (1.0 - e * std::cos(E));
        E += dE;
        if (std::fabs(dE) < 1e-10) {
            break;
        }
    }
    return E;
}

HelioLB fromOrbitalPlane(double a, double e, double IDeg, double OmDeg, double wDeg, double MDeg) {
    const double I = IDeg * kDegToRad;
    const double Om = OmDeg * kDegToRad;
    const double w = wDeg * kDegToRad;
    const double E = solveKepler(MDeg * kDegToRad, e);

    const double xp = a * (std::cos(E) - e);
    const double yp = a * std::sqrt(std::max(0.0, 1.0 - e * e)) * std::sin(E);

    const double cosW = std::cos(w);
    const double sinW = std::sin(w);
    const double cosOm = std::cos(Om);
    const double sinOm = std::sin(Om);
    const double cosI = std::cos(I);
    const double sinI = std::sin(I);

    // J2000 ecliptic: x toward equinox, z toward north ecliptic pole.
    const double xe =
        (cosW * cosOm - sinW * sinOm * cosI) * xp + (-sinW * cosOm - cosW * sinOm * cosI) * yp;
    const double ye =
        (cosW * sinOm + sinW * cosOm * cosI) * xp + (-sinW * sinOm + cosW * cosOm * cosI) * yp;
    const double ze = (sinW * sinI) * xp + (cosW * sinI) * yp;

    const double r = std::sqrt(xe * xe + ye * ye + ze * ze);
    HelioLB out;
    out.rAu = r;
    if (r < 1e-12) {
        return out;
    }
    out.lonRad = wrapRadTwoPi(std::atan2(ye, xe));
    out.latRad = std::asin(std::clamp(ze / r, -1.0, 1.0));
    return out;
}

HelioLB standishPosition(const Elements& el, double jd) {
    const double T = (jd - kJ2000) / kDaysPerCentury;

    const double a = el.a0 + el.aDot * T;
    const double e = el.e0 + el.eDot * T;
    const double I = el.I0 + el.IDot * T;
    const double L = el.L0 + el.LDot * T;
    const double wBar = el.wBar0 + el.wBarDot * T;
    const double Om = el.Om0 + el.OmDot * T;

    const double w = wBar - Om;
    const double M = wrapDeg180(L - wBar);

    return fromOrbitalPlane(a, e, I, Om, w, M);
}

HelioLB plutoPosition(double jd) {
    const double days = jd - kJ2000;
    const double nDegPerDay = 360.0 / kPlutoPeriodDays;
    const double M = wrapDeg180(kPlutoM0Deg + nDegPerDay * days);
    return fromOrbitalPlane(kPlutoA, kPlutoE, kPlutoIDeg, kPlutoOmDeg, kPlutoWDeg, M);
}

} // namespace

double JulianDateFromYmd(int year, int month, int day) {
    // Meeus / Fliegel–Van Flandern civil calendar → Julian Date at 0h UT.
    const int a = (14 - month) / 12;
    const int y = year + 4800 - a;
    const int m = month + 12 * a - 3;
    const double jdn = day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045.0;
    return jdn - 0.5;
}

void YmdFromJulianDate(double jd, int& year, int& month, int& day) {
    // Convert to chronological Julian day number at noon-centered integer.
    const long long J = static_cast<long long>(std::floor(jd + 0.5));
    const long long f = J + 1401 + (((4 * J + 274277) / 146097) * 3) / 4 - 38;
    const long long e = 4 * f + 3;
    const long long g = (e % 1461) / 4;
    const long long h = 5 * g + 2;
    day = static_cast<int>((h % 153) / 5 + 1);
    month = static_cast<int>((h / 153 + 2) % 12 + 1);
    year = static_cast<int>(e / 1461 - 4716 + (12 + 2 - month) / 12);
}

double JulianDateNowUtc() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t t = clock::to_time_t(now);
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    const double dayFraction =
        (tmUtc.tm_hour + (tmUtc.tm_min + tmUtc.tm_sec / 60.0) / 60.0) / 24.0;
    return JulianDateFromYmd(tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday) + dayFraction;
}

HelioLB Position(int bodyIndex, double julianDate) {
    // 0=Sun, 1=Mercury … 8=Neptune, 9=Pluto
    if (bodyIndex <= 0 || bodyIndex > 9) {
        return {};
    }
    if (bodyIndex == 9) {
        return plutoPosition(julianDate);
    }
    return standishPosition(kStandish[bodyIndex - 1], julianDate);
}

} // namespace Ephemeris
