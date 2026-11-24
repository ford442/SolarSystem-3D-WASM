#include "astro/flight/Propagator.h"
#include "astro/flight/FlightConstants.h"
#include "astro/flight/StateVector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>

namespace Flight {

namespace {

// Stumpff functions C(z) and S(z), with series near z = 0 where the closed forms cancel.
double StumpffC(double z) {
    if (z > 1e-6) {
        return (1.0 - std::cos(std::sqrt(z))) / z;
    }
    if (z < -1e-6) {
        return (std::cosh(std::sqrt(-z)) - 1.0) / -z;
    }
    return 0.5 - z / 24.0 + z * z / 720.0;
}

double StumpffS(double z) {
    if (z > 1e-6) {
        const double sz = std::sqrt(z);
        return (sz - std::sin(sz)) / (sz * sz * sz);
    }
    if (z < -1e-6) {
        const double sz = std::sqrt(-z);
        return (std::sinh(sz) - sz) / (sz * sz * sz);
    }
    return 1.0 / 6.0 - z / 120.0 + z * z / 5040.0;
}

// The planets the n-body acceleration pulls with (planet GM, not system GM, except that the
// Standish "Earth" is the Earth–Moon barycentre: the Moon is then a separate term about Earth).
constexpr std::array<int, 9> kPerturbers = {1, 2, 3, 4, 5, 6, 7, 8, 9};
constexpr double kMuMoon = 4902.800118;

} // namespace

StateVector KeplerPropagate(const StateVector& state, double mu, double dtSeconds) {
    const double r0 = glm::length(state.r);
    const double v0 = glm::length(state.v);
    if (r0 <= 0.0 || dtSeconds == 0.0) {
        return state;
    }
    const double sqrtMu = std::sqrt(mu);
    const double vr0 = glm::dot(state.r, state.v) / r0;
    const double alpha = 2.0 / r0 - v0 * v0 / mu; // 1/a

    // A long elliptic step is reduced by whole periods first, so Newton starts close.
    if (alpha > 1e-15) {
        const double period = 2.0 * kPi / (std::sqrt(mu) * std::pow(alpha, 1.5));
        dtSeconds = std::fmod(dtSeconds, period);
    }

    double chi = 0.0;
    if (alpha > 1e-15) {
        chi = sqrtMu * dtSeconds * alpha;
    } else if (alpha < -1e-15) {
        const double a = 1.0 / alpha;
        const double sign = dtSeconds >= 0.0 ? 1.0 : -1.0;
        const double arg = (-2.0 * mu * alpha * dtSeconds) /
                           (glm::dot(state.r, state.v) + sign * std::sqrt(-mu * a) * (1.0 - r0 * alpha));
        chi = arg > 0.0 ? sign * std::sqrt(-a) * std::log(arg) : sqrtMu * dtSeconds / r0;
    } else {
        chi = sqrtMu * dtSeconds / r0;
    }

    for (int i = 0; i < 100; ++i) {
        const double z = alpha * chi * chi;
        const double c = StumpffC(z);
        const double s = StumpffS(z);
        const double f = r0 * vr0 / sqrtMu * chi * chi * c + (1.0 - alpha * r0) * chi * chi * chi * s + r0 * chi -
                         sqrtMu * dtSeconds;
        const double df = r0 * vr0 / sqrtMu * chi * (1.0 - z * s) + (1.0 - alpha * r0) * chi * chi * c + r0;
        const double step = f / df;
        chi -= step;
        if (std::fabs(step) < 1e-12 * std::max(1.0, std::fabs(chi))) {
            break;
        }
    }

    const double z = alpha * chi * chi;
    const double c = StumpffC(z);
    const double s = StumpffS(z);
    const double f = 1.0 - chi * chi / r0 * c;
    const double g = dtSeconds - chi * chi * chi / sqrtMu * s;
    StateVector out;
    out.r = f * state.r + g * state.v;
    const double r = glm::length(out.r);
    const double fdot = sqrtMu / (r * r0) * (alpha * chi * chi * chi * s - chi);
    const double gdot = 1.0 - chi * chi / r * c;
    out.v = fdot * state.r + gdot * state.v;
    return out;
}

glm::dvec3 NBodyAcceleration(const glm::dvec3& r, double julianDate, const NBodyOptions& options) {
    const double rn = glm::length(r);
    glm::dvec3 acc = -kMuSun * r / (rn * rn * rn);
    glm::dvec3 earth(0.0);
    for (const int index : kPerturbers) {
        const BodyConstants* body = FindBody(index);
        const glm::dvec3 rp = PlanetPosition(index, julianDate);
        if (index == 3) {
            earth = rp;
        }
        if (index == options.excludeBody) {
            continue;
        }
        const glm::dvec3 d = rp - r;
        const double dn = glm::length(d);
        const double rpn = glm::length(rp);
        // Direct pull on the craft minus the pull on the Sun (the frame is heliocentric).
        acc += body->mu * (d / (dn * dn * dn) - rp / (rpn * rpn * rpn));
    }
    if (options.includeMoon && options.excludeBody != 3) {
        const glm::dvec3 moon = earth + MoonGeocentricPosition(julianDate);
        const glm::dvec3 d = moon - r;
        const double dn = glm::length(d);
        const double mn = glm::length(moon);
        acc += kMuMoon * (d / (dn * dn * dn) - moon / (mn * mn * mn));
    }
    return acc;
}

NBodyResult PropagateNBody(const StateVector& start, double jd0, double jd1, const NBodyOptions& options,
                           int watchBody) {
    // Dormand–Prince 5(4) tableau (Dormand & Prince 1980); the 7th stage is the next step's
    // first (FSAL).
    constexpr double c2 = 1.0 / 5.0, c3 = 3.0 / 10.0, c4 = 4.0 / 5.0, c5 = 8.0 / 9.0;
    constexpr double a21 = 1.0 / 5.0;
    constexpr double a31 = 3.0 / 40.0, a32 = 9.0 / 40.0;
    constexpr double a41 = 44.0 / 45.0, a42 = -56.0 / 15.0, a43 = 32.0 / 9.0;
    constexpr double a51 = 19372.0 / 6561.0, a52 = -25360.0 / 2187.0, a53 = 64448.0 / 6561.0, a54 = -212.0 / 729.0;
    constexpr double a61 = 9017.0 / 3168.0, a62 = -355.0 / 33.0, a63 = 46732.0 / 5247.0, a64 = 49.0 / 176.0,
                     a65 = -5103.0 / 18656.0;
    constexpr double b1 = 35.0 / 384.0, b3 = 500.0 / 1113.0, b4 = 125.0 / 192.0, b5 = -2187.0 / 6784.0,
                     b6 = 11.0 / 84.0;
    constexpr double e1 = 71.0 / 57600.0, e3 = -71.0 / 16695.0, e4 = 71.0 / 1920.0, e5 = -17253.0 / 339200.0,
                     e6 = 22.0 / 525.0, e7 = -1.0 / 40.0; // b - b*

    struct Deriv {
        glm::dvec3 dr;
        glm::dvec3 dv;
    };
    auto f = [&](double tSeconds, const glm::dvec3& r, const glm::dvec3& v) {
        return Deriv{v, NBodyAcceleration(r, jd0 + tSeconds / kSecondsPerDay, options)};
    };

    NBodyResult result;
    result.state = start;
    const double total = (jd1 - jd0) * kSecondsPerDay;
    if (total == 0.0) {
        result.ok = true;
        return result;
    }
    const double direction = total > 0.0 ? 1.0 : -1.0;
    double t = 0.0;
    double h = direction * std::min(3600.0, std::fabs(total));
    glm::dvec3 r = start.r;
    glm::dvec3 v = start.v;
    Deriv k1 = f(t, r, v);
    result.closestApproachKm = 1e300;
    auto watch = [&](double tSeconds, const glm::dvec3& rr) {
        if (watchBody < 1) {
            return;
        }
        const double jd = jd0 + tSeconds / kSecondsPerDay;
        const double d = glm::length(rr - PlanetPosition(watchBody, jd));
        if (d < result.closestApproachKm) {
            result.closestApproachKm = d;
            result.closestApproachJd = jd;
        }
    };
    watch(t, r);

    while (direction * (total - t) > 0.0 && result.steps < options.maxSteps) {
        if (direction * (t + h - total) > 0.0) {
            h = total - t;
        }
        const Deriv k2 = f(t + c2 * h, r + h * (a21 * k1.dr), v + h * (a21 * k1.dv));
        const Deriv k3 = f(t + c3 * h, r + h * (a31 * k1.dr + a32 * k2.dr), v + h * (a31 * k1.dv + a32 * k2.dv));
        const Deriv k4 = f(t + c4 * h, r + h * (a41 * k1.dr + a42 * k2.dr + a43 * k3.dr),
                           v + h * (a41 * k1.dv + a42 * k2.dv + a43 * k3.dv));
        const Deriv k5 = f(t + c5 * h, r + h * (a51 * k1.dr + a52 * k2.dr + a53 * k3.dr + a54 * k4.dr),
                           v + h * (a51 * k1.dv + a52 * k2.dv + a53 * k3.dv + a54 * k4.dv));
        const Deriv k6 = f(t + h, r + h * (a61 * k1.dr + a62 * k2.dr + a63 * k3.dr + a64 * k4.dr + a65 * k5.dr),
                           v + h * (a61 * k1.dv + a62 * k2.dv + a63 * k3.dv + a64 * k4.dv + a65 * k5.dv));
        const glm::dvec3 rNew = r + h * (b1 * k1.dr + b3 * k3.dr + b4 * k4.dr + b5 * k5.dr + b6 * k6.dr);
        const glm::dvec3 vNew = v + h * (b1 * k1.dv + b3 * k3.dv + b4 * k4.dv + b5 * k5.dv + b6 * k6.dv);
        const Deriv k7 = f(t + h, rNew, vNew);
        const glm::dvec3 errR = h * (e1 * k1.dr + e3 * k3.dr + e4 * k4.dr + e5 * k5.dr + e6 * k6.dr + e7 * k7.dr);

        // Position error against the tolerance (velocity follows it on these smooth arcs).
        const double scale = options.absoluteToleranceKm + options.relativeTolerance * glm::length(rNew);
        const double err = glm::length(errR) / scale;
        if (!std::isfinite(err)) {
            break; // the craft hit a body's centre: ok stays false
        }
        if (err <= 1.0 || std::fabs(h) <= options.minStepSeconds) {
            t += h;
            r = rNew;
            v = vNew;
            k1 = k7;
            ++result.steps;
            watch(t, r);
        } else {
            ++result.rejected;
        }
        const double factor = err > 0.0 ? std::clamp(0.9 * std::pow(err, -0.2), 0.2, 5.0) : 5.0;
        h = direction * std::clamp(std::fabs(h) * factor, options.minStepSeconds, options.maxStepSeconds);
    }
    result.state = {r, v};
    result.ok = direction * (total - t) <= 0.0;
    return result;
}

Flyby FlybyGeometry(const glm::dvec3& vInfIn, const glm::dvec3& vInfOut, double mu, double radiusKm,
                    double minAltitudeKm) {
    Flyby out;
    out.vInfInKmS = glm::length(vInfIn);
    out.vInfOutKmS = glm::length(vInfOut);
    if (out.vInfInKmS <= 0.0 || out.vInfOutKmS <= 0.0) {
        return out;
    }
    out.turnAngleRad = std::acos(std::clamp(glm::dot(vInfIn, vInfOut) / (out.vInfInKmS * out.vInfOutKmS), -1.0, 1.0));
    // Each half of a powered flyby turns by asin(1 / e) of its own hyperbola; find the
    // periapsis where the two halves add up to the turn (the sum falls monotonically with rp).
    const double vin2 = out.vInfInKmS * out.vInfInKmS;
    const double vout2 = out.vInfOutKmS * out.vInfOutKmS;
    auto turnAt = [&](double rp) {
        return std::asin(1.0 / (1.0 + rp * vin2 / mu)) + std::asin(1.0 / (1.0 + rp * vout2 / mu));
    };
    double lo = std::log(1e-3);
    double hi = std::log(1e12);
    if (turnAt(std::exp(lo)) < out.turnAngleRad) {
        out.periapsisKm = 0.0; // more turn than even a grazing pass gives
    } else {
        for (int i = 0; i < 200; ++i) {
            const double mid = 0.5 * (lo + hi);
            if (turnAt(std::exp(mid)) > out.turnAngleRad) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        out.periapsisKm = std::exp(0.5 * (lo + hi));
    }
    out.altitudeKm = out.periapsisKm - radiusKm;
    const double rp = std::max(out.periapsisKm, 1e-3);
    out.poweredDeltaVKmS = std::fabs(std::sqrt(vout2 + 2.0 * mu / rp) - std::sqrt(vin2 + 2.0 * mu / rp));
    out.feasible = out.altitudeKm >= minAltitudeKm;

    // B-plane: S along the incoming asymptote, T in the ecliptic, R = S x T. The flyby turns
    // v∞ away from B, so B points opposite the out-going asymptote's part across S.
    const glm::dvec3 sHat = vInfIn / out.vInfInKmS;
    glm::dvec3 tHat = glm::cross(sHat, glm::dvec3(0.0, 0.0, 1.0));
    if (glm::length(tHat) < 1e-9) {
        tHat = glm::dvec3(1.0, 0.0, 0.0);
    }
    tHat = glm::normalize(tHat);
    const glm::dvec3 rHat = glm::cross(sHat, tHat);
    const glm::dvec3 across = vInfOut - glm::dot(vInfOut, sHat) * sHat;
    const double vinfMean = 0.5 * (out.vInfInKmS + out.vInfOutKmS);
    out.bMagnitudeKm = rp * std::sqrt(1.0 + 2.0 * mu / (rp * vinfMean * vinfMean));
    if (glm::length(across) > 1e-12) {
        const glm::dvec3 bHat = -glm::normalize(across);
        out.bDotTKm = out.bMagnitudeKm * glm::dot(bHat, tHat);
        out.bDotRKm = out.bMagnitudeKm * glm::dot(bHat, rHat);
    }
    return out;
}

double HyperbolicDeltaV(double mu, double rpKm, double vInfKmS) {
    return std::sqrt(vInfKmS * vInfKmS + 2.0 * mu / rpKm) - std::sqrt(mu / rpKm);
}

bool FindSoiCrossing(const StateVector& start, double jd0, double jd1, int body, bool entering, double& outJd) {
    const BodyConstants* constants = FindBody(body);
    if (!constants || jd1 <= jd0) {
        return false;
    }
    const double soi = SphereOfInfluenceKm(*constants);
    auto inside = [&](double jd) {
        const StateVector s = KeplerPropagate(start, kMuSun, (jd - jd0) * kSecondsPerDay);
        return glm::length(s.r - PlanetPosition(body, jd)) - soi;
    };
    const int samples = std::clamp(static_cast<int>((jd1 - jd0) * 4.0), 16, 20000);
    double prevJd = jd0;
    double prev = inside(jd0);
    for (int i = 1; i <= samples; ++i) {
        const double jd = jd0 + (jd1 - jd0) * i / samples;
        const double d = inside(jd);
        const bool crosses = entering ? (prev > 0.0 && d <= 0.0) : (prev <= 0.0 && d > 0.0);
        if (crosses) {
            double lo = prevJd;
            double hi = jd;
            for (int k = 0; k < 60; ++k) {
                const double mid = 0.5 * (lo + hi);
                const bool midInside = inside(mid) <= 0.0;
                if (midInside == entering) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            outJd = 0.5 * (lo + hi);
            return true;
        }
        prevJd = jd;
        prev = d;
    }
    return false;
}

} // namespace Flight
