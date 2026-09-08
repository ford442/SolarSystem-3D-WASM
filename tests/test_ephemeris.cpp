#include <gtest/gtest.h>

#include <algorithm>
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

TEST(EphemerisBackendTest, DefaultBackendIsStandish) {
    EXPECT_STREQ(Ephemeris::GetBackend().Name(), "standish");
    EXPECT_EQ(&Ephemeris::GetBackend(), &Ephemeris::StandishBackend());
}

TEST(EphemerisBackendTest, PositionMatchesStandishBackendDirectly) {
    // Position() is a facade over the active backend — same numbers, no drift.
    for (const double jd : {Ephemeris::kJ2000, Ephemeris::kJ2000 + 9600.0}) {
        for (int body = 1; body <= 9; ++body) {
            const auto viaFacade = Ephemeris::Position(body, jd);
            const auto viaBackend = Ephemeris::StandishBackend().PlanetHeliocentric(body, jd);
            EXPECT_DOUBLE_EQ(viaFacade.lonRad, viaBackend.lonRad) << "body " << body;
            EXPECT_DOUBLE_EQ(viaFacade.latRad, viaBackend.latRad) << "body " << body;
            EXPECT_DOUBLE_EQ(viaFacade.rAu, viaBackend.rAu) << "body " << body;
        }
    }
}

TEST(EphemerisBackendTest, SatellitePositionsAreNotImplementedYet) {
    // Moons still use the circular SatelliteOrbit helpers; the hook exists but no
    // backend answers it. Guards against a caller silently trusting {0,0,0}.
    double xyz[3] = {1.0, 2.0, 3.0};
    EXPECT_FALSE(Ephemeris::GetBackend().SatelliteParentRelative(0, Ephemeris::kJ2000, xyz));
    EXPECT_DOUBLE_EQ(xyz[0], 0.0);
    EXPECT_DOUBLE_EQ(xyz[1], 0.0);
    EXPECT_DOUBLE_EQ(xyz[2], 0.0);
}

TEST(EphemerisBackendTest, SetBackendOverridesAndRestores) {
    class ShiftedStandish final : public Ephemeris::IEphemeris {
    public:
        const char* Name() const override { return "test-shifted"; }
        Ephemeris::HelioLB PlanetHeliocentric(int bodyIndex, double jd) const override {
            auto p = Ephemeris::StandishBackend().PlanetHeliocentric(bodyIndex, jd);
            p.rAu *= 2.0;
            return p;
        }
    };

    const ShiftedStandish shifted;
    const double baseline = Ephemeris::Position(3, Ephemeris::kJ2000).rAu;

    Ephemeris::SetBackend(&shifted);
    EXPECT_STREQ(Ephemeris::GetBackend().Name(), "test-shifted");
    EXPECT_NEAR(Ephemeris::Position(3, Ephemeris::kJ2000).rAu, baseline * 2.0, 1e-12);

    Ephemeris::SetBackend(nullptr);
    EXPECT_STREQ(Ephemeris::GetBackend().Name(), "standish");
    EXPECT_NEAR(Ephemeris::Position(3, Ephemeris::kJ2000).rAu, baseline, 1e-12);
}

namespace {

/**
 * Heliocentric ecliptic XYZ in AU, for comparison against JPL Horizons vectors
 * (CENTER='@sun', REF_PLANE=ECLIPTIC).
 */
void HelioXyz(int body, double jd, double out[3]) {
    const auto p = Ephemeris::Position(body, jd);
    const double cosLat = std::cos(p.latRad);
    out[0] = p.rAu * std::cos(p.lonRad) * cosLat;
    out[1] = p.rAu * std::sin(p.lonRad) * cosLat;
    out[2] = p.rAu * std::sin(p.latRad);
}

/** Angle between two heliocentric directions, in degrees. */
double AngularErrorDeg(int body, double jd, const double truth[3]) {
    double ours[3] = {0.0, 0.0, 0.0};
    HelioXyz(body, jd, ours);
    double dot = 0.0, n1 = 0.0, n2 = 0.0;
    for (int i = 0; i < 3; ++i) {
        dot += ours[i] * truth[i];
        n1 += ours[i] * ours[i];
        n2 += truth[i] * truth[i];
    }
    const double c = dot / (std::sqrt(n1) * std::sqrt(n2));
    return std::acos(std::max(-1.0, std::min(1.0, c))) * kDeg;
}

constexpr int kCeres = 10;
constexpr int kVesta = 11;

} // namespace

TEST(EphemerisTest, CeresMatchesHorizonsNearElementEpoch) {
    // JPL Horizons vectors, CENTER='@sun', REF_PLANE=ECLIPTIC, 2026-Sep-08 00:00 TDB.
    // Our elements are osculating at 2461200.5, so this is the best-case check.
    const double truth[3] = {5.216794036628920e-01, 2.640466903395465e+00, -1.251155432404318e-02};
    EXPECT_LT(AngularErrorDeg(kCeres, 2461291.5, truth), 0.01);
    EXPECT_NEAR(Ephemeris::Position(kCeres, 2461291.5).rAu, 2.690, 0.01);
}

TEST(EphemerisTest, VestaMatchesHorizonsNearElementEpoch) {
    // JPL Horizons, same setup: 4 Vesta at 2026-Sep-08 00:00 TDB.
    const double truth[3] = {2.376285375905351e+00, 4.468964363204065e-01, -3.026224361273567e-01};
    EXPECT_LT(AngularErrorDeg(kVesta, 2461291.5, truth), 0.01);
    EXPECT_NEAR(Ephemeris::Position(kVesta, 2461291.5).rAu, 2.438, 0.01);
}

TEST(EphemerisTest, BeltDwarfsStayUsableDecadesFromEpoch) {
    // Fixed osculating elements drift because nothing models Jupiter's perturbations.
    // These bounds are the measured error, not an aspiration — they document how far the
    // two-body approximation can be trusted before a real belt ephemeris is needed.
    const double ceres2035[3] = {2.818988763698951e+00, 6.153301732460975e-01, -4.986490697667665e-01};
    const double vesta2035[3] = {-4.878874003006368e-01, 2.501525994031174e+00, -1.431080418657751e-02};
    EXPECT_LT(AngularErrorDeg(kCeres, 2464328.5, ceres2035), 0.5); // 2035-01-01, ~8.5 yr out
    EXPECT_LT(AngularErrorDeg(kVesta, 2464328.5, vesta2035), 0.5);

    const double ceresJ2000[3] = {-2.379327628658312e+00, 7.954859974112578e-01, 4.630054514761865e-01};
    const double vestaJ2000[3] = {-1.353580318981305e+00, -1.673136663078244e+00, 2.149018119648995e-01};
    EXPECT_LT(AngularErrorDeg(kCeres, Ephemeris::kJ2000, ceresJ2000), 2.5); // 26 yr back
    EXPECT_LT(AngularErrorDeg(kVesta, Ephemeris::kJ2000, vestaJ2000), 2.0);
}

TEST(EphemerisTest, IndicesPastTheCatalogReturnZeros) {
    const auto beyond = Ephemeris::Position(12, Ephemeris::kJ2000);
    EXPECT_DOUBLE_EQ(beyond.rAu, 0.0);
    EXPECT_DOUBLE_EQ(Ephemeris::Position(-1, Ephemeris::kJ2000).rAu, 0.0);
}
