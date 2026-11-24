#include "astro/flight/StateVector.h"
#include "astro/Ephemeris.h"
#include "astro/flight/FlightConstants.h"
#include <cmath>
#include <glm/geometric.hpp>

namespace Flight {

namespace {

constexpr double kVelocityStepDays = 0.01;

} // namespace

glm::dvec3 PlanetPosition(int index, double julianDate) {
    const Ephemeris::HelioLB lb = Ephemeris::Position(index, julianDate);
    const double r = lb.rAu * kAuKm;
    const double cosLat = std::cos(lb.latRad);
    return {r * std::cos(lb.lonRad) * cosLat, r * std::sin(lb.lonRad) * cosLat, r * std::sin(lb.latRad)};
}

StateVector PlanetState(int index, double julianDate) {
    StateVector state;
    state.r = PlanetPosition(index, julianDate);
    const glm::dvec3 ahead = PlanetPosition(index, julianDate + kVelocityStepDays);
    const glm::dvec3 behind = PlanetPosition(index, julianDate - kVelocityStepDays);
    state.v = (ahead - behind) / (2.0 * kVelocityStepDays * kSecondsPerDay);
    return state;
}

glm::dvec3 MoonGeocentricPosition(double julianDate) {
    double au[3] = {0.0, 0.0, 0.0};
    Ephemeris::SatellitePosition(16, julianDate, au);
    return glm::dvec3(au[0], au[1], au[2]) * kAuKm;
}

Elements ElementsOf(const StateVector& state, double mu) {
    Elements out;
    const double r = glm::length(state.r);
    const double v2 = glm::dot(state.v, state.v);
    out.energy = 0.5 * v2 - mu / r;
    out.a = std::fabs(out.energy) > 1e-300 ? -mu / (2.0 * out.energy) : INFINITY;
    const glm::dvec3 h = glm::cross(state.r, state.v);
    const glm::dvec3 eVec = glm::cross(state.v, h) / mu - state.r / r;
    out.e = glm::length(eVec);
    const double hn = glm::length(h);
    out.iRad = hn > 0.0 ? std::acos(glm::clamp(h.z / hn, -1.0, 1.0)) : 0.0;
    return out;
}

} // namespace Flight
