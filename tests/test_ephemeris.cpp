#include <gtest/gtest.h>

#include <cmath>

#include "Auxiliary_Modules/Ephemeris.h"

namespace {

constexpr double kDeg = 180.0 / 3.14159265358979323846;

double LonDeg(int body, double jd) {
    return Ephemeris::Position(body, jd).lonRad * kDeg;
}

/** Smallest absolute difference between two degrees on a circle. */
double AngleDiffDeg(double a, double b) {
    double d = std::fmod(a - b + 180.0, 360.0);
    if (d < 0.0) {
        d += 360.0;
    }
    return std::fabs(d - 180.0);
}

} // namespace

TEST(EphemerisTest, JulianDateRoundTripYmd) {
    // Civil 2000-01-01 0h is JD 2451544.5; J2000.0 noon is 2451545.0.
    EXPECT_NEAR(Ephemeris::JulianDateFromYmd(2000, 1, 1), 2451544.5, 1e-6);

    int y = 0;
    int m = 0;
    int d = 0;
    Ephemeris::YmdFromJulianDate(2451544.5, y, m, d);
    EXPECT_EQ(y, 2000);
    EXPECT_EQ(m, 1);
    EXPECT_EQ(d, 1);

    Ephemeris::YmdFromJulianDate(Ephemeris::JulianDateFromYmd(2026, 7, 21), y, m, d);
    EXPECT_EQ(y, 2026);
    EXPECT_EQ(m, 7);
    EXPECT_EQ(d, 21);
}

TEST(EphemerisTest, SunPositionIsZero) {
    const auto sun = Ephemeris::Position(0, Ephemeris::kJ2000);
    EXPECT_DOUBLE_EQ(sun.lonRad, 0.0);
    EXPECT_DOUBLE_EQ(sun.latRad, 0.0);
    EXPECT_DOUBLE_EQ(sun.rAu, 0.0);
}

TEST(EphemerisTest, EarthNearMeanLongitudeAtJ2000) {
    // Standish EM Bary mean longitude at T=0 is 100.46457166°.
    // True heliocentric longitude is within ~1° of mean longitude for Earth.
    const double lon = LonDeg(3, Ephemeris::kJ2000);
    EXPECT_LT(AngleDiffDeg(lon, 100.46457166), 1.0);
    EXPECT_NEAR(Ephemeris::Position(3, Ephemeris::kJ2000).rAu, 1.0, 0.02);
}

TEST(EphemerisTest, MercuryNearMeanLongitudeAtJ2000) {
    const double lon = LonDeg(1, Ephemeris::kJ2000);
    EXPECT_LT(AngleDiffDeg(lon, 252.25032350), 2.0);
}

TEST(EphemerisTest, MarsNearMeanLongitudeAtJ2000) {
    // Mars e≈0.09 → true longitude can sit several degrees from mean L.
    const double lon = LonDeg(4, Ephemeris::kJ2000);
    EXPECT_LT(AngleDiffDeg(lon, -4.55343205), 6.0);
    EXPECT_NEAR(Ephemeris::Position(4, Ephemeris::kJ2000).rAu, 1.524, 0.15);
}

TEST(EphemerisTest, JupiterNearMeanLongitudeAtJ2000) {
    const double lon = LonDeg(5, Ephemeris::kJ2000);
    EXPECT_LT(AngleDiffDeg(lon, 34.39644051), 2.0);
}

TEST(EphemerisTest, EarthAdvancesAboutOneDegreePerDay) {
    const double lon0 = LonDeg(3, Ephemeris::kJ2000);
    const double lon1 = LonDeg(3, Ephemeris::kJ2000 + 1.0);
    // ~0.986°/day mean motion
    EXPECT_NEAR(AngleDiffDeg(lon1, lon0), 0.986, 0.05);
}

TEST(EphemerisTest, PlutoHasReasonableDistance) {
    const auto pluto = Ephemeris::Position(9, Ephemeris::kJ2000);
    EXPECT_GT(pluto.rAu, 29.0);
    EXPECT_LT(pluto.rAu, 50.0);
}
