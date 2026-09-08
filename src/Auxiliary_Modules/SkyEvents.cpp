#include "SkyEvents.h"

#include "Ephemeris.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>

namespace SkyEvents {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kDegToRad = kPi / 180.0;

constexpr int kEarth = 3;

// Pairs searched by NextInnerPlanetConjunction: Mercury, Venus, Mars.
// Earth is excluded — a body cannot be in conjunction with the observer.
constexpr int kInnerPlanets[] = {1, 2, 4};

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

Vec3 heliocentric(int bodyIndex, double jd) {
    const Ephemeris::HelioLB p = Ephemeris::Position(bodyIndex, jd);
    const double cosLat = std::cos(p.latRad);
    return {p.rAu * std::cos(p.lonRad) * cosLat,
            p.rAu * std::sin(p.lonRad) * cosLat,
            p.rAu * std::sin(p.latRad)};
}

Vec3 geocentric(int bodyIndex, double jd) {
    const Vec3 body = heliocentric(bodyIndex, jd);
    const Vec3 earth = heliocentric(kEarth, jd);
    return {body.x - earth.x, body.y - earth.y, body.z - earth.z};
}

/** Wrap to (-180, 180] so a sign change means the longitudes actually crossed. */
double wrapDeg180(double deg) {
    deg = std::fmod(deg + 180.0, 360.0);
    if (deg < 0.0) {
        deg += 360.0;
    }
    return deg - 180.0;
}

/** Signed geocentric longitude difference A-B in (-180, 180]. */
double lonDeltaDeg(int bodyA, int bodyB, double jd) {
    double lonA = 0.0, lonB = 0.0, unusedLat = 0.0;
    GeocentricLonLatDeg(bodyA, jd, lonA, unusedLat);
    GeocentricLonLatDeg(bodyB, jd, lonB, unusedLat);
    return wrapDeg180(lonA - lonB);
}

/** Bisect a bracketed longitude crossing; the interval is short enough for linear behaviour. */
double refineCrossing(int bodyA, int bodyB, double jdLo, double jdHi) {
    double lo = jdLo;
    double hi = jdHi;
    double deltaLo = lonDeltaDeg(bodyA, bodyB, lo);
    for (int i = 0; i < 48 && (hi - lo) > 1.0e-5; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double deltaMid = lonDeltaDeg(bodyA, bodyB, mid);
        if ((deltaLo < 0.0) == (deltaMid < 0.0)) {
            lo = mid;
            deltaLo = deltaMid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

} // namespace

void GeocentricLonLatDeg(int bodyIndex, double julianDate, double& lonDeg, double& latDeg) {
    const Vec3 v = geocentric(bodyIndex, julianDate);
    const double r = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (r < 1.0e-12) {
        lonDeg = 0.0;
        latDeg = 0.0;
        return;
    }
    double lon = std::atan2(v.y, v.x) * kRadToDeg;
    if (lon < 0.0) {
        lon += 360.0;
    }
    lonDeg = lon;
    latDeg = std::asin(std::clamp(v.z / r, -1.0, 1.0)) * kRadToDeg;
}

double ApparentSeparationDeg(int bodyA, int bodyB, double julianDate) {
    double lonA = 0.0, latA = 0.0, lonB = 0.0, latB = 0.0;
    GeocentricLonLatDeg(bodyA, julianDate, lonA, latA);
    GeocentricLonLatDeg(bodyB, julianDate, lonB, latB);

    const double dLon = (lonA - lonB) * kDegToRad;
    const double a = latA * kDegToRad;
    const double b = latB * kDegToRad;
    const double cosSep = std::sin(a) * std::sin(b) + std::cos(a) * std::cos(b) * std::cos(dLon);
    return std::acos(std::clamp(cosSep, -1.0, 1.0)) * kRadToDeg;
}

Conjunction NextInnerPlanetConjunction(double fromJd, double searchDays) {
    // One-day steps: the fastest pair (Mercury-Venus) closes a few degrees of geocentric
    // longitude per day, so a crossing cannot hide between samples.
    constexpr double kStepDays = 1.0;
    Conjunction best;

    for (std::size_t i = 0; i < std::size(kInnerPlanets); ++i) {
        for (std::size_t j = i + 1; j < std::size(kInnerPlanets); ++j) {
            const int bodyA = kInnerPlanets[i];
            const int bodyB = kInnerPlanets[j];

            double prevJd = fromJd;
            double prevDelta = lonDeltaDeg(bodyA, bodyB, prevJd);
            const double endJd = fromJd + searchDays;

            for (double jd = fromJd + kStepDays; jd <= endJd; jd += kStepDays) {
                const double delta = lonDeltaDeg(bodyA, bodyB, jd);
                // A sign change with both samples near zero is a crossing; a sign change with
                // both near +/-180 is only the wrap of the branch cut.
                const bool crossed = (prevDelta < 0.0) != (delta < 0.0);
                if (crossed && std::fabs(prevDelta) < 90.0 && std::fabs(delta) < 90.0) {
                    const double jdCross = refineCrossing(bodyA, bodyB, prevJd, jd);
                    if (!best.valid || jdCross < best.julianDate) {
                        best.valid = true;
                        best.bodyA = bodyA;
                        best.bodyB = bodyB;
                        best.julianDate = jdCross;
                        best.separationDeg = ApparentSeparationDeg(bodyA, bodyB, jdCross);
                    }
                    break; // earliest crossing for this pair; other pairs may still beat it
                }
                prevJd = jd;
                prevDelta = delta;
            }
        }
    }

    return best;
}

const char* BodyName(int bodyIndex) {
    switch (bodyIndex) {
        case 0: return "Sun";
        case 1: return "Mercury";
        case 2: return "Venus";
        case 3: return "Earth";
        case 4: return "Mars";
        case 5: return "Jupiter";
        case 6: return "Saturn";
        case 7: return "Uranus";
        case 8: return "Neptune";
        case 9: return "Pluto";
        case 10: return "Ceres";
        case 11: return "Vesta";
        default: return "";
    }
}

std::string Format(const Conjunction& conjunction) {
    if (!conjunction.valid) {
        return {};
    }

    int year = 0, month = 0, day = 0;
    Ephemeris::YmdFromJulianDate(conjunction.julianDate, year, month, day);

    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "Next conjunction: %s-%s, %04d-%02d-%02d (%.1f deg apart)",
                  BodyName(conjunction.bodyA), BodyName(conjunction.bodyB), year, month, day,
                  conjunction.separationDeg);
    return buffer;
}

std::string FormatNextInnerPlanetConjunction(double fromJd, double searchDays) {
    return Format(NextInnerPlanetConjunction(fromJd, searchDays));
}

} // namespace SkyEvents
