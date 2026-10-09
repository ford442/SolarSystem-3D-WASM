#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/Observer.h"
#include "Auxiliary_Modules/SkyEvents.h"

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

/** Unit equatorial vector for RA/Dec in degrees. */
void raDecToVector(double raDeg, double decDeg, double out[3]) {
    const double ra = raDeg * kDegToRad;
    const double dec = decDeg * kDegToRad;
    out[0] = std::cos(dec) * std::cos(ra);
    out[1] = std::cos(dec) * std::sin(ra);
    out[2] = std::sin(dec);
}

double angleDiffDeg(double a, double b) {
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d < -180.0) d += 360.0;
    return d;
}

/** 2017-03-20 12:00 UTC, a few hours after the March equinox. */
const double kEquinoxNoonJd = Ephemeris::JulianDateFromYmd(2017, 3, 20) + 0.5;

} // namespace

TEST(ObserverTest, LocalSiderealTimeAddsEastLongitude) {
    const double jd = Ephemeris::kJ2000;
    EXPECT_NEAR(Observer::LocalSiderealTimeDeg(jd, 0.0), 280.46061837, 1e-5);
    EXPECT_NEAR(Observer::LocalSiderealTimeDeg(jd, 15.0), 295.46061837, 1e-5);
    EXPECT_NEAR(Observer::LocalSiderealTimeDeg(jd, -90.0), 190.46061837, 1e-5);
    // Wraps into [0, 360).
    EXPECT_NEAR(Observer::LocalSiderealTimeDeg(jd, 100.0), 20.46061837, 1e-5);
}

TEST(ObserverTest, PrecessionIsIdentityAtJ2000AndMatchesTheGeneralRate) {
    const Observer::Mat3 identity = Observer::PrecessionJ2000ToDate(Ephemeris::kJ2000);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_NEAR(identity.m[i][j], i == j ? 1.0 : 0.0, 1e-12);
        }
    }

    // The J2000 equinox, one Julian century later: RA gains m ~ 46.1"/yr (1.28 deg) and Dec
    // gains n cos(RA) ~ 20.0"/yr (0.557 deg).
    const double equinox[3] = {1.0, 0.0, 0.0};
    double v[3];
    Observer::PrecessionJ2000ToDate(Ephemeris::kJ2000 + 36525.0).Apply(equinox, v);
    const double raDeg = std::atan2(v[1], v[0]) * kRadToDeg;
    const double decDeg = std::asin(v[2]) * kRadToDeg;
    EXPECT_NEAR(raDeg, 1.2813, 0.005);
    EXPECT_NEAR(decDeg, 0.5567, 0.005);
}

TEST(ObserverTest, PrecessionAndHorizonMatricesAreRotations) {
    const Observer::Site site{43.0, -106.0, 1500.0};
    for (double day = 0.0; day < 9000.0; day += 1234.5) {
        const double jd = Ephemeris::kJ2000 + day;
        const Observer::Mat3 mats[] = {Observer::PrecessionJ2000ToDate(jd),
                                       Observer::HorizonFromEquatorialJ2000(jd, site),
                                       Observer::HorizonFromEclipticJ2000(jd, site)};
        for (const Observer::Mat3& r : mats) {
            for (int a = 0; a < 3; ++a) {
                for (int b = 0; b < 3; ++b) {
                    double rowDot = 0.0;
                    for (int k = 0; k < 3; ++k) rowDot += r.m[a][k] * r.m[b][k];
                    EXPECT_NEAR(rowDot, a == b ? 1.0 : 0.0, 1e-12);
                }
            }
            const double det =
                r.m[0][0] * (r.m[1][1] * r.m[2][2] - r.m[1][2] * r.m[2][1]) -
                r.m[0][1] * (r.m[1][0] * r.m[2][2] - r.m[1][2] * r.m[2][0]) +
                r.m[0][2] * (r.m[1][0] * r.m[2][1] - r.m[1][1] * r.m[2][0]);
            EXPECT_NEAR(det, 1.0, 1e-12); // a rotation, not a mirror: East x Up = -North
        }
    }
}

TEST(ObserverTest, EclipticPoleSitsObliquityFromTheCelestialPole) {
    const double eclipticNorth[3] = {0.0, 0.0, 1.0};
    double eq[3];
    Observer::EclipticToEquatorialJ2000().Apply(eclipticNorth, eq);
    EXPECT_NEAR(std::asin(eq[2]) * kRadToDeg, 90.0 - 23.4392911, 1e-9);
    EXPECT_NEAR(std::atan2(eq[1], eq[0]) * kRadToDeg, -90.0, 1e-9); // RA 18h
}

TEST(ObserverTest, EquatorialToHorizontalHandlesTheCardinalCases) {
    double alt = 0.0, az = 0.0;

    // Celestial pole from the north pole is straight up.
    Observer::EquatorialToHorizontal(10.0, 90.0, 123.0, 90.0, alt, az);
    EXPECT_NEAR(alt, 90.0, 1e-9);

    // On the equator, a dec-0 star on the meridian is at the zenith.
    Observer::EquatorialToHorizontal(80.0, 0.0, 80.0, 0.0, alt, az);
    EXPECT_NEAR(alt, 90.0, 1e-9);

    // Latitude 45, dec 0 on the meridian: due south, 45 degrees up.
    Observer::EquatorialToHorizontal(80.0, 0.0, 80.0, 45.0, alt, az);
    EXPECT_NEAR(alt, 45.0, 1e-9);
    EXPECT_NEAR(az, 180.0, 1e-9);

    // The pole star's altitude is the latitude, due north.
    Observer::EquatorialToHorizontal(0.0, 90.0, 0.0, 52.0, alt, az);
    EXPECT_NEAR(alt, 52.0, 1e-9);

    // A star east of the meridian (RA > LST) rises in the east: positive azimuth below 180.
    Observer::EquatorialToHorizontal(110.0, 0.0, 80.0, 45.0, alt, az);
    EXPECT_GT(az, 90.0);
    EXPECT_LT(az, 180.0);
    // ...and a star west of the meridian (RA < LST) is in the western half.
    Observer::EquatorialToHorizontal(50.0, 0.0, 80.0, 45.0, alt, az);
    EXPECT_GT(az, 180.0);
    EXPECT_LT(az, 270.0);
}

TEST(ObserverTest, HorizonMatrixAgreesWithTheClosedFormAtJ2000) {
    // At J2000 precession is the identity, so the matrix path and the textbook alt/az
    // formula must agree — this pins down the East/Up/-North handedness.
    const Observer::Site site{35.0, 20.0, 0.0};
    const double jd = Ephemeris::kJ2000;
    const double lst = Observer::LocalSiderealTimeDeg(jd, site.lonDeg);
    const Observer::Mat3 toHorizon = Observer::HorizonFromEquatorialJ2000(jd, site);

    const double stars[][2] = {{0.0, 0.0},     {90.0, 30.0},  {180.0, -45.0},
                               {270.0, 10.0},  {300.0, 80.0}, {45.0, -80.0}};
    for (const auto& star : stars) {
        double v[3], h[3];
        raDecToVector(star[0], star[1], v);
        toHorizon.Apply(v, h);
        double alt = 0.0, az = 0.0;
        Observer::AltAzFromDirection(h, alt, az);

        double expectedAlt = 0.0, expectedAz = 0.0;
        Observer::EquatorialToHorizontal(star[0], star[1], lst, site.latDeg, expectedAlt,
                                         expectedAz);
        EXPECT_NEAR(alt, expectedAlt, 1e-9) << "RA " << star[0] << " Dec " << star[1];
        EXPECT_NEAR(angleDiffDeg(az, expectedAz), 0.0, 1e-9)
            << "RA " << star[0] << " Dec " << star[1];
    }
}

TEST(ObserverTest, AltAzFromDirectionUsesEastUpMinusNorth) {
    double alt = 0.0, az = 0.0;
    const double north[3] = {0.0, 0.0, -1.0};
    Observer::AltAzFromDirection(north, alt, az);
    EXPECT_NEAR(alt, 0.0, 1e-9);
    EXPECT_NEAR(az, 0.0, 1e-9);

    const double east[3] = {1.0, 0.0, 0.0};
    Observer::AltAzFromDirection(east, alt, az);
    EXPECT_NEAR(az, 90.0, 1e-9);

    const double south[3] = {0.0, 0.0, 1.0};
    Observer::AltAzFromDirection(south, alt, az);
    EXPECT_NEAR(az, 180.0, 1e-9);

    const double up[3] = {0.0, 2.0, 0.0};
    Observer::AltAzFromDirection(up, alt, az);
    EXPECT_NEAR(alt, 90.0, 1e-9);
}

TEST(ObserverTest, SunIsHighAtLocalNoonAndBelowTheHorizonAtMidnight) {
    // Equinox, equator, Greenwich: the Sun is nearly overhead at 12:00 UTC (the equation of
    // time is about -7 min) and nearly under foot at 00:00.
    const Observer::Site greenwichEquator{0.0, 0.0, 0.0};
    const Observer::Sky noon = Observer::ComputeSky(kEquinoxNoonJd, greenwichEquator);
    EXPECT_GT(noon.bodies[Observer::kSkySun].altDeg, 87.0);

    const Observer::Sky midnight = Observer::ComputeSky(kEquinoxNoonJd - 0.5, greenwichEquator);
    EXPECT_LT(midnight.bodies[Observer::kSkySun].altDeg, -86.0);

    // About 06:00 UTC the Sun is on the eastern horizon.
    const Observer::Sky dawn = Observer::ComputeSky(kEquinoxNoonJd - 0.25, greenwichEquator);
    EXPECT_NEAR(dawn.bodies[Observer::kSkySun].azDeg, 90.0, 3.0);
    EXPECT_NEAR(dawn.bodies[Observer::kSkySun].altDeg, 0.0, 3.0);
}

TEST(ObserverTest, SunCrossesTheSouthernSkyAtNorthernMidLatitudes) {
    const Observer::Site midLatitude{45.0, 0.0, 0.0};
    const Observer::SkyBody sun =
        Observer::ComputeSky(kEquinoxNoonJd, midLatitude).bodies[Observer::kSkySun];
    EXPECT_NEAR(sun.altDeg, 45.0, 1.5);   // 90 - latitude + declination (~0)
    EXPECT_NEAR(angleDiffDeg(sun.azDeg, 180.0), 0.0, 5.0);
}

TEST(ObserverTest, SunAndMoonHaveTheirRealAngularSizes) {
    const Observer::Site site{40.0, -75.0, 0.0};
    const Observer::Sky sky = Observer::ComputeSky(kEquinoxNoonJd, site);
    const Observer::SkyBody& sun = sky.bodies[Observer::kSkySun];
    const Observer::SkyBody& moon = sky.bodies[Observer::kSkyMoon];
    EXPECT_NEAR(sun.angRadiusDeg, 0.2666, 0.006);
    EXPECT_GT(moon.angRadiusDeg, 0.23);
    EXPECT_LT(moon.angRadiusDeg, 0.29);
    EXPECT_GT(moon.distanceKm, 350000.0);
    EXPECT_LT(moon.distanceKm, 410000.0);
    EXPECT_NEAR(sun.magnitude, -26.74, 0.1);
}

TEST(ObserverTest, MoonParallaxLowersItByAboutAHorizontalParallaxTimesCosAltitude) {
    const Observer::Site site{45.0, 10.0, 0.0};
    int checked = 0;
    for (double day = 0.0; day < 28.0; day += 0.37) {
        const double jd = kEquinoxNoonJd + day;
        const Observer::SkyBody moon =
            Observer::ComputeSky(jd, site).bodies[Observer::kSkyMoon];

        double geo[3], h[3];
        ASSERT_TRUE(SkyEvents::GeocentricEclipticKm(SkyEvents::kMoon, jd, geo));
        Observer::HorizonFromEclipticJ2000(jd, site).Apply(geo, h);
        double geoAlt = 0.0, geoAz = 0.0;
        Observer::AltAzFromDirection(h, geoAlt, geoAz);

        const double parallaxDeg = std::asin(6378.137 / moon.distanceKm) * kRadToDeg;
        const double expected = parallaxDeg * std::cos(geoAlt * kDegToRad);
        EXPECT_NEAR(geoAlt - moon.altDeg, expected, 0.06) << "jd " << jd;
        EXPECT_GE(geoAlt - moon.altDeg, -0.01);
        ++checked;
    }
    EXPECT_GT(checked, 50);
}

TEST(ObserverTest, MoonPhaseFollowsTheSunMoonGeometry) {
    const Observer::Site site{0.0, 0.0, 0.0};
    // New moon 2017-08-21 ~18:30 UTC and full moon 2017-08-07 ~18:11 UTC; the mean-element
    // Moon is a couple of degrees off, which still leaves a very thin or very full disc.
    const double newMoonJd = Ephemeris::JulianDateFromYmd(2017, 8, 21) + 18.5 / 24.0;
    const double fullMoonJd = Ephemeris::JulianDateFromYmd(2017, 8, 7) + 18.2 / 24.0;
    EXPECT_LT(Observer::ComputeSky(newMoonJd, site).bodies[Observer::kSkyMoon].illuminatedFraction,
              0.02);
    EXPECT_GT(Observer::ComputeSky(fullMoonJd, site).bodies[Observer::kSkyMoon].illuminatedFraction,
              0.95);
}

TEST(ObserverTest, BrightLimbPointsTowardTheSunAndAcrossTheMoon) {
    const Observer::Site site{30.0, 100.0, 0.0};
    for (double day = 0.0; day < 29.0; day += 3.1) {
        const Observer::Sky sky = Observer::ComputeSky(kEquinoxNoonJd + day, site);
        for (int slot = Observer::kSkyMoon; slot < Observer::kSkyBodyCount; ++slot) {
            const Observer::SkyBody& body = sky.bodies[slot];
            const double len = std::sqrt(body.brightLimbDir[0] * body.brightLimbDir[0] +
                                         body.brightLimbDir[1] * body.brightLimbDir[1] +
                                         body.brightLimbDir[2] * body.brightLimbDir[2]);
            EXPECT_NEAR(len, 1.0, 1e-9);
            const double along = body.brightLimbDir[0] * body.dir[0] +
                                 body.brightLimbDir[1] * body.dir[1] +
                                 body.brightLimbDir[2] * body.dir[2];
            EXPECT_NEAR(along, 0.0, 1e-9); // lies on the sky plane, perpendicular to the line of sight
        }
    }
}

TEST(ObserverTest, PlanetMagnitudesStayInTheirKnownRanges) {
    const Observer::Site site{0.0, 0.0, 0.0};
    for (double day = 0.0; day < 4000.0; day += 29.0) {
        const Observer::Sky sky = Observer::ComputeSky(kEquinoxNoonJd + day, site);
        const double venus = sky.bodies[Observer::kSkyVenus].magnitude;
        const double jupiter = sky.bodies[Observer::kSkyJupiter].magnitude;
        const double mars = sky.bodies[Observer::kSkyMars].magnitude;
        EXPECT_GT(venus, -5.0);
        EXPECT_LT(venus, -2.8);
        EXPECT_GT(jupiter, -3.2);
        EXPECT_LT(jupiter, -1.4);
        EXPECT_GT(mars, -3.2);
        EXPECT_LT(mars, 2.0);
    }
}

TEST(ObserverTest, SkyBodiesAreNamedAndIndexedConsistently) {
    const Observer::Sky sky = Observer::ComputeSky(kEquinoxNoonJd, {10.0, 20.0, 0.0});
    EXPECT_STREQ(sky.bodies[Observer::kSkySun].name, "Sun");
    EXPECT_STREQ(sky.bodies[Observer::kSkyMoon].name, "Moon");
    EXPECT_EQ(sky.bodies[Observer::kSkyMoon].ephemerisIndex, SkyEvents::kMoon);
    EXPECT_STREQ(sky.bodies[Observer::kSkySaturn].name, "Saturn");
    for (const Observer::SkyBody& body : sky.bodies) {
        const double len = std::sqrt(body.dir[0] * body.dir[0] + body.dir[1] * body.dir[1] +
                                     body.dir[2] * body.dir[2]);
        EXPECT_NEAR(len, 1.0, 1e-9) << body.name;
        EXPECT_GE(body.azDeg, 0.0);
        EXPECT_LT(body.azDeg, 360.0);
    }
}

TEST(ObserverTest, DiscOverlapFractionCoversTheEclipseCases) {
    // Disjoint, tangent, and fully covered.
    EXPECT_DOUBLE_EQ(Observer::DiscOverlapFraction(0.27, 0.26, 0.6), 0.0);
    EXPECT_DOUBLE_EQ(Observer::DiscOverlapFraction(0.27, 0.26, 0.53), 0.0);
    EXPECT_DOUBLE_EQ(Observer::DiscOverlapFraction(0.27, 0.28, 0.0), 1.0);   // total
    EXPECT_NEAR(Observer::DiscOverlapFraction(0.27, 0.26, 0.0), (0.26 * 0.26) / (0.27 * 0.27),
                1e-12);                                                       // annular
    // Equal discs whose centres are one radius apart cover 2*pi/3 - sqrt(3)/2 of r^2.
    EXPECT_NEAR(Observer::DiscOverlapFraction(1.0, 1.0, 1.0),
                (2.0 * kPi / 3.0 - std::sqrt(3.0) / 2.0) / kPi, 1e-12);
    // Monotonic as the discs slide apart.
    double previous = 1.0;
    for (double sep = 0.0; sep < 0.55; sep += 0.05) {
        const double now = Observer::DiscOverlapFraction(0.27, 0.26, sep);
        EXPECT_LE(now, previous + 1e-12);
        previous = now;
    }
}

TEST(ObserverTest, FormatUtcPrintsCivilDateAndTime) {
    EXPECT_EQ(Observer::FormatUtc(Ephemeris::kJ2000), "2000-01-01 12:00:00");
    const double eclipse = Ephemeris::JulianDateFromYmd(2017, 8, 21) + (17.0 + 42.0 / 60.0) / 24.0;
    EXPECT_EQ(Observer::FormatUtc(eclipse), "2017-08-21 17:42:00");
    // Midnight and the second before it stay on their own days; 59.7 s rounds up across it.
    const double midnight = Ephemeris::JulianDateFromYmd(2026, 10, 7);
    EXPECT_EQ(Observer::FormatUtc(midnight), "2026-10-07 00:00:00");
    EXPECT_EQ(Observer::FormatUtc(midnight - 1.0 / 86400.0), "2026-10-06 23:59:59");
    EXPECT_EQ(Observer::FormatUtc(midnight - 0.3 / 86400.0), "2026-10-07 00:00:00");
}

namespace {

double dot3d(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/** Pearson correlation of two equal-length series. */
double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(a.size());
    mb /= static_cast<double>(a.size());
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt(saa * sbb);
}

} // namespace

TEST(ObserverTest, MoonBodyAxesAreARightHandedRotation) {
    for (double day = 0.0; day < 60.0; day += 3.7) {
        const Observer::Mat3 axes = Observer::MoonBodyAxesEquatorialJ2000(2457986.5 + day);
        const double x[3] = {axes.m[0][0], axes.m[1][0], axes.m[2][0]};
        const double y[3] = {axes.m[0][1], axes.m[1][1], axes.m[2][1]};
        const double z[3] = {axes.m[0][2], axes.m[1][2], axes.m[2][2]};
        EXPECT_NEAR(dot3d(x, x), 1.0, 1e-12);
        EXPECT_NEAR(dot3d(y, y), 1.0, 1e-12);
        EXPECT_NEAR(dot3d(z, z), 1.0, 1e-12);
        EXPECT_NEAR(dot3d(x, y), 0.0, 1e-12);
        EXPECT_NEAR(dot3d(x, z), 0.0, 1e-12);
        const double cross[3] = {x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0]};
        EXPECT_NEAR(dot3d(cross, z), 1.0, 1e-12); // X x Y = Z: right-handed, so east is +Y
    }
}

// The Moon's pole is within ~1.5 deg of the ecliptic pole's neighbourhood: 66.5 deg north
// declination, RA 270 deg (IAU 2009 mean values).
TEST(ObserverTest, MoonPoleSitsAtTheIauMeanPole) {
    const Observer::Mat3 axes = Observer::MoonBodyAxesEquatorialJ2000(Ephemeris::kJ2000);
    const double dec = std::asin(axes.m[2][2]) * kRadToDeg;
    double ra = std::atan2(axes.m[1][2], axes.m[0][2]) * kRadToDeg;
    if (ra < 0.0) ra += 360.0;
    EXPECT_NEAR(dec, 66.5392, 2.0);
    EXPECT_NEAR(ra, 269.9949, 4.0);
}

// The strongest check on the frame: the sub-Earth point must librate a few degrees about
// (0, 0) — in longitude with the orbit's equation of centre (2e sin M, so positive just after
// perigee) and in latitude opposite the Moon's ecliptic latitude (Earth looks down on the north
// pole when the Moon is south of the ecliptic). A wrong W sign or axis order would put the
// longitude tens of degrees off, not a few.
TEST(ObserverTest, MoonLibrationStaysSmallAndFollowsTheOrbit) {
    const Observer::Site site{0.0, 0.0, 0.0};
    std::vector<double> lon, lat, sinMeanAnomaly, negEclipticLat;
    double maxAbsLon = 0.0, maxAbsLat = 0.0;
    for (double day = 0.0; day < 200.0; day += 0.5) {
        const double jd = 2457986.5 + day;
        const Observer::Sky sky = Observer::ComputeSky(jd, site);
        lon.push_back(sky.moonSubEarthLonDeg);
        lat.push_back(sky.moonSubEarthLatDeg);
        maxAbsLon = std::max(maxAbsLon, std::fabs(sky.moonSubEarthLonDeg));
        maxAbsLat = std::max(maxAbsLat, std::fabs(sky.moonSubEarthLatDeg));

        const double meanAnomaly = (134.9634 + 13.06499295 * (jd - Ephemeris::kJ2000)) * kDegToRad;
        sinMeanAnomaly.push_back(std::sin(meanAnomaly));
        double moonKm[3];
        ASSERT_TRUE(SkyEvents::GeocentricEclipticKm(SkyEvents::kMoon, jd, moonKm));
        const double eclLat = std::asin(moonKm[2] / std::sqrt(dot3d(moonKm, moonKm))) * kRadToDeg;
        negEclipticLat.push_back(-eclLat);
    }
    EXPECT_LT(maxAbsLon, 10.0);   // true maximum ~7.9 deg; the mean-element Moon adds a little
    EXPECT_GT(maxAbsLon, 4.5);
    EXPECT_LT(maxAbsLat, 8.5);    // true maximum ~6.7 deg
    EXPECT_GT(maxAbsLat, 4.5);
    EXPECT_GT(correlation(lon, sinMeanAnomaly), 0.9);
    EXPECT_GT(correlation(lat, negEclipticLat), 0.9);
}

// The horizon-frame body axes and the sub-Earth point must agree: rotating the direction the
// observer looks back along (toward the Moon from Earth's centre, ignoring the <1 deg
// parallax) into body coordinates gives the sub-Earth selenographic position.
TEST(ObserverTest, MoonBodyToHorizonMatchesTheSubEarthPoint) {
    const Observer::Site site{42.87, -106.31, 1600.0};
    for (double day = 0.0; day < 30.0; day += 2.3) {
        const double jd = 2457986.5 + day;
        const Observer::Sky sky = Observer::ComputeSky(jd, site);
        double geo[3], toMoon[3];
        ASSERT_TRUE(SkyEvents::GeocentricEclipticKm(SkyEvents::kMoon, jd, geo));
        Observer::HorizonFromEclipticJ2000(jd, site).Apply(geo, toMoon);
        const double len = std::sqrt(dot3d(toMoon, toMoon));
        // From the Moon the Earth is in the opposite direction.
        const double earthFromMoon[3] = {-toMoon[0] / len, -toMoon[1] / len, -toMoon[2] / len};
        double body[3];
        for (int axis = 0; axis < 3; ++axis) {
            const double column[3] = {sky.moonBodyToHorizon.m[0][axis], sky.moonBodyToHorizon.m[1][axis],
                                      sky.moonBodyToHorizon.m[2][axis]};
            body[axis] = dot3d(earthFromMoon, column);
        }
        EXPECT_NEAR(std::atan2(body[1], body[0]) * kRadToDeg, sky.moonSubEarthLonDeg, 1e-6);
        EXPECT_NEAR(std::asin(body[2]) * kRadToDeg, sky.moonSubEarthLatDeg, 1e-6);
    }
}

TEST(ObserverTest, BodyAltitudeCoversSkyBodiesOnly) {
    const Observer::Site site{0.0, 0.0, 0.0};
    double alt = 123.0;
    EXPECT_TRUE(Observer::BodyAltitudeDeg(0, kEquinoxNoonJd, site, alt));
    EXPECT_GT(alt, 87.0);
    EXPECT_TRUE(Observer::BodyAltitudeDeg(SkyEvents::kMoon, kEquinoxNoonJd, site, alt));
    EXPECT_TRUE(Observer::BodyAltitudeDeg(5, kEquinoxNoonJd, site, alt)); // Jupiter
    alt = 123.0;
    EXPECT_FALSE(Observer::BodyAltitudeDeg(3, kEquinoxNoonJd, site, alt)); // Earth is not in its own sky
    EXPECT_FALSE(Observer::BodyAltitudeDeg(8, kEquinoxNoonJd, site, alt)); // Neptune
    EXPECT_DOUBLE_EQ(alt, 123.0);
}

namespace {

/** Peak Sun coverage over a UTC window at a site, sampled every 10 s, and the UTC second-of-day it occurs. */
double peakCoverage(const Observer::Site& site, double fromHour, double toHour, double* atHour = nullptr) {
    const double day = Ephemeris::JulianDateFromYmd(2017, 8, 21);
    double best = 0.0;
    for (double h = fromHour; h < toHour; h += 10.0 / 3600.0) {
        const double c = Observer::ComputeSky(day + h / 24.0, site).sunCoverage;
        if (c > best) {
            best = c;
            if (atHour) *atHour = h;
        }
    }
    return best;
}

} // namespace

// The 2017-08-21 eclipse from three places on the path of totality, and one outside it. Published
// circumstances (NASA/Espenak): totality at Casper 17:42:36-17:45:02 UTC, at Salem OR about
// 17:17-17:19, around Hopkinsville KY about 18:25-18:28; New York saw ~0.71 magnitude (~0.64 of
// the Sun's area) near 18:44.
TEST(ObserverTest, TheAugust2017EclipseIsTotalOnThePathAndPartialOffIt) {
    double hour = 0.0;
    EXPECT_GT(peakCoverage({42.8666, -106.3131, 1600.0}, 17.0, 18.5, &hour), 0.9999); // Casper
    EXPECT_NEAR(hour, 17.0 + 43.75 / 60.0, 1.5 / 60.0);       // mid-totality ~17:43:45
    EXPECT_GT(peakCoverage({44.9429, -123.0351, 50.0}, 17.0, 17.6), 0.9999);          // Salem
    EXPECT_GT(peakCoverage({36.8667, -87.4886, 150.0}, 18.0, 18.9), 0.9999);          // Hopkinsville

    const double newYork = peakCoverage({40.7128, -74.006, 10.0}, 18.0, 19.5);
    EXPECT_GT(newYork, 0.55);
    EXPECT_LT(newYork, 0.75);

    // Far from the shadow, at a moment when the Sun is up, nothing is covered.
    EXPECT_DOUBLE_EQ(Observer::ComputeSky(2457986.5 + 17.7 / 24.0 - 5.0, {42.8666, -106.3131, 1600.0}).sunCoverage, 0.0);
}

TEST(ObserverTest, TotalityLastsMinutesAtCasperAndCoverageRisesMonotonically) {
    const Observer::Site casper{42.8666, -106.3131, 1600.0};
    const double day = Ephemeris::JulianDateFromYmd(2017, 8, 21);
    double firstTotal = -1.0, lastTotal = -1.0, previous = -1.0;
    for (double s = 16.0 * 3600.0 + 50.0 * 60.0; s < 17.0 * 3600.0 + 43.0 * 60.0; s += 20.0) { // approach only
        const double c = Observer::ComputeSky(day + s / 86400.0, casper).sunCoverage;
        EXPECT_GE(c, previous - 1e-9) << "coverage fell on the way in at " << s;
        previous = c;
    }
    for (double s = 17.0 * 3600.0; s < 18.0 * 3600.0; s += 1.0) {
        if (Observer::ComputeSky(day + s / 86400.0, casper).sunCoverage >= 0.9999) {
            if (firstTotal < 0.0) firstTotal = s;
            lastTotal = s;
        }
    }
    ASSERT_GT(firstTotal, 0.0);
    EXPECT_NEAR(lastTotal - firstTotal, 146.0, 15.0);       // published duration 2m26s
    EXPECT_NEAR(firstTotal, 17 * 3600 + 42 * 60 + 36, 25.0); // second contact ~17:42:36
}
