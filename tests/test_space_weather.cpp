#include <gtest/gtest.h>

#include "astro/Ephemeris.h"
#include "astro/SpaceWeather.h"
#include "core/Json.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

SpaceWeather::CmeCatalog ParseCatalog(const std::string& text) {
    Json::Value document;
    std::string error;
    EXPECT_TRUE(Json::Parse(text, document, error)) << error;
    SpaceWeather::CmeCatalog catalog;
    EXPECT_TRUE(SpaceWeather::ParseCmeCatalog(document, catalog, error)) << error;
    return catalog;
}

const char* kHalloween = R"({"events":[{
    "id": "2003-10-28-halloween",
    "name": "Halloween storm",
    "launch": "2003-10-28T11:30Z",
    "speedKmS": 2459,
    "halfAngleDeg": 60,
    "latitudeDeg": -16,
    "longitudeDeg": -8,
    "earthArrival": "2003-10-29T06:11Z",
    "kpMax": 9
}]})";

std::string RepoPath(const char* relative) {
    return std::string(SOLARSYSTEM_SOURCE_DIR) + "/" + relative;
}

/** Heliocentric ecliptic (lon, lat, r) of every magnetosphere body at `jd`. */
std::vector<glm::dvec3> Positions(double jd) {
    std::vector<glm::dvec3> positions;
    for (const SpaceWeather::Magnetosphere& m : SpaceWeather::Magnetospheres()) {
        const Ephemeris::HelioLB p = Ephemeris::Position(static_cast<int>(m.body), jd);
        positions.emplace_back(p.lonRad, p.latRad, p.rAu);
    }
    return positions;
}

} // namespace

// --- Standoff distances ----------------------------------------------------------------------

TEST(SpaceWeatherTest, QuietWindPressureAtEarthIsAboutOneNanopascal) {
    // 5 protons/cm³ at 400 km/s: 1.34 nPa (the OMNI median is ~1.5–2 with alphas).
    EXPECT_NEAR(SpaceWeather::DynamicPressureNPa(5.0, 400.0), 1.34, 0.01);
}

TEST(SpaceWeatherTest, EarthQuietStandoffIsTenEarthRadii) {
    const auto* earth = SpaceWeather::FindMagnetosphere(OrbitLayout::Body::Earth);
    ASSERT_NE(earth, nullptr);
    const double standoff = SpaceWeather::StandoffRadii(earth->surfaceFieldTesla, SpaceWeather::QuietPressureNPa(1.0));
    EXPECT_NEAR(standoff, 10.0, 0.6);
}

TEST(SpaceWeatherTest, StandoffFallsAsTheSixthRootOfPressure) {
    const double quiet = SpaceWeather::StandoffRadii(3.12e-5, 1.0);
    const double pushed = SpaceWeather::StandoffRadii(3.12e-5, 64.0);
    EXPECT_NEAR(pushed / quiet, 0.5, 1e-9);
}

TEST(SpaceWeatherTest, JupiterStandoffIsSixtyToNinetyJovianRadii) {
    const auto* jupiter = SpaceWeather::FindMagnetosphere(OrbitLayout::Body::Jupiter);
    ASSERT_NE(jupiter, nullptr);
    const double standoff = SpaceWeather::StandoffRadii(jupiter->surfaceFieldTesla, SpaceWeather::QuietPressureNPa(5.2),
                                                        jupiter->inflation);
    EXPECT_GE(standoff, 60.0);
    EXPECT_LE(standoff, 90.0);
}

TEST(SpaceWeatherTest, OtherStandoffsMatchSpacecraftCrossings) {
    // Mercury ~1.4–1.5 R_M (MESSENGER), Saturn ~20–25 R_S (Cassini), Neptune ~23–26 R_N
    // (Voyager 2). Uranus measured 18 R_U, but in a rarefied wind; the median wind gives ~25.
    const auto standoffAt = [](OrbitLayout::Body body, double rAu) {
        const auto* m = SpaceWeather::FindMagnetosphere(body);
        return SpaceWeather::StandoffRadii(m->surfaceFieldTesla, SpaceWeather::QuietPressureNPa(rAu), m->inflation);
    };
    EXPECT_NEAR(standoffAt(OrbitLayout::Body::Mercury, 0.387), 1.45, 0.25);
    EXPECT_NEAR(standoffAt(OrbitLayout::Body::Saturn, 9.54), 22.0, 4.0);
    EXPECT_NEAR(standoffAt(OrbitLayout::Body::Uranus, 19.2), 23.0, 5.0);
    EXPECT_NEAR(standoffAt(OrbitLayout::Body::Neptune, 30.1), 24.0, 4.0);
}

TEST(SpaceWeatherTest, ShueMagnetopauseFlaresTowardTheTail) {
    const double alpha = SpaceWeather::ShueFlaringExponent(2.0);
    EXPECT_NEAR(alpha, 0.59, 0.01);
    EXPECT_DOUBLE_EQ(SpaceWeather::MagnetopauseRadius(10.0, alpha, 1.0), 10.0);
    // At the terminator r = r₀·2^α: ~15 R_E for Earth.
    EXPECT_NEAR(SpaceWeather::MagnetopauseRadius(10.0, alpha, 0.0), 10.0 * std::pow(2.0, alpha), 1e-9);
    EXPECT_GT(SpaceWeather::MagnetopauseRadius(10.0, alpha, -0.9), 20.0);
}

// --- Parker spiral ---------------------------------------------------------------------------

TEST(SpaceWeatherTest, ParkerSpiralIsFortyFiveDegreesAtEarth) {
    EXPECT_NEAR(SpaceWeather::ParkerSpiralAngleRad(1.0, 400.0) / kDeg, 45.0, 3.0);
    // Faster wind, straighter spiral; near-azimuthal by Jupiter.
    EXPECT_LT(SpaceWeather::ParkerSpiralAngleRad(1.0, 750.0), SpaceWeather::ParkerSpiralAngleRad(1.0, 400.0));
    EXPECT_GT(SpaceWeather::ParkerSpiralAngleRad(5.2, 400.0) / kDeg, 78.0);
    // Over the poles the field is radial.
    EXPECT_NEAR(SpaceWeather::ParkerSpiralAngleRad(1.0, 400.0, 89.9 * kDeg) / kDeg, 0.0, 0.5);
}

// --- Ovals and Kp ----------------------------------------------------------------------------

TEST(SpaceWeatherTest, EarthOvalMovesEquatorwardWithKp) {
    const SpaceWeather::Oval quiet = SpaceWeather::EarthOval(2.0);
    const SpaceWeather::Oval storm = SpaceWeather::EarthOval(9.0);
    // Feldstein–Starkov: night-side equatorward edge ~62° at Kp 2, ~48–50° at Kp 9.
    EXPECT_NEAR(quiet.equatorwardMidnightDeg, 62.0, 1.5);
    EXPECT_NEAR(storm.equatorwardMidnightDeg, 49.0, 1.5);
    // The day side sits poleward of the night side, and the band keeps its order.
    for (double kp = 0.0; kp <= 9.0; kp += 1.0) {
        const SpaceWeather::Oval oval = SpaceWeather::EarthOval(kp);
        EXPECT_GT(oval.equatorwardNoonDeg, oval.equatorwardMidnightDeg) << kp;
        EXPECT_GT(oval.polewardMidnightDeg, oval.equatorwardMidnightDeg) << kp;
        EXPECT_GT(oval.polewardNoonDeg, oval.equatorwardNoonDeg) << kp;
        if (kp > 0.0) {
            EXPECT_LT(oval.equatorwardMidnightDeg, SpaceWeather::EarthOval(kp - 1.0).equatorwardMidnightDeg) << kp;
        }
    }
}

TEST(SpaceWeatherTest, OvalBoundaryInterpolatesNoonToMidnight) {
    const SpaceWeather::Oval oval = SpaceWeather::EarthOval(3.0);
    const double pi = 3.14159265358979323846;
    EXPECT_DOUBLE_EQ(SpaceWeather::EquatorwardLatitudeDeg(oval, 0.0), oval.equatorwardNoonDeg);
    EXPECT_DOUBLE_EQ(SpaceWeather::EquatorwardLatitudeDeg(oval, pi), oval.equatorwardMidnightDeg);
    const double dawn = SpaceWeather::EquatorwardLatitudeDeg(oval, 0.5 * pi);
    EXPECT_NEAR(dawn, 0.5 * (oval.equatorwardNoonDeg + oval.equatorwardMidnightDeg), 1e-9);
}

TEST(SpaceWeatherTest, QuietWindGivesTheBaselineKp) {
    const double quiet = SpaceWeather::QuietPressureNPa(1.0);
    EXPECT_NEAR(SpaceWeather::KpFromCoupling(SpaceWeather::CouplingRatio(quiet, quiet, 400.0)), 2.0, 1e-9);
    // A fast stream is a minor disturbance; a big CME saturates the index.
    const double stream = SpaceWeather::DynamicPressureNPa(2.5, 750.0);
    const double kpStream = SpaceWeather::KpFromCoupling(SpaceWeather::CouplingRatio(stream, quiet, 750.0));
    EXPECT_GT(kpStream, 2.5);
    EXPECT_LT(kpStream, 4.5);
    const double cme = SpaceWeather::DynamicPressureNPa(25.0, 2000.0);
    EXPECT_DOUBLE_EQ(SpaceWeather::KpFromCoupling(SpaceWeather::CouplingRatio(cme, quiet, 2000.0)), 9.0);
}

// --- Sunspot cycle ---------------------------------------------------------------------------

TEST(SpaceWeatherTest, SunspotCycleReachesMaximumIn2014) {
    const SpaceWeather::CycleParams sun = SpaceWeather::SolarCycle();
    // Cycle 24's smoothed maximum: April 2014, 116.4 (SILSO v2).
    const double april2014 = SpaceWeather::JulianDateFromDecimalYear(2014.3);
    const SpaceWeather::CycleState peak = SpaceWeather::EvaluateCycle(sun, april2014);
    EXPECT_EQ(peak.cycleNumber, 24);
    EXPECT_NEAR(peak.phase, 0.49, 0.02);
    EXPECT_NEAR(peak.sunspotNumber, 116.4, 10.0);

    // The modelled curve peaks within half a year of the observed maximum.
    double bestYear = 0.0;
    double best = 0.0;
    for (double year = 2009.0; year < 2019.0; year += 0.05) {
        const double r = SpaceWeather::EvaluateCycle(sun, SpaceWeather::JulianDateFromDecimalYear(year)).sunspotNumber;
        if (r > best) {
            best = r;
            bestYear = year;
        }
    }
    EXPECT_NEAR(bestYear, 2014.3, 0.5);

    // Minima are spotless or nearly; the previous cycle's tail is all that is left.
    const SpaceWeather::CycleState minimum =
        SpaceWeather::EvaluateCycle(sun, SpaceWeather::JulianDateFromDecimalYear(2008.95));
    EXPECT_EQ(minimum.cycleNumber, 24);
    EXPECT_LT(minimum.sunspotNumber, 15.0);
    EXPECT_GT(minimum.previous.coverage, minimum.current.coverage);
}

TEST(SpaceWeatherTest, SporerLawMovesSpotsEquatorward) {
    EXPECT_NEAR(SpaceWeather::SporerLatitudeDeg(0.0), 28.0, 1e-9);
    // At maximum the band is near 15°, by the end of a cycle ~8°.
    EXPECT_NEAR(SpaceWeather::SporerLatitudeDeg(5.0), 14.5, 2.0);
    EXPECT_NEAR(SpaceWeather::SporerLatitudeDeg(10.5), 6.5, 2.0);
    const SpaceWeather::CycleState rising =
        SpaceWeather::EvaluateCycle(SpaceWeather::SolarCycle(), SpaceWeather::JulianDateFromDecimalYear(2011.0));
    const SpaceWeather::CycleState declining =
        SpaceWeather::EvaluateCycle(SpaceWeather::SolarCycle(), SpaceWeather::JulianDateFromDecimalYear(2017.0));
    EXPECT_GT(rising.current.latitudeDeg, declining.current.latitudeDeg);
}

TEST(SpaceWeatherTest, AStarWithNoCycleHasNoSpots) {
    const SpaceWeather::CycleState none = SpaceWeather::EvaluateCycle(SpaceWeather::CycleParams{}, 2460000.0);
    EXPECT_EQ(none.sunspotNumber, 0.0);
    EXPECT_EQ(none.current.coverage, 0.0);
}

// --- CMEs ------------------------------------------------------------------------------------

TEST(SpaceWeatherTest, ParsesIsoTimestamps) {
    EXPECT_NEAR(SpaceWeather::ParseIsoUtc("2000-01-01T12:00Z"), Ephemeris::kJ2000, 1e-9);
    EXPECT_NEAR(SpaceWeather::ParseIsoUtc("2000-01-01T12:00:00Z"), Ephemeris::kJ2000, 1e-9);
    EXPECT_NEAR(SpaceWeather::ParseIsoUtc("2000-01-01T12:00:00.000Z"), Ephemeris::kJ2000, 1e-9);
    EXPECT_NEAR(SpaceWeather::ParseIsoUtc("2000-01-02"), Ephemeris::kJ2000 + 0.5, 1e-9);
    EXPECT_EQ(SpaceWeather::ParseIsoUtc("yesterday"), 0.0);
    EXPECT_EQ(SpaceWeather::ParseIsoUtc("2000-13-01"), 0.0);
}

TEST(SpaceWeatherTest, DragSlowsAFastCmeTowardTheWind) {
    const double v0 = 2000.0;
    const double w = 400.0;
    const double gamma = 1.0e-7;
    const double day = 86400.0;
    EXPECT_NEAR(SpaceWeather::DragSpeedKmS(v0, w, gamma, 0.0), v0, 1e-9);
    const double later = SpaceWeather::DragSpeedKmS(v0, w, gamma, day);
    EXPECT_LT(later, v0);
    EXPECT_GT(later, w);
    // The front is the integral of the speed.
    const double dt = 1.0;
    const double r0 = SpaceWeather::DragFrontKm(v0, w, gamma, 0.0, day);
    const double r1 = SpaceWeather::DragFrontKm(v0, w, gamma, 0.0, day + dt);
    EXPECT_NEAR((r1 - r0) / dt, later, 1.0);
    // No drag: ballistic.
    EXPECT_NEAR(SpaceWeather::DragFrontKm(v0, w, 0.0, 1000.0, day), 1000.0 + v0 * day, 1e-6);
}

TEST(SpaceWeatherTest, HalloweenCmeArrivesWhenObservedAndSaturatesKp) {
    const SpaceWeather::CmeCatalog catalog = ParseCatalog(kHalloween);
    ASSERT_EQ(catalog.events.size(), 1u);
    const SpaceWeather::Cme& cme = catalog.events.front();
    EXPECT_TRUE(cme.hitsEarth);
    // The drag fit lands the front on Earth within minutes of the 06:11 sudden commencement.
    const double arrival = SpaceWeather::ParseIsoUtc("2003-10-29T06:11Z");
    EXPECT_NEAR(cme.modelArrivalJd, arrival, 0.75 / 24.0);
    EXPECT_GT(cme.dragPerKm, 0.0);
    EXPECT_GE(cme.modelKpMax, 8.5);

    SpaceWeather::State state;
    state.SetCatalog(catalog);
    // Before launch: quiet.
    const double before = SpaceWeather::ParseIsoUtc("2003-10-27T00:00Z");
    state.Evaluate(before, Positions(before));
    EXPECT_TRUE(state.ActiveCmes().empty());
    EXPECT_LT(state.Kp(), 4.5);
    EXPECT_EQ(state.NextStorm(before), 0);

    // An hour after the modelled shock the dayside is already compressed (the field lines
    // follow standoff / quiet standoff, magneticFieldLine.vs) and the oval is a storm oval.
    const double hourIn = cme.modelArrivalJd + 1.0 / 24.0;
    state.Evaluate(hourIn, Positions(hourIn));
    const SpaceWeather::BodyState* earthHourIn = state.Body(OrbitLayout::Body::Earth);
    ASSERT_NE(earthHourIn, nullptr);
    EXPECT_LT(earthHourIn->standoffRadii, 0.8 * earthHourIn->quietStandoffRadii);
    EXPECT_LT(earthHourIn->oval.equatorwardMidnightDeg, 56.0);

    // Three hours after arrival Earth is inside the sheath: Kp 9, the oval near 50°, the
    // magnetopause pushed inside geosynchronous orbit (6.6 R_E) — as GOES saw that day.
    const double storm = arrival + 3.0 / 24.0;
    state.Evaluate(storm, Positions(storm));
    ASSERT_EQ(state.ActiveCmes().size(), 1u);
    const SpaceWeather::BodyState* earth = state.Body(OrbitLayout::Body::Earth);
    ASSERT_NE(earth, nullptr);
    EXPECT_GT(earth->wind.cmeStrength, 0.5);
    EXPECT_NEAR(state.Kp(), 9.0, 0.6);
    EXPECT_NEAR(earth->oval.equatorwardMidnightDeg, 50.0, 2.0);
    EXPECT_LT(earth->standoffRadii, 7.0);
    EXPECT_GT(earth->quietStandoffRadii, 9.0);
    EXPECT_EQ(state.PreviousStorm(storm), 0);
    EXPECT_EQ(state.NextStorm(storm), -1);

    // A week on, the front is past Mars and Earth is quiet again.
    const double after = arrival + 7.0;
    state.Evaluate(after, Positions(after));
    EXPECT_LT(state.Body(OrbitLayout::Body::Earth)->wind.cmeStrength, 0.01);
    EXPECT_LT(state.Kp(), 5.0);
}

TEST(SpaceWeatherTest, CmeAimedAwayFromEarthMissesIt) {
    // The July 2012 event went off toward STEREO-A, 120° west of Earth.
    const SpaceWeather::CmeCatalog catalog = ParseCatalog(R"({"events":[{
        "id": "2012-07-23", "launch": "2012-07-23T02:08Z", "speedKmS": 3050,
        "halfAngleDeg": 45, "longitudeDeg": 125, "latitudeDeg": 0
    }]})");
    ASSERT_EQ(catalog.events.size(), 1u);
    EXPECT_FALSE(catalog.events.front().hitsEarth);
    EXPECT_LT(catalog.events.front().PeakKp(), 0.0);
}

TEST(SpaceWeatherTest, KpOverrideHoldsEveryOval) {
    SpaceWeather::State state;
    state.SetKpOverride(7.0);
    const double jd = Ephemeris::kJ2000;
    state.Evaluate(jd, Positions(jd));
    EXPECT_DOUBLE_EQ(state.Kp(), 7.0);
    for (const SpaceWeather::BodyState& body : state.Bodies()) {
        EXPECT_DOUBLE_EQ(body.kp, 7.0) << body.magnetosphere->id;
    }
    state.SetKpOverride(-3.0);
    EXPECT_LT(state.KpOverride(), 0.0);
    state.SetKpOverride(12.0);
    EXPECT_DOUBLE_EQ(state.KpOverride(), 9.0);
}

TEST(SpaceWeatherTest, LaunchedCmeReachesEarthAndRaisesKp) {
    SpaceWeather::State state;
    const double launch = SpaceWeather::ParseIsoUtc("2025-03-01T00:00Z");
    const SpaceWeather::Cme& cme = state.LaunchCme(launch);
    EXPECT_TRUE(cme.hitsEarth);
    EXPECT_GT(cme.modelArrivalJd, launch + 0.5);
    EXPECT_LT(cme.modelArrivalJd, launch + 2.5);
    const double hit = cme.modelArrivalJd + 2.0 / 24.0;
    state.Evaluate(hit, Positions(hit));
    EXPECT_GE(state.Kp(), 6.0);
    // Pointers into the launched list survive more launches.
    const SpaceWeather::Cme* first = &cme;
    for (int i = 0; i < 20; ++i) {
        state.LaunchCme(launch + i);
    }
    EXPECT_EQ(first->launchJd, launch);
}

TEST(SpaceWeatherTest, CatalogSkipsBadRowsAndSortsByLaunch) {
    const SpaceWeather::CmeCatalog catalog = ParseCatalog(R"({"events":[
        {"id": "b", "launch": "2017-09-06T12:24Z", "speedKmS": 1571},
        {"id": "no-speed", "launch": "2017-09-06T12:24Z"},
        {"launch": "2017-09-06T12:24Z", "speedKmS": 1000},
        {"id": "a", "launch": "1859-09-01T11:18Z", "speedKmS": 2380, "earthArrival": "1859-09-02T04:50Z", "kpMax": 9}
    ]})");
    ASSERT_EQ(catalog.events.size(), 2u);
    EXPECT_EQ(catalog.events[0].id, "a");
    EXPECT_EQ(catalog.events[1].id, "b");
    // Carrington: 17.6 hours to Earth, the fastest transit on record.
    EXPECT_NEAR(catalog.events[0].modelArrivalJd, SpaceWeather::ParseIsoUtc("1859-09-02T04:50Z"), 1.0 / 24.0);
}

TEST(SpaceWeatherTest, JsonCarriesKpStandoffsAndStorms) {
    SpaceWeather::State state;
    state.SetCatalog(ParseCatalog(kHalloween));
    const double jd = SpaceWeather::ParseIsoUtc("2003-10-29T09:00Z");
    state.Evaluate(jd, Positions(jd));
    const std::string json = state.ToJson(R"("enabled":true)");
    Json::Value value;
    std::string error;
    ASSERT_TRUE(Json::Parse(json, value, error)) << error << "\n" << json;
    EXPECT_TRUE(value["enabled"].AsBool());
    EXPECT_GT(value["kp"].AsNumber(), 8.0);
    EXPECT_EQ(value["cme"]["id"].AsString(""), "2003-10-28-halloween");
    EXPECT_EQ(value["bodies"].Size(), SpaceWeather::Magnetospheres().size());
    EXPECT_EQ(value["bodies"][1]["id"].AsString(""), "earth");
    EXPECT_LT(value["bodies"][1]["standoffRadii"].AsNumber(), 7.0);
    EXPECT_TRUE(value["lastStorm"].IsObject());
    EXPECT_TRUE(value["nextStorm"].IsNull());
}

TEST(SpaceWeatherTest, CommittedCmeListCalibratesToEveryObservedArrival) {
    SpaceWeather::CmeCatalog catalog;
    std::string error;
    ASSERT_TRUE(SpaceWeather::LoadCmeCatalog(RepoPath("resource/spaceweather/cmes.json"), catalog, error)) << error;
    ASSERT_GE(catalog.events.size(), 10u);
    int observed = 0;
    for (const SpaceWeather::Cme& cme : catalog.events) {
        EXPECT_GT(cme.modelSpeedKmS, 0.0) << cme.id;
        if (cme.observedArrivalJd <= 0.0) {
            continue;
        }
        ++observed;
        // The fitted front reaches Earth at the observed sudden commencement…
        EXPECT_NEAR(cme.modelArrivalJd, cme.observedArrivalJd, 1.0 / 24.0) << cme.id;
        EXPECT_TRUE(cme.hitsEarth) << cme.id;
        // …and the storm it drives peaks near the observed Kp.
        if (cme.kpMax >= 0.0) {
            EXPECT_NEAR(cme.modelKpMax, cme.kpMax, 1.0) << cme.id;
        }
    }
    EXPECT_GE(observed, 9);
    // The ticket's acceptance date: the Halloween storm is the event at 2003-10-29T06:00Z.
    SpaceWeather::State state;
    state.SetCatalog(catalog);
    const int index = state.FindEventNear(SpaceWeather::ParseIsoUtc("2003-10-29T06:00Z"), 1.0);
    ASSERT_GE(index, 0);
    EXPECT_EQ(state.Catalog().events[static_cast<std::size_t>(index)].id, "2003-10-28-halloween");
}
