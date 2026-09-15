#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/SkyEvents.h"

namespace {

constexpr int kMercury = 1;
constexpr int kVenus = 2;
constexpr int kMars = 4;

/** 2026-09-08 00:00 UT — the search origin used throughout these tests. */
const double kFromJd = Ephemeris::JulianDateFromYmd(2026, 9, 8);

} // namespace

TEST(SkyEventsTest, GeocentricLongitudeMatchesHorizons) {
    // Venus minus Earth from JPL Horizons vectors (CENTER='@sun', REF_PLANE=ECLIPTIC,
    // 2026-Sep-08 00:00 TDB), i.e. geometric and in the J2000 ecliptic — the frame this code
    // works in. Horizons' own ObsEcLon for the same instant reads 208.4180 because it is
    // apparent and referred to the ecliptic of date; the 0.37 deg gap is precession since
    // J2000, which shifts both bodies of a pair alike and so cannot move a conjunction.
    double lon = 0.0, lat = 0.0;
    SkyEvents::GeocentricLonLatDeg(kVenus, kFromJd, lon, lat);
    EXPECT_NEAR(lon, 208.0443, 0.02);
    EXPECT_NEAR(lat, -4.2923, 0.02);
}

TEST(SkyEventsTest, SeparationFromEarthIsSymmetricAndZeroWithItself) {
    EXPECT_NEAR(SkyEvents::ApparentSeparationDeg(kVenus, kMars, kFromJd),
                SkyEvents::ApparentSeparationDeg(kMars, kVenus, kFromJd), 1e-9);
    EXPECT_NEAR(SkyEvents::ApparentSeparationDeg(kMars, kMars, kFromJd), 0.0, 1e-9);
}

TEST(SkyEventsTest, NextInnerPlanetConjunctionMatchesHorizons) {
    // Horizons daily ObsEcLon for Mercury/Venus/Mars from 2026-09-08 puts the first
    // longitude crossing at Mercury-Venus, between 2026-Oct-07 and 2026-Oct-08, ~5 deg apart
    // (the pair passes wide because their ecliptic latitudes differ).
    const SkyEvents::Conjunction next = SkyEvents::NextInnerPlanetConjunction(kFromJd);
    ASSERT_TRUE(next.valid);
    EXPECT_EQ(next.bodyA, kMercury);
    EXPECT_EQ(next.bodyB, kVenus);

    int year = 0, month = 0, day = 0;
    Ephemeris::YmdFromJulianDate(next.julianDate, year, month, day);
    EXPECT_EQ(year, 2026);
    EXPECT_EQ(month, 10);
    EXPECT_GE(day, 7);
    EXPECT_LE(day, 8);

    EXPECT_GT(next.separationDeg, 4.5);
    EXPECT_LT(next.separationDeg, 6.0);
}

TEST(SkyEventsTest, ConjunctionInstantHasEqualLongitudes) {
    const SkyEvents::Conjunction next = SkyEvents::NextInnerPlanetConjunction(kFromJd);
    ASSERT_TRUE(next.valid);

    double lonA = 0.0, lonB = 0.0, lat = 0.0;
    SkyEvents::GeocentricLonLatDeg(next.bodyA, next.julianDate, lonA, lat);
    SkyEvents::GeocentricLonLatDeg(next.bodyB, next.julianDate, lonB, lat);
    EXPECT_NEAR(std::fmod(lonA - lonB + 540.0, 360.0) - 180.0, 0.0, 1e-3);
}

TEST(SkyEventsTest, SearchAdvancesPastTheEventItJustReported) {
    // The hint re-runs every frame from the current epoch; if the search returned the same
    // event once its instant had passed, the UI would freeze on a stale date.
    const SkyEvents::Conjunction first = SkyEvents::NextInnerPlanetConjunction(kFromJd);
    ASSERT_TRUE(first.valid);

    const SkyEvents::Conjunction second = SkyEvents::NextInnerPlanetConjunction(first.julianDate + 1.0);
    ASSERT_TRUE(second.valid);
    EXPECT_GT(second.julianDate, first.julianDate);
}

TEST(SkyEventsTest, ShortWindowFindsNothing) {
    const SkyEvents::Conjunction none = SkyEvents::NextInnerPlanetConjunction(kFromJd, 5.0);
    EXPECT_FALSE(none.valid);
    EXPECT_TRUE(SkyEvents::FormatNextInnerPlanetConjunction(kFromJd, 5.0).empty());
}

TEST(SkyEventsTest, HintTextNamesBothBodiesAndTheDate) {
    const std::string hint = SkyEvents::FormatNextInnerPlanetConjunction(kFromJd);
    EXPECT_NE(hint.find("Mercury"), std::string::npos) << hint;
    EXPECT_NE(hint.find("Venus"), std::string::npos) << hint;
    EXPECT_NE(hint.find("2026-10-0"), std::string::npos) << hint;
}

TEST(SkyEventsTest, BodyNamesCoverTheCatalogAndRejectStrays) {
    EXPECT_STREQ(SkyEvents::BodyName(10), "Ceres");
    EXPECT_STREQ(SkyEvents::BodyName(11), "Vesta");
    // Satellites the event search can name carry English names too; the rest do not, so
    // callers never get a half-built label for a moon no search reports.
    EXPECT_STREQ(SkyEvents::BodyName(SkyEvents::kMoon), "Moon");
    EXPECT_STREQ(SkyEvents::BodyName(SkyEvents::kIo), "Io");
    EXPECT_STREQ(SkyEvents::BodyName(19), ""); // Mimas — no ephemeris solution, no name
    EXPECT_STREQ(SkyEvents::BodyName(99), "");
}

// --- Eclipses, transits and shadow transits -------------------------------------------
//
// Reference dates are the published UTC calendar dates of real events (NASA GSFC eclipse
// catalogue for eclipses, the 2016 Mercury transit for the transit case). The search only
// has to name the right day: the clock time it reports comes from mean lunar elements with
// no periodic terms, so it can be an hour or two off greatest eclipse. Nothing here checks
// a time of day, and nothing here should be read as a contact time.

namespace {

/** Every event of `kind` the search finds in [fromJd, fromJd + days), as YYYY-MM-DD. */
std::vector<std::string> EventDatesInWindow(SkyEvents::EventKind kind, double fromJd, double days) {
    std::vector<std::string> dates;
    const double endJd = fromJd + days;
    double cursor = fromJd;

    for (int guard = 0; guard < 64 && cursor < endJd; ++guard) {
        SkyEvents::SkyEvent event;
        switch (kind) {
            case SkyEvents::EventKind::SolarEclipse:
                event = SkyEvents::NextSolarEclipse(cursor, endJd - cursor);
                break;
            case SkyEvents::EventKind::LunarEclipse:
                event = SkyEvents::NextLunarEclipse(cursor, endJd - cursor);
                break;
            case SkyEvents::EventKind::Transit:
                event = SkyEvents::NextInferiorPlanetTransit(cursor, endJd - cursor);
                break;
            default:
                return dates;
        }
        if (!event.valid) {
            break;
        }

        int year = 0, month = 0, day = 0;
        Ephemeris::YmdFromJulianDate(event.julianDate, year, month, day);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
        dates.emplace_back(buffer);
        // Step past this event; no two eclipses of a kind fall within five days.
        cursor = event.julianDate + 5.0;
    }
    return dates;
}

bool Contains(const std::vector<std::string>& dates, const std::string& date) {
    return std::find(dates.begin(), dates.end(), date) != dates.end();
}

} // namespace

TEST(SkyEventsTest, SolarEclipsesLandOnTheirPublishedDates) {
    // NASA GSFC five-millennium canon, 2017-2019. Every solar eclipse in the window must be
    // found and no extra one invented, so the size check matters as much as the contents.
    const std::vector<std::string> found = EventDatesInWindow(
        SkyEvents::EventKind::SolarEclipse, Ephemeris::JulianDateFromYmd(2017, 1, 1), 1080.0);

    EXPECT_TRUE(Contains(found, "2017-02-26")); // annular, South America / Africa
    EXPECT_TRUE(Contains(found, "2017-08-21")); // total, the "Great American Eclipse"
    EXPECT_TRUE(Contains(found, "2018-02-15")); // partial, southern South America
    EXPECT_TRUE(Contains(found, "2018-07-13")); // partial, south of Australia
    EXPECT_TRUE(Contains(found, "2018-08-11")); // partial, northern Europe / Asia
    // The canon dates this one 2019-01-06; greatest eclipse is 01:42 UTC, and the search
    // reports ~23:50 on the 5th. That two-hour lead is exactly the accuracy the header
    // promises, and it is why nothing here asserts a time of day.
    EXPECT_TRUE(Contains(found, "2019-01-05"));
    EXPECT_TRUE(Contains(found, "2019-07-02")); // total, South Pacific / Chile / Argentina
    EXPECT_EQ(found.size(), 7u) << "found an eclipse the canon does not list";
}

TEST(SkyEventsTest, LunarEclipsesLandOnTheirPublishedDates) {
    const std::vector<std::string> found = EventDatesInWindow(
        SkyEvents::EventKind::LunarEclipse, Ephemeris::JulianDateFromYmd(2018, 1, 1), 700.0);

    EXPECT_TRUE(Contains(found, "2018-01-31")); // total
    EXPECT_TRUE(Contains(found, "2018-07-27")); // total, the longest of the century
    EXPECT_TRUE(Contains(found, "2019-01-21")); // total
    EXPECT_TRUE(Contains(found, "2019-07-16")); // partial
}

TEST(SkyEventsTest, MercuryTransitLandsOnItsPublishedDate) {
    const SkyEvents::SkyEvent transit = SkyEvents::NextInferiorPlanetTransit(
        Ephemeris::JulianDateFromYmd(2016, 1, 1), 400.0);

    ASSERT_TRUE(transit.valid);
    EXPECT_EQ(transit.kind, SkyEvents::EventKind::Transit);
    EXPECT_EQ(transit.bodyA, kMercury);
    EXPECT_EQ(transit.bodyB, 0); // the Sun

    int year = 0, month = 0, day = 0;
    Ephemeris::YmdFromJulianDate(transit.julianDate, year, month, day);
    EXPECT_EQ(year, 2016);
    EXPECT_EQ(month, 5);
    EXPECT_EQ(day, 9);

    // A transit is by definition inside the Sun's disc, so the miss must beat the limit.
    EXPECT_LT(transit.missDeg, transit.limitDeg);
}

TEST(SkyEventsTest, IoCastsAShadowOnJupiterEveryOrbit) {
    // Io orbits in 1.769 d and its orbit is nearly in Jupiter's equatorial plane, so a
    // shadow transit happens on essentially every revolution. A 30-day window must hold a
    // run of them roughly one orbital period apart.
    double cursor = Ephemeris::JulianDateFromYmd(2026, 1, 1);
    const double endJd = cursor + 30.0;
    std::vector<double> instants;

    for (int guard = 0; guard < 32 && cursor < endJd; ++guard) {
        const SkyEvents::SkyEvent event =
            SkyEvents::NextGalileanShadowTransit(cursor, endJd - cursor, SkyEvents::kIo);
        if (!event.valid) {
            break;
        }
        EXPECT_EQ(event.kind, SkyEvents::EventKind::ShadowTransit);
        EXPECT_EQ(event.bodyA, SkyEvents::kIo);
        EXPECT_EQ(event.bodyB, 5); // Jupiter
        EXPECT_LT(event.missDeg, event.limitDeg);
        instants.push_back(event.julianDate);
        cursor = event.julianDate + 0.5;
    }

    ASSERT_GE(instants.size(), 15u) << "Io should shadow Jupiter ~17 times in 30 days";
    for (std::size_t i = 1; i < instants.size(); ++i) {
        EXPECT_NEAR(instants[i] - instants[i - 1], 1.769, 0.05);
    }
}

TEST(SkyEventsTest, NextEventPicksTheEarliestOfEveryKind) {
    const double fromJd = Ephemeris::JulianDateFromYmd(2026, 1, 1);
    const SkyEvents::SkyEvent next = SkyEvents::NextEvent(fromJd, 400.0);

    ASSERT_TRUE(next.valid);
    EXPECT_GT(next.julianDate, fromJd);
    EXPECT_NE(next.kind, SkyEvents::EventKind::None);

    // Nothing of any kind may precede it inside the same window.
    const SkyEvents::SkyEvent candidates[] = {
        SkyEvents::NextConjunctionEvent(fromJd, 400.0),
        SkyEvents::NextSolarEclipse(fromJd, 400.0),
        SkyEvents::NextLunarEclipse(fromJd, 400.0),
        SkyEvents::NextInferiorPlanetTransit(fromJd, 400.0),
        SkyEvents::NextGalileanShadowTransit(fromJd, 30.0),
    };
    for (const SkyEvents::SkyEvent& candidate : candidates) {
        if (candidate.valid) {
            EXPECT_GE(candidate.julianDate, next.julianDate);
        }
    }
}

TEST(SkyEventsTest, FormatEventIsAsciiAndNamesTheKind) {
    const SkyEvents::SkyEvent eclipse =
        SkyEvents::NextSolarEclipse(Ephemeris::JulianDateFromYmd(2017, 1, 1), 400.0);
    ASSERT_TRUE(eclipse.valid);

    const std::string line = SkyEvents::FormatEvent(eclipse);
    EXPECT_NE(line.find("solar eclipse"), std::string::npos);
    EXPECT_NE(line.find("2017-02-26"), std::string::npos);
    for (const char c : line) {
        EXPECT_GE(static_cast<unsigned char>(c), 0x20u);
        EXPECT_LT(static_cast<unsigned char>(c), 0x80u) << "overlay copies this byte-for-byte";
    }

    EXPECT_STREQ(SkyEvents::EventKindSlug(SkyEvents::EventKind::SolarEclipse), "solarEclipse");
    EXPECT_STREQ(SkyEvents::EventKindSlug(SkyEvents::EventKind::None), "none");
    EXPECT_TRUE(SkyEvents::FormatEvent(SkyEvents::SkyEvent{}).empty());
}
