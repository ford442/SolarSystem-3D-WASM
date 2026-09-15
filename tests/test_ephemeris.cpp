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

// --- Keplerian satellites -------------------------------------------------------------
//
// Reference vectors are JPL Horizons parent-relative state vectors (EPHEM_TYPE=VECTORS,
// REF_PLANE=ECLIPTIC, OUT_UNITS=AU-D) at three epochs: J2000.0, 2026-01-01 and 2035-01-01
// TDB. The catalog elements themselves are a least-squares fit of Horizons osculating
// elements over 2000-2036, so these checks are a genuine hold-out only at the ends of that
// span; they exist to catch a regression in the propagator or a corrupted catalog row, not
// to prove the model.
//
// Tolerances below are the measured error rounded up, not aspirations. The Moon's is the
// loosest because its row carries only secular rates: the periodic terms (evection 1.27
// deg, variation 0.66 deg) are not modelled. Those terms very nearly cancel at syzygy,
// which is why the eclipse searches in test_sky_events.cpp land on the right day anyway.

namespace {

struct SatelliteReference {
    int satelliteId;
    double julianDate;
    double xyzAu[3];
    double angleToleranceDeg;
    double radiusTolerancePercent;
};

/** Angle between the model's parent-relative direction and a reference vector. */
double SatelliteAngularErrorDeg(int satelliteId, double jd, const double truth[3]) {
    double p[3] = {0.0, 0.0, 0.0};
    EXPECT_TRUE(Ephemeris::SatellitePosition(satelliteId, jd, p)) << "no solution for " << satelliteId;

    const double dotProduct = p[0] * truth[0] + p[1] * truth[1] + p[2] * truth[2];
    const double modelLength = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    const double truthLength = std::sqrt(truth[0] * truth[0] + truth[1] * truth[1] + truth[2] * truth[2]);
    if (modelLength < 1e-12 || truthLength < 1e-12) {
        return 180.0;
    }
    return std::acos(std::clamp(dotProduct / (modelLength * truthLength), -1.0, 1.0)) * kDeg;
}

double SatelliteRadiusErrorPercent(int satelliteId, double jd, const double truth[3]) {
    double p[3] = {0.0, 0.0, 0.0};
    Ephemeris::SatellitePosition(satelliteId, jd, p);
    const double modelLength = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    const double truthLength = std::sqrt(truth[0] * truth[0] + truth[1] * truth[1] + truth[2] * truth[2]);
    return 100.0 * (modelLength - truthLength) / truthLength;
}

constexpr int kMoonId = 12;
constexpr int kIoId = 15;
constexpr int kEuropaId = 16;
constexpr int kGanymedeId = 17;
constexpr int kCallistoId = 18;
constexpr int kTitanId = 24;
constexpr int kTritonId = 31;

constexpr SatelliteReference kSatelliteReferences[] = {
    {kMoonId, 2451545.0, {-1.949281649687e-03, -1.838126040073e-03, 2.424579738821e-04}, 1.0, 1.0},
    {kMoonId, 2461041.5, {9.647578994046e-04, 2.201875129334e-03, 2.122555367575e-04}, 2.5, 1.0},
    {kMoonId, 2464328.5, {-2.617952893689e-03, -2.350614012334e-04, 7.383486146070e-05}, 1.0, 1.0},
    {kIoId, 2451545.0, {2.671924636756e-03, 8.640941902565e-04, 7.127946422890e-05}, 0.1, 0.5},
    {kIoId, 2461041.5, {2.481204857656e-03, -1.345564381596e-03, -1.316300073474e-05}, 0.7, 0.5},
    {kIoId, 2464328.5, {2.129236690923e-03, -1.829581848466e-03, -3.572002024210e-05}, 0.2, 0.5},
    {kEuropaId, 2451545.0, {-3.751687581656e-03, -2.379800306953e-03, -1.200157298708e-04}, 0.1, 0.5},
    {kEuropaId, 2461041.5, {5.518136355496e-04, -4.475513211863e-03, -1.276109045920e-04}, 0.4, 1.5},
    {kEuropaId, 2464328.5, {-3.233285031901e-03, 3.050338658898e-03, 6.926258179308e-05}, 0.5, 0.5},
    {kGanymedeId, 2451545.0, {-5.490352844042e-03, -4.581541308862e-03, -2.310042060536e-04}, 0.2, 0.5},
    {kGanymedeId, 2461041.5, {6.763975676560e-03, -2.298529012985e-03, 1.018737015836e-05}, 0.2, 0.5},
    {kGanymedeId, 2464328.5, {-5.099024701809e-03, 5.042806439493e-03, 1.075877459631e-04}, 0.2, 0.5},
    {kCallistoId, 2451545.0, {2.173023781101e-03, 1.238159356838e-02, 4.328588775702e-04}, 0.2, 0.5},
    {kCallistoId, 2461041.5, {6.945938950933e-05, 1.256119740939e-02, 3.940643301920e-04}, 0.2, 0.5},
    {kCallistoId, 2464328.5, {3.513764304536e-03, 1.202674587744e-02, 4.194480242626e-04}, 0.2, 0.5},
    {kTitanId, 2451545.0, {-6.328986729681e-03, 5.126196169185e-03, -2.025162432186e-03}, 0.1, 0.5},
    {kTitanId, 2461041.5, {7.432267262005e-03, -2.701769821021e-03, 6.536458357409e-04}, 0.1, 0.5},
    {kTitanId, 2464328.5, {6.658404712470e-03, 3.614083439085e-03, -2.528692369736e-03}, 0.1, 0.5},
    {kTritonId, 2451545.0, {-1.374996004324e-03, 8.293000645146e-04, 1.744682412714e-03}, 1.0, 0.5},
    {kTritonId, 2461041.5, {-1.902344209967e-03, -2.019903124772e-04, 1.401138892349e-03}, 0.5, 0.5},
    {kTritonId, 2464328.5, {1.323893487283e-03, 1.901619930944e-03, 5.038364888021e-04}, 0.3, 0.5},
};

} // namespace

TEST(EphemerisTest, KeplerianSatellitesMatchHorizons) {
    for (const SatelliteReference& reference : kSatelliteReferences) {
        SCOPED_TRACE(testing::Message() << "satellite " << reference.satelliteId
                                        << " at JD " << reference.julianDate);
        EXPECT_LT(SatelliteAngularErrorDeg(reference.satelliteId, reference.julianDate, reference.xyzAu),
                  reference.angleToleranceDeg);
        EXPECT_LT(std::fabs(SatelliteRadiusErrorPercent(reference.satelliteId, reference.julianDate,
                                                        reference.xyzAu)),
                  reference.radiusTolerancePercent);
    }
}

TEST(EphemerisTest, SatelliteOrbitsAreInclinedNotFlat) {
    // The whole point of the Keplerian path: a moon has to leave its parent's equatorial
    // plane, or a lunar umbra would strike Earth every single month.
    double moon[3] = {0.0, 0.0, 0.0};
    double maxAbsZ = 0.0;
    for (double day = 0.0; day < 30.0; day += 0.25) {
        ASSERT_TRUE(Ephemeris::SatellitePosition(12, Ephemeris::kJ2000 + day, moon));
        maxAbsZ = std::max(maxAbsZ, std::fabs(moon[2]));
    }
    // 5.15 deg of inclination over ~0.00257 AU is ~0.00023 AU out of plane at the extremes.
    EXPECT_GT(maxAbsZ, 1.5e-4);
}

TEST(EphemerisTest, SatellitesWithoutMeasuredElementsHaveNoSolution) {
    // Rows still carrying placeholder node/periapsis angles must stay on the circular
    // SatelliteOrbit path rather than being placed in a confidently wrong plane.
    double xyz[3] = {1.0, 1.0, 1.0};
    EXPECT_FALSE(Ephemeris::SatellitePosition(13, Ephemeris::kJ2000, xyz)); // Phobos
    EXPECT_DOUBLE_EQ(xyz[0], 0.0);
    EXPECT_FALSE(Ephemeris::SatellitePosition(19, Ephemeris::kJ2000, xyz)); // Mimas
    EXPECT_FALSE(Ephemeris::SatellitePosition(32, Ephemeris::kJ2000, xyz)); // Charon
    EXPECT_FALSE(Ephemeris::SatellitePosition(3, Ephemeris::kJ2000, xyz));  // Earth is not a moon
    EXPECT_FALSE(Ephemeris::SatellitePosition(-1, Ephemeris::kJ2000, xyz));
}

TEST(EphemerisTest, GreenwichMeanSiderealTimeMatchesTheStandardEpochValue) {
    // GMST at J2000.0 is 280.46061837 deg — the constant term of the IAU 1982 series, and
    // the value every sidereal-time reference quotes for that instant.
    EXPECT_NEAR(Ephemeris::GreenwichMeanSiderealTimeDeg(Ephemeris::kJ2000), 280.46061837, 1e-5);

    // One mean solar day advances GMST by one full turn plus ~3m56s of sidereal gain.
    const double oneDayLater = Ephemeris::GreenwichMeanSiderealTimeDeg(Ephemeris::kJ2000 + 1.0);
    EXPECT_NEAR(oneDayLater, std::fmod(280.46061837 + 360.98564736629, 360.0), 1e-4);

    for (double day = 0.0; day < 4000.0; day += 137.0) {
        const double gmst = Ephemeris::GreenwichMeanSiderealTimeDeg(Ephemeris::kJ2000 + day);
        EXPECT_GE(gmst, 0.0);
        EXPECT_LT(gmst, 360.0);
    }
}
