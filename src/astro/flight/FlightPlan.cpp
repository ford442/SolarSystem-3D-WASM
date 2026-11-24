#include "astro/flight/FlightPlan.h"
#include "astro/Ephemeris.h"
#include "astro/MissionCatalog.h"
#include "astro/flight/FlightConstants.h"
#include "astro/flight/Lambert.h"
#include "astro/flight/Propagator.h"
#include "astro/flight/StateVector.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <glm/geometric.hpp>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace Flight {

namespace {

// A flyby below the body's floor still joins the legs, but the leg choice steers away from it.
constexpr double kInfeasibleFlybyPenaltyKmS = 10.0;

double Jd(int year, int month, int day, int hour, int minute) {
    return Ephemeris::JulianDateFromYmd(year, month, day) + (hour + minute / 60.0) / 24.0;
}

const char* KindName(EncounterReport::Kind kind) {
    switch (kind) {
        case EncounterReport::Kind::Launch: return "launch";
        case EncounterReport::Kind::Flyby: return "flyby";
        case EncounterReport::Kind::Arrival: return "arrival";
    }
    return "?";
}

void IsoDate(double jd, char* out, std::size_t size) {
    int y = 0;
    int m = 0;
    int d = 0;
    Ephemeris::YmdFromJulianDate(jd, y, m, d);
    std::snprintf(out, size, "%04d-%02d-%02d", y, m, d);
}

} // namespace

int Plan::LegAt(double julianDate) const {
    if (legs.empty()) {
        return -1;
    }
    for (std::size_t i = 0; i < legs.size(); ++i) {
        if (julianDate < legs[i].jd1) {
            return static_cast<int>(i);
        }
    }
    return static_cast<int>(legs.size()) - 1;
}

StateVector Plan::StateAt(double julianDate) const {
    if (legs.empty()) {
        return {};
    }
    if (julianDate <= legs.front().jd0) {
        return PlanetState(legs.front().fromBody, julianDate);
    }
    if (julianDate >= legs.back().jd1) {
        return PlanetState(legs.back().toBody, julianDate);
    }
    const Leg& leg = legs[static_cast<std::size_t>(LegAt(julianDate))];
    return KeplerPropagate(leg.start, kMuSun, (julianDate - leg.jd0) * kSecondsPerDay);
}

Plan PlanSequence(const std::vector<Encounter>& sequence, const PlanOptions& options) {
    Plan plan;
    if (sequence.size() < 2) {
        plan.error = "a flight needs at least two encounters";
        return plan;
    }
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        if (!FindBody(sequence[i].body)) {
            plan.error = "unknown body " + std::to_string(sequence[i].body);
            return plan;
        }
        if (i > 0 && !(sequence[i].julianDate > sequence[i - 1].julianDate)) {
            plan.error = "encounter dates must increase";
            return plan;
        }
        if (i > 0 && sequence[i].body == sequence[i - 1].body && options.maxRevolutions < 1) {
            plan.error = "a resonant leg (same body twice) needs multi-revolution Lambert";
            return plan;
        }
    }

    // Every Lambert solution of every leg (0-rev, then each revolution count's two branches).
    const std::size_t legCount = sequence.size() - 1;
    std::vector<StateVector> bodies(sequence.size());
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        bodies[i] = PlanetState(sequence[i].body, sequence[i].julianDate);
    }
    std::vector<std::vector<Lambert::Solution>> solutions(legCount);
    Lambert::Options lambert;
    lambert.maxRevolutions = options.maxRevolutions;
    for (std::size_t i = 0; i < legCount; ++i) {
        const double tof = (sequence[i + 1].julianDate - sequence[i].julianDate) * kSecondsPerDay;
        solutions[i] = Lambert::Solve(kMuSun, bodies[i].r, bodies[i + 1].r, tof, lambert);
        if (solutions[i].empty()) {
            plan.error = "no Lambert solution for leg " + std::to_string(i + 1);
            return plan;
        }
    }

    // Cheapest chain of solutions: launch Δv + powered-flyby Δv at each junction + capture Δv,
    // a dynamic programme over (leg, solution) since each junction only couples two legs.
    const BodyConstants& first = *FindBody(sequence.front().body);
    const BodyConstants& last = *FindBody(sequence.back().body);
    auto launchCost = [&](const Lambert::Solution& s) {
        return HyperbolicDeltaV(first.mu, first.radiusKm + options.parkingAltitudeKm,
                                glm::length(s.v1 - bodies.front().v));
    };
    auto captureCost = [&](const Lambert::Solution& s) {
        return HyperbolicDeltaV(last.mu, last.radiusKm + options.captureAltitudeKm,
                                glm::length(s.v2 - bodies.back().v));
    };
    auto junctionCost = [&](std::size_t leg, const Lambert::Solution& in, const Lambert::Solution& out) {
        const BodyConstants& body = *FindBody(sequence[leg + 1].body);
        const glm::dvec3 planetV = bodies[leg + 1].v;
        const Flyby flyby =
            FlybyGeometry(in.v2 - planetV, out.v1 - planetV, body.mu, body.radiusKm, body.minFlybyAltitudeKm);
        return flyby.poweredDeltaVKmS + (flyby.feasible ? 0.0 : kInfeasibleFlybyPenaltyKmS);
    };

    std::vector<std::vector<double>> cost(legCount);
    std::vector<std::vector<int>> from(legCount);
    for (std::size_t i = 0; i < legCount; ++i) {
        cost[i].assign(solutions[i].size(), std::numeric_limits<double>::infinity());
        from[i].assign(solutions[i].size(), -1);
        for (std::size_t b = 0; b < solutions[i].size(); ++b) {
            if (i == 0) {
                cost[i][b] = launchCost(solutions[i][b]);
                continue;
            }
            for (std::size_t a = 0; a < solutions[i - 1].size(); ++a) {
                const double c = cost[i - 1][a] + junctionCost(i - 1, solutions[i - 1][a], solutions[i][b]);
                if (c < cost[i][b]) {
                    cost[i][b] = c;
                    from[i][b] = static_cast<int>(a);
                }
            }
        }
    }
    int pick = -1;
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t b = 0; b < solutions[legCount - 1].size(); ++b) {
        const double c = cost[legCount - 1][b] + captureCost(solutions[legCount - 1][b]);
        if (c < best) {
            best = c;
            pick = static_cast<int>(b);
        }
    }
    std::vector<int> chosen(legCount, 0);
    for (std::size_t i = legCount; i-- > 0;) {
        chosen[i] = pick;
        pick = from[i][static_cast<std::size_t>(pick)];
    }

    for (std::size_t i = 0; i < legCount; ++i) {
        const Lambert::Solution& s = solutions[i][static_cast<std::size_t>(chosen[i])];
        Leg leg;
        leg.fromBody = sequence[i].body;
        leg.toBody = sequence[i + 1].body;
        leg.jd0 = sequence[i].julianDate;
        leg.jd1 = sequence[i + 1].julianDate;
        leg.start = {bodies[i].r, s.v1};
        leg.arriveVelocity = s.v2;
        leg.revolutions = s.revolutions;
        plan.legs.push_back(leg);
    }

    for (std::size_t i = 0; i < sequence.size(); ++i) {
        EncounterReport report;
        report.body = sequence[i].body;
        report.julianDate = sequence[i].julianDate;
        report.historicAltitudeKm = sequence[i].historicAltitudeKm;
        const BodyConstants& body = *FindBody(report.body);
        if (i > 0) {
            report.vInfIn = plan.legs[i - 1].arriveVelocity - bodies[i].v;
        }
        if (i < legCount) {
            report.vInfOut = plan.legs[i].start.v - bodies[i].v;
        }
        if (i == 0) {
            report.kind = EncounterReport::Kind::Launch;
            plan.c3 = glm::dot(report.vInfOut, report.vInfOut);
            report.deltaVKmS =
                HyperbolicDeltaV(body.mu, body.radiusKm + options.parkingAltitudeKm, glm::length(report.vInfOut));
        } else if (i == legCount) {
            report.kind = EncounterReport::Kind::Arrival;
            report.deltaVKmS =
                HyperbolicDeltaV(body.mu, body.radiusKm + options.captureAltitudeKm, glm::length(report.vInfIn));
        } else {
            report.kind = EncounterReport::Kind::Flyby;
            report.flyby =
                FlybyGeometry(report.vInfIn, report.vInfOut, body.mu, body.radiusKm, body.minFlybyAltitudeKm);
            report.deltaVKmS = report.flyby.poweredDeltaVKmS;
        }
        plan.totalDeltaVKmS += report.deltaVKmS;
        plan.encounters.push_back(report);
    }
    plan.ok = true;
    return plan;
}

Plan PlanTransfer(int from, int to, double departureJd, double tofDays, const PlanOptions& options) {
    PlanOptions direct = options;
    direct.maxRevolutions = 0; // a direct transfer is the 0-rev arc the porkchop cell showed
    Plan plan = PlanSequence({{from, departureJd}, {to, departureJd + tofDays}}, direct);
    const BodyConstants* a = FindBody(from);
    const BodyConstants* b = FindBody(to);
    if (a && b) {
        plan.id = std::string(a->id) + "-" + b->id;
        plan.name = std::string(a->name) + " → " + b->name;
    }
    return plan;
}

const std::vector<Preset>& Presets() {
    // Launch and closest-approach dates (UTC) from the JPL/NASA mission pages; historic
    // altitudes are above the cloud tops / surface.
    static const std::vector<Preset> kPresets = {
        {"voyager1",
         "Voyager 1",
         "EJS",
         {{3, Jd(1977, 9, 5, 12, 56)}, {5, Jd(1979, 3, 5, 12, 5), 277400.0}, {6, Jd(1980, 11, 12, 23, 46), 124000.0}}},
        {"voyager2",
         "Voyager 2",
         "EJSUN",
         {{3, Jd(1977, 8, 20, 14, 29)},
          {5, Jd(1979, 7, 9, 22, 29), 650180.0},
          {6, Jd(1981, 8, 26, 3, 24), 101000.0},
          {7, Jd(1986, 1, 24, 17, 59), 81500.0},
          {8, Jd(1989, 8, 25, 3, 56), 4951.0}}},
        {"cassini",
         "Cassini–Huygens",
         "EVVEJS",
         {{3, Jd(1997, 10, 15, 8, 43)},
          {2, Jd(1998, 4, 26, 13, 45), 284.0},
          {2, Jd(1999, 6, 24, 20, 30), 623.0},
          {3, Jd(1999, 8, 18, 3, 28), 1171.0},
          {5, Jd(2000, 12, 30, 10, 5), 9.65e6},
          {6, Jd(2004, 7, 1, 2, 48)}}},
        {"new-horizons",
         "New Horizons",
         "EJP",
         {{3, Jd(2006, 1, 19, 19, 0)}, {5, Jd(2007, 2, 28, 5, 43), 2.23e6}, {9, Jd(2015, 7, 14, 11, 49)}}},
    };
    return kPresets;
}

const Preset* FindPreset(const std::string& id) {
    for (const Preset& preset : Presets()) {
        if (id == preset.id) {
            return &preset;
        }
    }
    return nullptr;
}

Plan PlanPreset(const Preset& preset, const PlanOptions& options) {
    Plan plan = PlanSequence(preset.encounters, options);
    plan.id = preset.id;
    plan.name = preset.name;
    return plan;
}

Residuals CompareWithMission(const Plan& plan, const MissionCatalog::Mission& mission, int samples) {
    Residuals out;
    if (!plan.ok || mission.samples.empty() || samples < 2) {
        return out;
    }
    const double jd0 = std::max(plan.StartJd(), mission.samples.front().julianDate);
    const double jd1 = std::min(plan.EndJd(), mission.samples.back().julianDate);
    if (!(jd1 > jd0)) {
        return out;
    }
    double sumSq = 0.0;
    for (int i = 0; i < samples; ++i) {
        const double jd = jd0 + (jd1 - jd0) * i / (samples - 1);
        glm::dvec3 storedSceneAu(0.0);
        MissionCatalog::InterpolateAu(mission, jd, storedSceneAu);
        const glm::dvec3 km = plan.StateAt(jd).r;
        // Scene axes are (x, z, y) of the ecliptic ones.
        const glm::dvec3 computedSceneAu = glm::dvec3(km.x, km.z, km.y) / kAuKm;
        const double d = glm::length(computedSceneAu - storedSceneAu);
        sumSq += d * d;
        if (d > out.maxAu) {
            out.maxAu = d;
            out.maxJd = jd;
        }
        ++out.samples;
    }
    out.rmsAu = std::sqrt(sumSq / out.samples);
    return out;
}

NBodyCheck CheckLegNBody(const Plan& plan, int legIndex) {
    NBodyCheck out;
    if (!plan.ok || legIndex < 0 || legIndex >= static_cast<int>(plan.legs.size())) {
        return out;
    }
    const Leg& leg = plan.legs[static_cast<std::size_t>(legIndex)];
    const BodyConstants* from = FindBody(leg.fromBody);
    const BodyConstants* to = FindBody(leg.toBody);
    double exitJd = 0.0;
    if (!FindSoiCrossing(leg.start, leg.jd0, leg.jd1, leg.fromBody, false, exitJd)) {
        return out;
    }
    const StateVector planet = PlanetState(leg.fromBody, exitJd);
    const glm::dvec3 vInf = leg.start.v - PlanetState(leg.fromBody, leg.jd0).v;
    const double vInfN = glm::length(vInf);
    const double soi = SphereOfInfluenceKm(*from);
    const glm::dvec3 asymptote = vInf / vInfN;
    StateVector start;
    start.r = planet.r + asymptote * soi;
    start.v = planet.v + asymptote * std::sqrt(vInfN * vInfN + 2.0 * from->mu / soi);
    const NBodyResult n = PropagateNBody(start, exitJd, leg.jd1 + 5.0, NBodyOptions{}, leg.toBody);
    out.ok = n.ok;
    out.soiExitJd = exitJd;
    out.closestApproachKm = n.closestApproachKm;
    out.closestApproachJd = n.closestApproachJd;
    out.missInSoiRadii = n.closestApproachKm / SphereOfInfluenceKm(*to);
    out.steps = n.steps;
    return out;
}

std::string PlanJson(const Plan& plan) {
    std::ostringstream out;
    char buffer[768];
    if (!plan.ok) {
        out << R"({"ok":false,"error":")" << plan.error << R"("})";
        return out.str();
    }
    std::snprintf(buffer, sizeof(buffer),
                  R"({"ok":true,"id":"%s","name":"%s","startJd":%.5f,"endJd":%.5f,"c3":%.4f,"totalDeltaV":%.4f,)",
                  plan.id.c_str(), plan.name.c_str(), plan.StartJd(), plan.EndJd(), plan.c3, plan.totalDeltaVKmS);
    out << buffer << R"("legs":[)";
    for (std::size_t i = 0; i < plan.legs.size(); ++i) {
        const Leg& leg = plan.legs[i];
        std::snprintf(buffer, sizeof(buffer), R"(%s{"from":%d,"to":%d,"jd0":%.5f,"jd1":%.5f,"revolutions":%d})",
                      i > 0 ? "," : "", leg.fromBody, leg.toBody, leg.jd0, leg.jd1, leg.revolutions);
        out << buffer;
    }
    out << R"(],"encounters":[)";
    for (std::size_t i = 0; i < plan.encounters.size(); ++i) {
        const EncounterReport& e = plan.encounters[i];
        char date[16];
        IsoDate(e.julianDate, date, sizeof(date));
        const BodyConstants* body = FindBody(e.body);
        std::snprintf(buffer, sizeof(buffer),
                      R"(%s{"kind":"%s","body":%d,"bodyName":"%s","jd":%.5f,"date":"%s","vInfIn":%.4f,"vInfOut":%.4f,)"
                      R"("deltaV":%.4f)",
                      i > 0 ? "," : "", KindName(e.kind), e.body, body ? body->name : "?", e.julianDate, date,
                      glm::length(e.vInfIn), glm::length(e.vInfOut), e.deltaVKmS);
        out << buffer;
        if (e.kind == EncounterReport::Kind::Flyby) {
            std::snprintf(buffer, sizeof(buffer),
                          R"(,"turnDeg":%.3f,"altitudeKm":%.1f,"bDotT":%.1f,"bDotR":%.1f,"feasible":%s)",
                          e.flyby.turnAngleRad * 180.0 / kPi, e.flyby.altitudeKm, e.flyby.bDotTKm, e.flyby.bDotRKm,
                          e.flyby.feasible ? "true" : "false");
            out << buffer;
        }
        if (e.historicAltitudeKm >= 0.0) {
            std::snprintf(buffer, sizeof(buffer), R"(,"historicAltitudeKm":%.1f)", e.historicAltitudeKm);
            out << buffer;
        }
        out << '}';
    }
    out << "]}";
    return out.str();
}

} // namespace Flight
