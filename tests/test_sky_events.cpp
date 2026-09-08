#include <gtest/gtest.h>

#include <cmath>

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
    EXPECT_STREQ(SkyEvents::BodyName(12), "");
}
