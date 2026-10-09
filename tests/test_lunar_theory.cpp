#include <gtest/gtest.h>

#include <cmath>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/LunarTheory.h"
#include "Auxiliary_Modules/Observer.h"
#include "Auxiliary_Modules/SkyEvents.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

double angularDistanceDeg(double lon1, double lat1, double lon2, double lat2) {
    const double a = lat1 * kDegToRad, b = lat2 * kDegToRad, dl = (lon1 - lon2) * kDegToRad;
    const double c = std::sin(a) * std::sin(b) + std::cos(a) * std::cos(b) * std::cos(dl);
    return std::acos(std::fmax(-1.0, std::fmin(1.0, c))) * kRadToDeg;
}

} // namespace

// Meeus, Astronomical Algorithms 2nd ed., Example 47.a: 1992 April 12, 0h TD
// (JDE 2448724.5): lambda = 133.162655, beta = -3.229126, Delta = 368409.7 km.
TEST(LunarTheoryTest, ReproducesMeeusExample47a) {
    const LunarTheory::MoonOfDate moon = LunarTheory::MeeusMoon(2448724.5);
    EXPECT_NEAR(moon.lonDeg, 133.162655, 2e-6);
    EXPECT_NEAR(moon.latDeg, -3.229126, 2e-6);
    EXPECT_NEAR(moon.distanceKm, 368409.7, 0.1);
}

TEST(LunarTheoryTest, DistanceAndLatitudeStayInTheLunarRanges) {
    double minDistance = 1e9, maxDistance = 0.0, maxLat = 0.0;
    for (double day = 0.0; day < 6800.0; day += 0.37) { // ~18.6 years: a full node cycle
        const LunarTheory::MoonOfDate moon = LunarTheory::MeeusMoon(2451545.0 + day);
        minDistance = std::fmin(minDistance, moon.distanceKm);
        maxDistance = std::fmax(maxDistance, moon.distanceKm);
        maxLat = std::fmax(maxLat, std::fabs(moon.latDeg));
        EXPECT_GE(moon.lonDeg, 0.0);
        EXPECT_LT(moon.lonDeg, 360.0);
    }
    EXPECT_GT(minDistance, 356000.0);   // closest perigee ~356,400 km
    EXPECT_LT(minDistance, 363500.0);
    EXPECT_GT(maxDistance, 405000.0);   // farthest apogee ~406,700 km
    EXPECT_LT(maxDistance, 407000.0);
    EXPECT_NEAR(maxLat, 5.30, 0.1);     // inclination 5.145 deg + perturbations
}

// The two ways of getting from ecliptic-of-date to the J2000 ecliptic — Meeus 21.5's
// ecliptic-precession formulas and the equatorial route through the mean obliquity of date and
// Observer's precession matrix — are independent, so they must agree.
TEST(LunarTheoryTest, EclipticPrecessionAgreesWithTheEquatorialRoute) {
    for (const double jde : {2415020.5, 2433282.5, 2451545.0, 2457986.5, 2469807.5}) {
        const double t = (jde - 2451545.0) / 36525.0;
        const double eps = (23.0 + 26.0 / 60.0 + 21.448 / 3600.0 - 46.8150 / 3600.0 * t -
                            0.00059 / 3600.0 * t * t + 0.001813 / 3600.0 * t * t * t) * kDegToRad;
        for (const double lon : {10.0, 133.1627, 250.0, 359.0}) {
            for (const double lat : {-5.0, 0.0, 4.2}) {
                // ecliptic of date → equatorial of date
                const double l = lon * kDegToRad, b = lat * kDegToRad;
                const double ecl[3] = {std::cos(b) * std::cos(l), std::cos(b) * std::sin(l), std::sin(b)};
                const double eq[3] = {ecl[0], ecl[1] * std::cos(eps) - ecl[2] * std::sin(eps),
                                      ecl[1] * std::sin(eps) + ecl[2] * std::cos(eps)};
                // → equatorial J2000 (inverse = transpose of the J2000→date rotation) → ecliptic J2000
                const Observer::Mat3 p = Observer::PrecessionJ2000ToDate(jde);
                double eq2000[3];
                for (int i = 0; i < 3; ++i) {
                    eq2000[i] = p.m[0][i] * eq[0] + p.m[1][i] * eq[1] + p.m[2][i] * eq[2];
                }
                const Observer::Mat3 e = Observer::EclipticToEquatorialJ2000();
                double ecl2000[3];
                for (int i = 0; i < 3; ++i) {
                    ecl2000[i] = e.m[0][i] * eq2000[0] + e.m[1][i] * eq2000[1] + e.m[2][i] * eq2000[2];
                }
                const double expectedLon = std::fmod(std::atan2(ecl2000[1], ecl2000[0]) * kRadToDeg + 360.0, 360.0);
                const double expectedLat = std::asin(ecl2000[2]) * kRadToDeg;

                double gotLon = 0.0, gotLat = 0.0;
                LunarTheory::EclipticOfDateToJ2000(jde, lon, lat, gotLon, gotLat);
                EXPECT_LT(angularDistanceDeg(gotLon, gotLat, expectedLon, expectedLat), 2e-4)
                    << "jde " << jde << " lon " << lon << " lat " << lat;
            }
        }
    }
}

TEST(LunarTheoryTest, PrecessionIsIdentityAtJ2000AndShiftsLongitudeByThePrecessionRate) {
    double lon = 0.0, lat = 0.0;
    LunarTheory::EclipticOfDateToJ2000(2451545.0, 123.0, 4.0, lon, lat);
    EXPECT_NEAR(lon, 123.0, 1e-9);
    EXPECT_NEAR(lat, 4.0, 1e-9);
    // One century on, the equinox of date has moved ~1.397 deg east of J2000's.
    LunarTheory::EclipticOfDateToJ2000(2451545.0 + 36525.0, 123.0, 0.0, lon, lat);
    EXPECT_NEAR(123.0 - lon, 1.3970, 0.01);
}

TEST(LunarTheoryTest, DeltaTMatchesKnownValuesAndIsContinuous) {
    auto jdOfYear = [](double year) { return Ephemeris::kJ2000 + (year - 2000.0) * 365.25; };
    EXPECT_NEAR(Ephemeris::DeltaTSeconds(jdOfYear(2000.0)), 63.8, 0.5);
    EXPECT_NEAR(Ephemeris::DeltaTSeconds(jdOfYear(2017.6)), 69.2, 2.0);   // TT-UTC 69.18 s then
    EXPECT_NEAR(Ephemeris::DeltaTSeconds(jdOfYear(1950.0)), 29.1, 0.5);
    EXPECT_NEAR(Ephemeris::DeltaTSeconds(jdOfYear(1900.0)), -2.8, 0.5);
    EXPECT_NEAR(Ephemeris::DeltaTSeconds(jdOfYear(1820.0)), 12.0, 1.5);
    // A coefficient typo shows up as a jump where two polynomials meet.
    for (const double boundary : {1860.0, 1900.0, 1920.0, 1941.0, 1961.0, 1986.0, 2005.0, 2050.0, 2150.0}) {
        const double before = Ephemeris::DeltaTSeconds(jdOfYear(boundary - 1e-4));
        const double after = Ephemeris::DeltaTSeconds(jdOfYear(boundary + 1e-4));
        EXPECT_NEAR(before, after, 1.0) << "at " << boundary;
    }
}

TEST(LunarTheoryTest, BackendServesTheMeeusMoonInJ2000AxesInAu) {
    const double jd = 2457986.5;
    double xyz[3];
    ASSERT_TRUE(Ephemeris::SatellitePosition(SkyEvents::kMoon, jd, xyz));
    const double r = std::sqrt(xyz[0] * xyz[0] + xyz[1] * xyz[1] + xyz[2] * xyz[2]);
    EXPECT_GT(r * 149597870.7, 356000.0);
    EXPECT_LT(r * 149597870.7, 407000.0);

    double direct[3];
    LunarTheory::MoonGeocentricJ2000Au(jd + Ephemeris::DeltaTSeconds(jd) / 86400.0, direct);
    for (int i = 0; i < 3; ++i) {
        EXPECT_DOUBLE_EQ(xyz[i], direct[i]);
    }
}

// The August 21 2017 total solar eclipse: greatest eclipse 18:25:32 UT (NASA/Espenak), with the
// Moon's shadow axis passing gamma = 0.4367 Earth radii from the geocentre, i.e. the Moon-Sun
// centre separation at greatest eclipse is gamma * (moon parallax - sun parallax) ~ 0.41 deg.
// With the mean-element Moon this was off by over a degree; the series puts it within minutes.
TEST(LunarTheoryTest, TheAugust2017EclipseLandsWhereItDid) {
    const double from = Ephemeris::JulianDateFromYmd(2017, 8, 1);
    const SkyEvents::SkyEvent event = SkyEvents::NextSolarEclipse(from, 60.0);
    ASSERT_TRUE(event.valid);
    const double greatest = Ephemeris::JulianDateFromYmd(2017, 8, 21) + (18.0 + 25.0 / 60.0 + 32.0 / 3600.0) / 24.0;
    // The Sun's aberration (Meeus ch. 25) is what brings this from ~36 s late to a few seconds.
    EXPECT_NEAR(event.julianDate, greatest, 12.0 / 86400.0) << "within 12 seconds";
    EXPECT_NEAR(event.missDeg, 0.41, 0.05);
}
