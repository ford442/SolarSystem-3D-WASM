// Izzo's Lambert solver (Celest. Mech. Dyn. Astr. 121, 2015). The variable names follow the
// paper: lambda (λ) is the geometry parameter, T the non-dimensional time of flight, x the
// iteration variable (x < 1 ellipse, x = 1 parabola, x > 1 hyperbola), y = sqrt(1 - λ²(1 - x²)).
#include "astro/flight/Lambert.h"
#include "astro/flight/FlightConstants.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <limits>
#include <vector>

namespace Flight::Lambert {

namespace {

/** 2F1(3, 1; 5/2; z), the series Battin's form of the time of flight needs near the parabola. */
double Hypergeometric(double z) {
    if (z >= 1.0) {
        return std::numeric_limits<double>::infinity();
    }
    double result = 1.0;
    double term = 1.0;
    for (int i = 0; i < 1000; ++i) {
        term *= (3.0 + i) * (1.0 + i) / (2.5 + i) * z / (i + 1.0);
        const double previous = result;
        result += term;
        if (previous == result) {
            break;
        }
    }
    return result;
}

double ComputeY(double x, double lambda) {
    return std::sqrt(std::max(0.0, 1.0 - lambda * lambda * (1.0 - x * x)));
}

double ComputePsi(double x, double y, double lambda) {
    if (x >= -1.0 && x < 1.0) {
        return std::acos(std::clamp(x * y + lambda * (1.0 - x * x), -1.0, 1.0));
    }
    if (x > 1.0) {
        return std::asinh((y - x * lambda) * std::sqrt(x * x - 1.0));
    }
    return 0.0;
}

/** Non-dimensional time of flight at x (Izzo eq. 18, Battin's series near x = 1 for M = 0). */
double TimeOfFlight(double x, double y, double lambda, int revolutions) {
    if (revolutions == 0 && x > std::sqrt(0.6) && x < std::sqrt(1.4)) {
        const double eta = y - lambda * x;
        const double s1 = 0.5 * (1.0 - lambda - x * eta);
        const double q = 4.0 / 3.0 * Hypergeometric(s1);
        return 0.5 * (eta * eta * eta * q + 4.0 * lambda * eta);
    }
    const double psi = ComputePsi(x, y, lambda);
    const double oneMinusX2 = 1.0 - x * x;
    return ((psi + revolutions * kPi) / std::sqrt(std::fabs(oneMinusX2)) - x + lambda * y) / oneMinusX2;
}

/** dT/dx, d²T/dx², d³T/dx³ at x (Izzo eq. 22). */
struct Derivatives {
    double d1 = 0.0;
    double d2 = 0.0;
    double d3 = 0.0;
};

Derivatives TimeDerivatives(double x, double y, double t, double lambda) {
    const double l2 = lambda * lambda;
    const double l3 = l2 * lambda;
    const double l5 = l3 * l2;
    const double oneMinusX2 = 1.0 - x * x;
    Derivatives d;
    d.d1 = (3.0 * t * x - 2.0 + 2.0 * l3 * x / y) / oneMinusX2;
    d.d2 = (3.0 * t + 5.0 * x * d.d1 + 2.0 * (1.0 - l2) * l3 / (y * y * y)) / oneMinusX2;
    d.d3 = (7.0 * x * d.d2 + 8.0 * d.d1 - 6.0 * (1.0 - l2) * l5 * x / (y * y * y * y * y)) / oneMinusX2;
    return d;
}

/** The minimum time of flight of the `revolutions`-rev family (Halley on dT/dx = 0). */
double MinimumTime(double lambda, int revolutions, int maxIterations, double tolerance) {
    double x = 0.0;
    double t = TimeOfFlight(x, ComputeY(x, lambda), lambda, revolutions);
    for (int i = 0; i < maxIterations; ++i) {
        const double y = ComputeY(x, lambda);
        const Derivatives d = TimeDerivatives(x, y, t, lambda);
        const double denominator = d.d2 * d.d2 - 0.5 * d.d1 * d.d3;
        if (denominator == 0.0) {
            break;
        }
        const double next = x - d.d1 * d.d2 / denominator;
        const bool done = std::fabs(next - x) < tolerance;
        x = std::clamp(next, -0.999999, 0.999999);
        t = TimeOfFlight(x, ComputeY(x, lambda), lambda, revolutions);
        if (done) {
            break;
        }
    }
    return t;
}

/** Householder iteration on T(x) - T0 = 0. Returns iterations used, or -1 without convergence. */
int Householder(double target, double& x, double lambda, int revolutions, int maxIterations, double tolerance) {
    for (int i = 1; i <= maxIterations; ++i) {
        const double y = ComputeY(x, lambda);
        const double t = TimeOfFlight(x, y, lambda, revolutions);
        const double f = t - target;
        const Derivatives d = TimeDerivatives(x, y, t, lambda);
        const double d1sq = d.d1 * d.d1;
        const double next = x - f * (d1sq - 0.5 * f * d.d2) / (d.d1 * (d1sq - f * d.d2) + d.d3 * f * f / 6.0);
        if (!std::isfinite(next)) {
            return -1;
        }
        const double step = std::fabs(next - x);
        x = next;
        if (step < tolerance) {
            return i;
        }
    }
    return -1;
}

double InitialGuessZeroRev(double t, double lambda) {
    const double t0 = std::acos(lambda) + lambda * std::sqrt(1.0 - lambda * lambda); // eq. 19
    const double t1 = 2.0 / 3.0 * (1.0 - lambda * lambda * lambda);                  // eq. 21
    if (t >= t0) {
        return std::pow(t0 / t, 2.0 / 3.0) - 1.0;
    }
    if (t < t1) {
        const double l5 = std::pow(lambda, 5.0);
        return 2.5 * t1 / t * (t1 - t) / (1.0 - l5) + 1.0;
    }
    // Between the two: the paper's printed expression is a typo; this is the corrected one
    // (exact at both ends, x = 0 at T0 and x = 1 at T1).
    return std::exp(std::log(2.0) * std::log(t / t0) / std::log(t1 / t0)) - 1.0;
}

} // namespace

std::vector<Solution> Solve(double mu, const glm::dvec3& r1, const glm::dvec3& r2, double tofSeconds,
                            const Options& options) {
    std::vector<Solution> out;
    const double r1n = glm::length(r1);
    const double r2n = glm::length(r2);
    if (!(mu > 0.0) || !(tofSeconds > 0.0) || r1n <= 0.0 || r2n <= 0.0) {
        return out;
    }
    const glm::dvec3 chord = r2 - r1;
    const double c = glm::length(chord);
    const double s = 0.5 * (r1n + r2n + c);
    const glm::dvec3 ir1 = r1 / r1n;
    const glm::dvec3 ir2 = r2 / r2n;
    glm::dvec3 ih = glm::cross(ir1, ir2);
    const double ihn = glm::length(ih);
    if (ihn < 1e-12 || c <= 0.0) {
        return out; // 0 or 180 degrees: no unique transfer plane
    }
    ih /= ihn;

    double lambda = std::sqrt(std::max(0.0, 1.0 - std::min(1.0, c / s)));
    glm::dvec3 it1;
    glm::dvec3 it2;
    if (ih.z < 0.0) {
        lambda = -lambda;
        it1 = glm::cross(ir1, ih);
        it2 = glm::cross(ir2, ih);
    } else {
        it1 = glm::cross(ih, ir1);
        it2 = glm::cross(ih, ir2);
    }
    if (!options.prograde) {
        lambda = -lambda;
        it1 = -it1;
        it2 = -it2;
    }

    const double t = std::sqrt(2.0 * mu / (s * s * s)) * tofSeconds;

    // The most revolutions T allows (eq. 19 and the T_min refinement of section 3).
    int maxRevs = static_cast<int>(std::floor(t / kPi));
    const double t00 = std::acos(lambda) + lambda * std::sqrt(1.0 - lambda * lambda);
    if (maxRevs > 0 && t < t00 + maxRevs * kPi) {
        if (t < MinimumTime(lambda, maxRevs, options.maxIterations, options.tolerance)) {
            --maxRevs;
        }
    }
    maxRevs = std::min(maxRevs, std::max(0, options.maxRevolutions));

    const double gamma = std::sqrt(0.5 * mu * s);
    const double rho = (r1n - r2n) / c;
    const double sigma = std::sqrt(std::max(0.0, 1.0 - rho * rho));
    auto emit = [&](double x, int revs, bool right, int iterations) {
        const double y = ComputeY(x, lambda);
        const double vr1 = gamma * ((lambda * y - x) - rho * (lambda * y + x)) / r1n;
        const double vr2 = -gamma * ((lambda * y - x) + rho * (lambda * y + x)) / r2n;
        const double vt1 = gamma * sigma * (y + lambda * x) / r1n;
        const double vt2 = gamma * sigma * (y + lambda * x) / r2n;
        Solution solution;
        solution.v1 = vr1 * ir1 + vt1 * it1;
        solution.v2 = vr2 * ir2 + vt2 * it2;
        solution.revolutions = revs;
        solution.rightBranch = right;
        solution.iterations = iterations;
        if (std::isfinite(solution.v1.x) && std::isfinite(solution.v2.x)) {
            out.push_back(solution);
        }
    };

    double x = InitialGuessZeroRev(t, lambda);
    int iterations = Householder(t, x, lambda, 0, options.maxIterations, options.tolerance);
    if (iterations > 0) {
        emit(x, 0, false, iterations);
    }
    for (int revs = 1; revs <= maxRevs; ++revs) {
        const double left = std::pow((revs * kPi + kPi) / (8.0 * t), 2.0 / 3.0);
        double xl = (left - 1.0) / (left + 1.0);
        iterations = Householder(t, xl, lambda, revs, options.maxIterations, options.tolerance);
        if (iterations > 0 && std::fabs(xl) < 1.0) {
            emit(xl, revs, false, iterations);
        }
        const double right = std::pow(8.0 * t / (revs * kPi), 2.0 / 3.0);
        double xr = (right - 1.0) / (right + 1.0);
        iterations = Householder(t, xr, lambda, revs, options.maxIterations, options.tolerance);
        if (iterations > 0 && std::fabs(xr) < 1.0 && std::fabs(xr - xl) > 1e-9) {
            emit(xr, revs, true, iterations);
        }
    }
    return out;
}

bool SolveSingle(double mu, const glm::dvec3& r1, const glm::dvec3& r2, double tofSeconds, Solution& out) {
    const std::vector<Solution> solutions = Solve(mu, r1, r2, tofSeconds, Options{});
    if (solutions.empty() || solutions.front().revolutions != 0) {
        return false;
    }
    out = solutions.front();
    return true;
}

} // namespace Flight::Lambert
