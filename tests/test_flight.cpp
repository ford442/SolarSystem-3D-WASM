// Flight planner (src/astro/flight/, docs/ARCHITECTURE.md §14): Lambert against textbook and
// mission references, the porkchop grid, the propagators, flybys, real missions rebuilt from
// their encounter dates, and the craft landing on its target's drawn disc in both scale modes.
#include <gtest/gtest.h>

#include "Solar_System/OrbitLayout.h"
#include "astro/Ephemeris.h"
#include "astro/MissionCatalog.h"
#include "astro/flight/FlightConstants.h"
#include "astro/flight/FlightPlan.h"
#include "astro/flight/Lambert.h"
#include "astro/flight/Porkchop.h"
#include "astro/flight/Propagator.h"
#include "astro/flight/Spacecraft.h"
#include "astro/flight/StateVector.h"
#include "core/Log.h"

#include <cmath>
#include <glm/geometric.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

namespace {

constexpr double kMuEarth = 398600.4418;

double Jd(int y, int m, int d, double hours = 0.0) {
    return Ephemeris::JulianDateFromYmd(y, m, d) + hours / 24.0;
}

void ExpectVecNear(const glm::dvec3& a, const glm::dvec3& b, double tol) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

} // namespace

// --- Lambert -------------------------------------------------------------------------------

TEST(Lambert, CurtisExample5_2) {
    // Curtis, Orbital Mechanics for Engineering Students, Example 5.2 (geocentric, 1 h).
    const glm::dvec3 r1(5000.0, 10000.0, 2100.0);
    const glm::dvec3 r2(-14600.0, 2500.0, 7000.0);
    Flight::Lambert::Solution s;
    ASSERT_TRUE(Flight::Lambert::SolveSingle(kMuEarth, r1, r2, 3600.0, s));
    ExpectVecNear(s.v1, {-5.9925, 1.9254, 3.2456}, 1e-3);
    ExpectVecNear(s.v2, {-3.3125, -4.1966, -0.38529}, 1e-3);
}

TEST(Lambert, ValladoExample7_5) {
    // Vallado, Fundamentals of Astrodynamics and Applications, Example 7-5 (76 min, short way).
    const glm::dvec3 r1(15945.34, 0.0, 0.0);
    const glm::dvec3 r2(12214.83899, 10249.46731, 0.0);
    Flight::Lambert::Solution s;
    ASSERT_TRUE(Flight::Lambert::SolveSingle(kMuEarth, r1, r2, 76.0 * 60.0, s));
    ExpectVecNear(s.v1, {2.058913, 2.915965, 0.0}, 1e-4);
    ExpectVecNear(s.v2, {-3.451565, 0.910315, 0.0}, 1e-4);
}

TEST(Lambert, EverySolutionFliesToTheTarget) {
    // Each 0- and multi-rev solution, propagated on its own conic, lands on r2 with v2.
    const glm::dvec3 r1(1.0e8, 2.0e7, 1.0e6);
    const glm::dvec3 r2(-1.2e8, 1.5e8, -4.0e6);
    const double tof = 900.0 * Flight::kSecondsPerDay;
    Flight::Lambert::Options options;
    options.maxRevolutions = 3;
    const auto solutions = Flight::Lambert::Solve(Flight::kMuSun, r1, r2, tof, options);
    ASSERT_GE(solutions.size(), 3u); // 0-rev plus at least one multi-rev pair
    EXPECT_EQ(solutions.front().revolutions, 0);
    for (const auto& s : solutions) {
        const Flight::StateVector end = Flight::KeplerPropagate({r1, s.v1}, Flight::kMuSun, tof);
        EXPECT_LT(glm::length(end.r - r2), 1.0) << "revs " << s.revolutions << " right " << s.rightBranch;
        EXPECT_LT(glm::length(end.v - s.v2), 1e-6);
    }
}

TEST(Lambert, RetrogradeAndDegenerate) {
    const glm::dvec3 r1(7000.0, 0.0, 0.0);
    const glm::dvec3 r2(0.0, 8000.0, 0.0);
    Flight::Lambert::Options retro;
    retro.prograde = false;
    const auto s = Flight::Lambert::Solve(kMuEarth, r1, r2, 5000.0, retro);
    ASSERT_FALSE(s.empty());
    EXPECT_LT(glm::cross(r1, s.front().v1).z, 0.0);                         // clockwise about +z
    EXPECT_TRUE(Flight::Lambert::Solve(kMuEarth, r1, -r1, 5000.0).empty()); // 180 degrees: no plane
    EXPECT_TRUE(Flight::Lambert::Solve(kMuEarth, r1, r2, -1.0).empty());
}

TEST(Lambert, Mars2020Window) {
    // Mars 2020 (Perseverance): launch 2020-07-30 11:50 UTC, entry 2021-02-18 20:48 UTC.
    // ULA's mission brochure gives C3 = 14.49 km^2/s^2 at separation (v∞ = 3.807 km/s).
    double c3 = 0.0;
    double vInfArrive = 0.0;
    ASSERT_TRUE(Flight::Porkchop::EvaluateCell(3, 4, Jd(2020, 7, 30, 11.83),
                                               Jd(2021, 2, 18, 20.8) - Jd(2020, 7, 30, 11.83), c3, vInfArrive));
    EXPECT_NEAR(std::sqrt(c3), std::sqrt(14.49), 0.1);
    // Perseverance hit the atmosphere at ~5.4 km/s inertial from a ~2.6–2.9 km/s v∞.
    EXPECT_GT(vInfArrive, 2.4);
    EXPECT_LT(vInfArrive, 3.2);
}

// --- Porkchop ------------------------------------------------------------------------------

TEST(Porkchop, GridMatchesCellsAndFindsTheWindow) {
    Flight::Porkchop::Request request;
    request.departureStartJd = Jd(2020, 6, 1);
    request.departureSpanDays = 120.0;
    request.tofMinDays = 120.0;
    request.tofMaxDays = 360.0;
    request.departureSteps = 31;
    request.tofSteps = 25;
    const Flight::Porkchop::Grid grid = Flight::Porkchop::Compute(request);
    ASSERT_EQ(grid.c3.size(), 31u * 25u);
    EXPECT_GE(grid.tiles, 2);
    ASSERT_GE(grid.bestDeparture, 0);

    // Each tile wrote the cells its own EvaluateCell gives.
    for (int j : {0, 7, 24}) {
        for (int i : {0, 15, 30}) {
            double c3 = 0.0;
            double vInf = 0.0;
            if (Flight::Porkchop::EvaluateCell(3, 4, grid.DepartureJd(i), grid.TofDays(j), c3, vInf)) {
                const float stored = grid.c3[static_cast<std::size_t>(j * 31 + i)];
                EXPECT_NEAR(stored, c3, 1e-3 * std::max(1.0, c3));
            }
        }
    }

    // The 2020 window's cheapest departure is in July–August with C3 near 13–16 km^2/s^2
    // (the planetary-protection-free optimum; Mars 2020 flew at 14.49).
    EXPECT_GT(grid.bestDepartureJd, Jd(2020, 7, 1));
    EXPECT_LT(grid.bestDepartureJd, Jd(2020, 8, 31));
    EXPECT_NEAR(std::sqrt(grid.bestC3), std::sqrt(14.49), 0.35);

    const std::string json = Flight::Porkchop::ToJson(grid);
    EXPECT_NE(json.find("\"best\":{"), std::string::npos);
    EXPECT_NE(json.find("\"c3\":["), std::string::npos);
}

TEST(Porkchop, SameBodyIsEmpty) {
    Flight::Porkchop::Request request;
    request.departureBody = 4;
    request.arrivalBody = 4;
    const auto grid = Flight::Porkchop::Compute(request);
    EXPECT_LT(grid.bestDeparture, 0);
}

// --- Propagators ---------------------------------------------------------------------------

TEST(Propagator, KeplerConservesEnergyAndReturnsAfterAPeriod) {
    const Flight::StateVector leo{{7000.0, 0.0, 0.0}, {0.0, 7.2, 1.0}};
    const Flight::Elements e = Flight::ElementsOf(leo, kMuEarth);
    const double period = 2.0 * Flight::kPi * std::sqrt(e.a * e.a * e.a / kMuEarth);
    const Flight::StateVector back = Flight::KeplerPropagate(leo, kMuEarth, period);
    ExpectVecNear(back.r, leo.r, 1e-3);
    const Flight::StateVector half = Flight::KeplerPropagate(leo, kMuEarth, 0.37 * period);
    EXPECT_NEAR(Flight::ElementsOf(half, kMuEarth).energy, e.energy, 1e-9);
    // Backwards undoes forwards; hyperbolas work too.
    const Flight::StateVector hyper{{7000.0, 0.0, 0.0}, {0.0, 12.0, 0.0}};
    const Flight::StateVector out = Flight::KeplerPropagate(hyper, kMuEarth, 86400.0);
    const Flight::StateVector in = Flight::KeplerPropagate(out, kMuEarth, -86400.0);
    ExpectVecNear(in.r, hyper.r, 1e-3);
    ExpectVecNear(in.v, hyper.v, 1e-8);
}

TEST(Propagator, DopriMatchesKeplerOverAMonth) {
    // A craft on Mars's orbit but 20 degrees behind it, against the full ephemeris: over a month
    // the planets' pulls move it by well under 2000 km from the two-body arc, so the integrator
    // and Kepler agree to that.
    const double jd0 = Jd(2026, 1, 1);
    const Flight::StateVector mars = Flight::PlanetState(4, jd0);
    const double a = -0.349; // 20 degrees about +z
    const auto rot = [a](const glm::dvec3& v) {
        return glm::dvec3(std::cos(a) * v.x - std::sin(a) * v.y, std::sin(a) * v.x + std::cos(a) * v.y, v.z);
    };
    const Flight::StateVector start{rot(mars.r), rot(mars.v)};
    const Flight::NBodyResult n = Flight::PropagateNBody(start, jd0, jd0 + 30.0);
    ASSERT_TRUE(n.ok);
    const Flight::StateVector k = Flight::KeplerPropagate(start, Flight::kMuSun, 30.0 * Flight::kSecondsPerDay);
    EXPECT_LT(glm::length(n.state.r - k.r), 2000.0);
    EXPECT_GT(n.steps, 5);
    // Started on a body's centre it reports failure instead of spinning.
    EXPECT_FALSE(Flight::PropagateNBody(mars, jd0, jd0 + 1.0).ok);
}

TEST(Propagator, NBodyTransferStillReachesMars) {
    // The patched-conic Mars 2020 arc, flown from Earth's SOI exit on its escape hyperbola
    // against the full ephemeris, still passes Mars within a few of its SOI radii: the
    // two-body design is a sane first guess (real missions fly TCMs to null the rest).
    const Flight::Plan plan = Flight::PlanTransfer(3, 4, Jd(2020, 7, 30, 11.83), 203.4);
    ASSERT_TRUE(plan.ok);
    double exitJd = 0.0;
    ASSERT_TRUE(Flight::FindSoiCrossing(plan.legs[0].start, plan.StartJd(), plan.EndJd(), 3, false, exitJd));
    EXPECT_GT(exitJd - plan.StartJd(), 2.0);
    EXPECT_LT(exitJd - plan.StartJd(), 6.0);
    double entryJd = 0.0;
    ASSERT_TRUE(Flight::FindSoiCrossing(plan.StateAt(exitJd), exitJd, plan.EndJd() + 1.0, 4, true, entryJd));
    EXPECT_LT(plan.EndJd() - entryJd, 3.0);

    const Flight::NBodyCheck check = Flight::CheckLegNBody(plan, 0);
    ASSERT_TRUE(check.ok);
    EXPECT_LT(check.missInSoiRadii, 5.0);
    LOG_INFO("Flight", "Mars 2020 n-body check: closest approach %.0f km (%.2f SOI) on JD %.2f, %d steps",
             check.closestApproachKm, check.missInSoiRadii, check.closestApproachJd, check.steps);
}

TEST(Propagator, FlybyGeometry) {
    // A symmetric 60 degree turn at v∞ = 10 km/s past Jupiter.
    const double mu = Flight::FindBody(5)->mu;
    const glm::dvec3 in(10.0, 0.0, 0.0);
    const glm::dvec3 out(10.0 * std::cos(Flight::kPi / 3.0), 10.0 * std::sin(Flight::kPi / 3.0), 0.0);
    const Flight::Flyby f = Flight::FlybyGeometry(in, out, mu, 71492.0, 0.0);
    EXPECT_NEAR(f.turnAngleRad, Flight::kPi / 3.0, 1e-9);
    const double rpExpected = mu / 100.0 * (1.0 / std::sin(Flight::kPi / 6.0) - 1.0);
    EXPECT_NEAR(f.periapsisKm, rpExpected, 1e-3 * rpExpected);
    EXPECT_NEAR(f.poweredDeltaVKmS, 0.0, 1e-9);
    // B lies across S, opposite the way the flyby bends v∞: here along -y, i.e. -T... in the ecliptic.
    EXPECT_NEAR(std::hypot(f.bDotTKm, f.bDotRKm), f.bMagnitudeKm, 1e-6 * f.bMagnitudeKm);
    EXPECT_NEAR(f.bDotRKm, 0.0, 1e-6 * f.bMagnitudeKm);
    EXPECT_TRUE(f.feasible);
}

TEST(Propagator, DepartureDeltaV) {
    // v∞ = 2.945 km/s from a 200 km LEO (Hohmann to Mars): ~3.6 km/s.
    EXPECT_NEAR(Flight::HyperbolicDeltaV(kMuEarth, 6378.137 + 200.0, 2.945), 3.61, 0.02);
}

// --- Real missions -------------------------------------------------------------------------

TEST(Missions, PresetsPlan) {
    for (const Flight::Preset& preset : Flight::Presets()) {
        const Flight::Plan plan = Flight::PlanPreset(preset);
        ASSERT_TRUE(plan.ok) << preset.id << ": " << plan.error;
        EXPECT_EQ(plan.legs.size() + 1, preset.encounters.size());
        for (const auto& e : plan.encounters) {
            if (e.kind == Flight::EncounterReport::Kind::Flyby) {
                LOG_INFO(
                    "Flight",
                    "%s %s flyby: v∞ %.2f/%.2f km/s, turn %.1f°, alt %.0f km (historic %.0f), powered Δv %.3f km/s",
                    preset.id, Flight::FindBody(e.body)->name, e.flyby.vInfInKmS, e.flyby.vInfOutKmS,
                    e.flyby.turnAngleRad * 180.0 / Flight::kPi, e.flyby.altitudeKm, e.historicAltitudeKm,
                    e.flyby.poweredDeltaVKmS);
            }
        }
        LOG_INFO("Flight", "%s: C3 %.1f km^2/s^2, total Δv %.2f km/s", preset.id, plan.c3, plan.totalDeltaVKmS);
        EXPECT_FALSE(Flight::PlanJson(plan).empty());
    }
}

TEST(Missions, Voyager2IsNearlyBallistic) {
    // The Grand Tour was flown with small TCMs only: the rebuilt flybys need little powered Δv
    // and their launch C3 is Voyager's (~102 km^2/s^2 on the Titan-Centaur).
    const Flight::Plan plan = Flight::PlanPreset(*Flight::FindPreset("voyager2"));
    ASSERT_TRUE(plan.ok);
    EXPECT_NEAR(plan.c3, 102.0, 15.0);
    for (const auto& e : plan.encounters) {
        if (e.kind == Flight::EncounterReport::Kind::Flyby) {
            EXPECT_LT(e.flyby.poweredDeltaVKmS, 1.0) << Flight::FindBody(e.body)->name;
        }
    }
}

TEST(Missions, ResidualsAgainstTheStoredPaths) {
    MissionCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(MissionCatalog::LoadFromFile(std::string(SOLARSYSTEM_SOURCE_DIR) + "/resource/missions/catalog.json",
                                             catalog, error))
        << error;
    for (const char* id : {"voyager1", "voyager2"}) {
        const int index = MissionCatalog::IndexById(catalog, id);
        ASSERT_GE(index, 0) << id;
        const Flight::Plan plan = Flight::PlanPreset(*Flight::FindPreset(id));
        const Flight::Residuals r = Flight::CompareWithMission(plan, catalog.missions[static_cast<std::size_t>(index)]);
        LOG_INFO("Flight", "%s vs stored path: rms %.3f AU, max %.3f AU, %d samples", id, r.rmsAu, r.maxAu, r.samples);
        ASSERT_GT(r.samples, 0);
        // The stored path is a spline through the same encounter positions: they agree at the
        // planets and differ between them by a fraction of the leg length.
        EXPECT_LT(r.rmsAu, 1.5);
    }
}

// --- The craft in the scene ----------------------------------------------------------------

TEST(Spacecraft, ArrivesOnTheDrawnPlanetInBothScaleModes) {
    OrbitLayout::ResetForTests();
    const Flight::Plan plan = Flight::PlanTransfer(3, 4, Jd(2026, 11, 1), 210.0);
    ASSERT_TRUE(plan.ok);
    const Flight::Spacecraft craft({}, plan);
    for (const auto mode : {OrbitLayout::ScaleMode::Compressed, OrbitLayout::ScaleMode::Realistic}) {
        OrbitLayout::SetScaleMode(mode);
        for (const auto& [jd, body] :
             {std::pair{plan.StartJd(), OrbitLayout::Body::Earth}, std::pair{plan.EndJd(), OrbitLayout::Body::Mars},
              std::pair{plan.EndJd() + 30.0, OrbitLayout::Body::Mars}}) {
            OrbitLayout::SetJulianDate(jd);
            const glm::dvec3 planet = OrbitLayout::GetHeliocentricScenePosition(body);
            const glm::dvec3 ship = craft.WorldPositionAt(jd);
            EXPECT_LT(glm::length(ship - planet), 1e-6 * glm::length(planet))
                << "mode " << static_cast<int>(mode) << " jd " << jd;
        }
        // Mid-cruise it is between the orbits and away from both planets.
        const double mid = 0.5 * (plan.StartJd() + plan.EndJd());
        OrbitLayout::SetJulianDate(mid);
        const double r = glm::length(craft.WorldPositionAt(mid));
        EXPECT_GT(r, OrbitLayout::GetOrbitRadiusDouble(OrbitLayout::Body::Earth) * 0.95);
        EXPECT_LT(r, OrbitLayout::GetOrbitRadiusDouble(OrbitLayout::Body::Mars) * 1.05);
        EXPECT_EQ(craft.PhaseAt(mid), Flight::Spacecraft::Phase::Cruise);
    }
    // Realistic scale stretches by no more than the planets' eccentricities.
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    EXPECT_NEAR(Flight::SceneStretch(plan, plan.EndJd()), 1.0, 0.1);
    const auto path = craft.PathScene(64);
    EXPECT_EQ(path.size(), 64u);
    OrbitLayout::ResetForTests();
}
