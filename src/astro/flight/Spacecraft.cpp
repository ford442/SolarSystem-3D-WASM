#include "astro/flight/Spacecraft.h"
#include "Solar_System/OrbitLayout.h"
#include "astro/flight/FlightConstants.h"
#include "astro/flight/FlightPlan.h"
#include "astro/flight/StateVector.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/geometric.hpp>
#include <utility>
#include <vector>

namespace Flight {

namespace {

/** Drawn radius over the radius its true distance would map to, for body `index` at `jd`. */
double BodyStretch(int index, double julianDate) {
    const auto body = static_cast<OrbitLayout::Body>(index);
    if (OrbitLayout::UsesTrueRadius(body)) {
        return 1.0; // drawn at its real r already
    }
    const double trueAu = glm::length(PlanetPosition(index, julianDate)) / kAuKm;
    const double mapped = OrbitLayout::AuToSceneDistanceDouble(trueAu);
    return mapped > 0.0 ? OrbitLayout::GetOrbitRadiusDouble(body) / mapped : 1.0;
}

glm::dvec3 EclipticKmToSceneAu(const glm::dvec3& km) {
    return glm::dvec3(km.x, km.z, km.y) / kAuKm;
}

} // namespace

double SceneStretch(const Plan& plan, double julianDate) {
    if (plan.legs.empty()) {
        return 1.0;
    }
    const Leg& first = plan.legs.front();
    const Leg& last = plan.legs.back();
    if (julianDate <= first.jd0) {
        return BodyStretch(first.fromBody, julianDate);
    }
    if (julianDate >= last.jd1) {
        return BodyStretch(last.toBody, julianDate);
    }
    const Leg& leg = plan.legs[static_cast<std::size_t>(plan.LegAt(julianDate))];
    const double u = std::clamp((julianDate - leg.jd0) / (leg.jd1 - leg.jd0), 0.0, 1.0);
    const double a = BodyStretch(leg.fromBody, leg.jd0);
    const double b = BodyStretch(leg.toBody, leg.jd1);
    return a + (b - a) * u;
}

glm::dvec3 PlanToScene(const Plan& plan, double julianDate, const glm::dvec3& eclipticKm) {
    return OrbitLayout::HelioAuToSceneDouble(EclipticKmToSceneAu(eclipticKm)) * SceneStretch(plan, julianDate);
}

Spacecraft::Spacecraft(SpacecraftRow row, Plan plan) : _row(std::move(row)), _plan(std::move(plan)) {}

Spacecraft::Phase Spacecraft::PhaseAt(double julianDate) const {
    if (!Valid() || julianDate < _plan.StartJd()) {
        return Phase::BeforeLaunch;
    }
    return julianDate < _plan.EndJd() ? Phase::Cruise : Phase::Arrived;
}

StateVector Spacecraft::StateAt(double julianDate) const {
    return _plan.StateAt(julianDate);
}

glm::dvec3 Spacecraft::WorldPositionAt(double julianDate) const {
    return PlanToScene(_plan, julianDate, StateAt(julianDate).r);
}

glm::dvec3 Spacecraft::SceneForwardAt(double julianDate) const {
    constexpr double kStepDays = 0.05;
    const glm::dvec3 d = WorldPositionAt(julianDate + kStepDays) - WorldPositionAt(julianDate - kStepDays);
    const double n = glm::length(d);
    return n > 0.0 ? d / n : glm::dvec3(1.0, 0.0, 0.0);
}

std::vector<double> Spacecraft::PathDates(int perLeg) const {
    std::vector<double> dates;
    if (!Valid()) {
        return dates;
    }
    perLeg = std::max(perLeg, 2);
    dates.reserve(_plan.legs.size() * static_cast<std::size_t>(perLeg));
    for (std::size_t i = 0; i < _plan.legs.size(); ++i) {
        const Leg& leg = _plan.legs[i];
        // Cosine spacing: denser at both ends, where the arc bends round its planets.
        for (int k = i == 0 ? 0 : 1; k < perLeg; ++k) {
            const double u = 0.5 - 0.5 * std::cos(kPi * k / (perLeg - 1));
            dates.push_back(leg.jd0 + (leg.jd1 - leg.jd0) * u);
        }
    }
    return dates;
}

std::vector<glm::dvec3> Spacecraft::PathScene(int perLeg) const {
    const std::vector<double> dates = PathDates(perLeg);
    std::vector<glm::dvec3> path;
    path.reserve(dates.size());
    for (const double jd : dates) {
        path.push_back(WorldPositionAt(jd));
    }
    return path;
}

} // namespace Flight
