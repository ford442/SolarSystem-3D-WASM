#include "SkyEvents.h"

#include "Ephemeris.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
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

constexpr double kKmPerAu = 149597870.7;

// Mean (volumetric) radii in km, IAU 2015 / NASA fact sheets. Only the bodies the event
// searches need — everything else would be dead weight in the wasm.
constexpr double kSunRadiusKm = 695700.0;
constexpr double kMercuryRadiusKm = 2439.7;
constexpr double kVenusRadiusKm = 6051.8;
constexpr double kEarthRadiusKm = 6371.0;
constexpr double kJupiterRadiusKm = 69911.0;
constexpr double kMoonRadiusKm = 1737.4;
constexpr double kIoRadiusKm = 1821.6;

constexpr int kSun = 0;
constexpr int kMercury = 1;
constexpr int kVenus = 2;
constexpr int kJupiter = 5;

double satelliteRadiusKm(int satelliteIndex) {
    switch (satelliteIndex) {
        case 12: return kMoonRadiusKm;
        case 15: return kIoRadiusKm;
        case 16: return 1560.8;  // Europa
        case 17: return 2634.1;  // Ganymede
        case 18: return 2410.3;  // Callisto
        case 24: return 2574.7;  // Titan
        case 31: return 1353.4;  // Triton
        default: return 0.0;
    }
}

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& v, double k) { return {v.x * k, v.y * k, v.z * k}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(const Vec3& v) { return std::sqrt(dot(v, v)); }

Vec3 normalized(const Vec3& v) {
    const double len = length(v);
    return len < 1e-12 ? Vec3{} : scale(v, 1.0 / len);
}

/** Angular radius in degrees of a sphere of radius `radiusKm` seen from `distanceKm`. */
double angularRadiusDeg(double radiusKm, double distanceKm) {
    if (distanceKm <= radiusKm) {
        return 90.0;
    }
    return std::asin(radiusKm / distanceKm) * kRadToDeg;
}

/** Angle in degrees between two directions. */
double angleBetweenDeg(const Vec3& a, const Vec3& b) {
    const Vec3 ua = normalized(a);
    const Vec3 ub = normalized(b);
    return std::acos(std::clamp(dot(ua, ub), -1.0, 1.0)) * kRadToDeg;
}

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

/** Parent-relative satellite position in km, or {0,0,0} when the backend has no solution. */
Vec3 satelliteParentRelativeKm(int satelliteIndex, double jd) {
    double au[3] = {0.0, 0.0, 0.0};
    if (!Ephemeris::SatellitePosition(satelliteIndex, jd, au)) {
        return {};
    }
    return {au[0] * kKmPerAu, au[1] * kKmPerAu, au[2] * kKmPerAu};
}

/** Geocentric position in km (planets via heliocentric difference, the Moon directly). */
Vec3 geocentricKm(int bodyIndex, double jd) {
    if (bodyIndex == kMoon) {
        return satelliteParentRelativeKm(kMoon, jd);
    }
    const Vec3 v = geocentric(bodyIndex, jd);
    return scale(v, kKmPerAu);
}

/**
 * Earliest local minimum of `metric` strictly after fromJd whose refined value is negative.
 *
 * `metric` is a miss distance with the event threshold already subtracted, so negative
 * means "happening". `stepDays` must be short enough that the metric cannot dip below zero
 * and back between two samples — for each caller that is a fraction of the alignment's own
 * recurrence period, not of the event's duration, because the metric has exactly one
 * minimum per recurrence.
 */
bool firstNegativeMinimum(const std::function<double(double)>& metric, double fromJd,
                          double searchDays, double stepDays, double& outJd,
                          double& outValue) {
    const double endJd = fromJd + searchDays;
    double prevJd = fromJd;
    double prev = metric(prevJd);
    double curJd = fromJd + stepDays;
    if (curJd >= endJd) {
        return false;
    }
    double cur = metric(curJd);

    for (double nextJd = curJd + stepDays; nextJd <= endJd; nextJd += stepDays) {
        const double next = metric(nextJd);
        if (cur <= prev && cur <= next) {
            // Bracketed minimum: golden-section on [prevJd, nextJd]. The metric is smooth
            // here, so a few dozen iterations land well inside a second of time.
            double lo = prevJd;
            double hi = nextJd;
            for (int i = 0; i < 80 && (hi - lo) > 1.0e-7; ++i) {
                const double m1 = lo + (hi - lo) / 3.0;
                const double m2 = hi - (hi - lo) / 3.0;
                if (metric(m1) < metric(m2)) {
                    hi = m2;
                } else {
                    lo = m1;
                }
            }
            const double jd = 0.5 * (lo + hi);
            const double value = metric(jd);
            if (value < 0.0 && jd > fromJd) {
                outJd = jd;
                outValue = value;
                return true;
            }
        }
        prevJd = curJd;
        prev = cur;
        curJd = nextJd;
        cur = next;
    }

    return false;
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

SkyEvent NextConjunctionEvent(double fromJd, double searchDays) {
    const Conjunction next = NextInnerPlanetConjunction(fromJd, searchDays);
    SkyEvent event;
    if (!next.valid) {
        return event;
    }
    event.valid = true;
    event.kind = EventKind::Conjunction;
    event.bodyA = next.bodyA;
    event.bodyB = next.bodyB;
    event.julianDate = next.julianDate;
    event.missDeg = next.separationDeg;
    // A conjunction is an alignment in longitude, not an overlap of discs; there is no
    // physical limit to compare against, so report the pair's own separation as the limit.
    event.limitDeg = next.separationDeg;
    return event;
}

SkyEvent NextSolarEclipse(double fromJd, double searchDays) {
    // Metric: how far the Moon's disc misses the Sun's at greatest geocentric alignment.
    // The limit carries the Moon's horizontal parallax as well as the two angular radii,
    // because an observer anywhere on Earth's surface sits up to one Earth radius off the
    // geocentre — that is what makes this "an eclipse somewhere on Earth" rather than "an
    // eclipse at the geocentre". One minimum per synodic month (~29.5 d), so 1-day steps
    // cannot skip it.
    const auto metric = [](double jd) {
        const Vec3 moon = geocentricKm(kMoon, jd);
        const Vec3 sun = geocentricKm(kSun, jd);
        const double moonDist = length(moon);
        const double sunDist = length(sun);
        if (moonDist < 1.0 || sunDist < 1.0) {
            return 1.0e9;
        }
        const double limit = angularRadiusDeg(kSunRadiusKm, sunDist) +
                             angularRadiusDeg(kMoonRadiusKm, moonDist) +
                             angularRadiusDeg(kEarthRadiusKm, moonDist);
        return angleBetweenDeg(moon, sun) - limit;
    };

    SkyEvent event;
    double jd = 0.0, value = 0.0;
    if (!firstNegativeMinimum(metric, fromJd, searchDays, 1.0, jd, value)) {
        return event;
    }

    const Vec3 moon = geocentricKm(kMoon, jd);
    const Vec3 sun = geocentricKm(kSun, jd);
    event.valid = true;
    event.kind = EventKind::SolarEclipse;
    event.bodyA = kMoon;
    event.bodyB = kSun;
    event.julianDate = jd;
    event.missDeg = angleBetweenDeg(moon, sun);
    event.limitDeg = event.missDeg - value;
    return event;
}

SkyEvent NextLunarEclipse(double fromJd, double searchDays) {
    // Metric: the Moon's distance from the axis of Earth's umbral cone, minus the cone's
    // own radius at the Moon's distance plus the Moon's radius. All in km, then reported
    // back as the geocentric angle so the field means the same thing across event kinds.
    const auto missKm = [](double jd, double& outUmbraPlusMoonKm) {
        const Vec3 moon = geocentricKm(kMoon, jd);
        const Vec3 sun = geocentricKm(kSun, jd);
        const double sunDist = length(sun);
        if (length(moon) < 1.0 || sunDist < 1.0) {
            outUmbraPlusMoonKm = 0.0;
            return 1.0e12;
        }
        const Vec3 axis = normalized(scale(sun, -1.0)); // antisolar direction
        const double along = dot(moon, axis);
        const Vec3 perp = sub(moon, scale(axis, along));
        // Umbra cross-section radius at `along`: the shadow cone narrows by the Sun's
        // angular size. Negative past the cone's tip, which never happens for the Moon.
        const double umbraKm = kEarthRadiusKm - along * (kSunRadiusKm - kEarthRadiusKm) / sunDist;
        outUmbraPlusMoonKm = umbraKm + kMoonRadiusKm;
        return along > 0.0 ? length(perp) : 1.0e12;
    };

    const auto metric = [&missKm](double jd) {
        double limitKm = 0.0;
        return missKm(jd, limitKm) - limitKm;
    };

    SkyEvent event;
    double jd = 0.0, value = 0.0;
    if (!firstNegativeMinimum(metric, fromJd, searchDays, 1.0, jd, value)) {
        return event;
    }

    double limitKm = 0.0;
    const double missKmAtMin = missKm(jd, limitKm);
    const double moonDist = length(geocentricKm(kMoon, jd));
    event.valid = true;
    event.kind = EventKind::LunarEclipse;
    event.bodyA = kMoon;
    event.bodyB = kEarth;
    event.julianDate = jd;
    event.missDeg = angularRadiusDeg(missKmAtMin, moonDist);
    event.limitDeg = angularRadiusDeg(limitKm, moonDist);
    return event;
}

SkyEvent NextInferiorPlanetTransit(double fromJd, double searchDays) {
    SkyEvent best;

    for (const int planet : {kMercury, kVenus}) {
        const double planetRadiusKm = planet == kMercury ? kMercuryRadiusKm : kVenusRadiusKm;
        const auto metric = [planet, planetRadiusKm](double jd) {
            const Vec3 body = geocentricKm(planet, jd);
            const Vec3 sun = geocentricKm(kSun, jd);
            const double bodyDist = length(body);
            const double sunDist = length(sun);
            if (bodyDist < 1.0 || sunDist < 1.0) {
                return 1.0e9;
            }
            // Only an inferior conjunction can be a transit: the planet must be between us
            // and the Sun, not behind it.
            if (bodyDist >= sunDist) {
                return 1.0e9;
            }
            const double limit = angularRadiusDeg(kSunRadiusKm, sunDist) -
                                 angularRadiusDeg(planetRadiusKm, bodyDist);
            return angleBetweenDeg(body, sun) - limit;
        };

        // Mercury's synodic period is ~116 d and Venus's ~584 d; 2-day steps resolve the
        // single approach minimum in each.
        double jd = 0.0, value = 0.0;
        if (!firstNegativeMinimum(metric, fromJd, searchDays, 2.0, jd, value)) {
            continue;
        }
        if (best.valid && jd >= best.julianDate) {
            continue;
        }

        const Vec3 body = geocentricKm(planet, jd);
        const Vec3 sun = geocentricKm(kSun, jd);
        best.valid = true;
        best.kind = EventKind::Transit;
        best.bodyA = planet;
        best.bodyB = kSun;
        best.julianDate = jd;
        best.missDeg = angleBetweenDeg(body, sun);
        best.limitDeg = best.missDeg - value;
    }

    return best;
}

SkyEvent NextGalileanShadowTransit(double fromJd, double searchDays, int satelliteIndex) {
    const double moonRadiusKm = satelliteRadiusKm(satelliteIndex);
    SkyEvent event;
    if (moonRadiusKm <= 0.0) {
        return event;
    }

    // Metric: how far the moon misses the Sun-Jupiter axis on the sunward side, minus the
    // radius that would put its shadow on the disc. Jovicentric km throughout.
    const auto missKm = [satelliteIndex, moonRadiusKm](double jd, double& outLimitKm) {
        const Vec3 moon = satelliteParentRelativeKm(satelliteIndex, jd);
        if (length(moon) < 1.0) {
            outLimitKm = 0.0;
            return 1.0e12;
        }
        const Vec3 jupiter = scale(heliocentric(kJupiter, jd), kKmPerAu);
        const Vec3 toSun = normalized(scale(jupiter, -1.0));
        const double along = dot(moon, toSun);
        outLimitKm = kJupiterRadiusKm + moonRadiusKm;
        if (along <= 0.0) {
            return 1.0e12; // moon is behind Jupiter, casting its shadow into space
        }
        return length(sub(moon, scale(toSun, along)));
    };

    const auto metric = [&missKm](double jd) {
        double limitKm = 0.0;
        return missKm(jd, limitKm) - limitKm;
    };

    // Io's period is 1.77 d and Callisto's 16.7 d; 0.05-day steps resolve the fastest.
    double jd = 0.0, value = 0.0;
    if (!firstNegativeMinimum(metric, fromJd, searchDays, 0.05, jd, value)) {
        return event;
    }

    double limitKm = 0.0;
    const double missKmAtMin = missKm(jd, limitKm);
    const double jupiterDistKm = length(scale(heliocentric(kJupiter, jd), kKmPerAu));
    event.valid = true;
    event.kind = EventKind::ShadowTransit;
    event.bodyA = satelliteIndex;
    event.bodyB = kJupiter;
    event.julianDate = jd;
    event.missDeg = angularRadiusDeg(missKmAtMin, jupiterDistKm);
    event.limitDeg = angularRadiusDeg(limitKm, jupiterDistKm);
    return event;
}

SkyEvent NextEvent(double fromJd, double searchDays) {
    const SkyEvent candidates[] = {
        NextConjunctionEvent(fromJd, searchDays),
        NextSolarEclipse(fromJd, searchDays),
        NextLunarEclipse(fromJd, searchDays),
        NextInferiorPlanetTransit(fromJd, searchDays),
        NextGalileanShadowTransit(fromJd, std::min(searchDays, 30.0)),
    };

    SkyEvent best;
    for (const SkyEvent& candidate : candidates) {
        if (candidate.valid && (!best.valid || candidate.julianDate < best.julianDate)) {
            best = candidate;
        }
    }
    return best;
}

const char* EventKindSlug(EventKind kind) {
    switch (kind) {
        case EventKind::Conjunction: return "conjunction";
        case EventKind::SolarEclipse: return "solarEclipse";
        case EventKind::LunarEclipse: return "lunarEclipse";
        case EventKind::Transit: return "transit";
        case EventKind::ShadowTransit: return "shadowTransit";
        case EventKind::None: break;
    }
    return "none";
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
        // Only the satellites the event searches can name. Everything else stays with the
        // catalog's own display names, which are wide strings.
        case 12: return "Moon";
        case 15: return "Io";
        case 16: return "Europa";
        case 17: return "Ganymede";
        case 18: return "Callisto";
        case 24: return "Titan";
        case 31: return "Triton";
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

std::string FormatEvent(const SkyEvent& event) {
    if (!event.valid) {
        return {};
    }

    int year = 0, month = 0, day = 0;
    Ephemeris::YmdFromJulianDate(event.julianDate, year, month, day);

    char buffer[160];
    switch (event.kind) {
        case EventKind::Conjunction:
            std::snprintf(buffer, sizeof(buffer),
                          "Next conjunction: %s-%s, %04d-%02d-%02d (%.1f deg apart)",
                          BodyName(event.bodyA), BodyName(event.bodyB), year, month, day,
                          event.missDeg);
            break;
        case EventKind::SolarEclipse:
            std::snprintf(buffer, sizeof(buffer), "Next solar eclipse: %04d-%02d-%02d",
                          year, month, day);
            break;
        case EventKind::LunarEclipse:
            std::snprintf(buffer, sizeof(buffer), "Next lunar eclipse: %04d-%02d-%02d",
                          year, month, day);
            break;
        case EventKind::Transit:
            std::snprintf(buffer, sizeof(buffer), "Next transit: %s across the Sun, %04d-%02d-%02d",
                          BodyName(event.bodyA), year, month, day);
            break;
        case EventKind::ShadowTransit:
            std::snprintf(buffer, sizeof(buffer),
                          "Next shadow transit: %s on Jupiter, %04d-%02d-%02d",
                          BodyName(event.bodyA), year, month, day);
            break;
        case EventKind::None:
            return {};
    }
    return buffer;
}

std::string FormatNextInnerPlanetConjunction(double fromJd, double searchDays) {
    return Format(NextInnerPlanetConjunction(fromJd, searchDays));
}

} // namespace SkyEvents
