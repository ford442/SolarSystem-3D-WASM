#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "astro/Ephemeris.h"
#include "astro/Observer.h"
#include "astro/StarCatalog.h"

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

const std::string kCatalogPath = std::string(SOLARSYSTEM_SOURCE_DIR) + "/resource/sky/bright_stars.json";

/** Index of the named star, or -1. */
int indexOfName(const StarCatalog::Catalog& catalog, const std::string& name) {
    for (const auto& [index, candidate] : catalog.names) {
        if (candidate == name) {
            return index;
        }
    }
    return -1;
}

const char* kTinyCatalog = R"({
"v":1,"epoch":"J2000","count":3,
"fields":["raDeg","decDeg","vmag","bv"],
"names":{"0":"Sirius","2":"Faint One"},
"data":[
101.287,-16.716,-1.46,0.00,
37.953,89.264,2.02,0.60,
10.0,-5.5,5.50,1.20]
})";

} // namespace

TEST(StarCatalogTest, ParsesTheGeneratorShape) {
    StarCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(StarCatalog::Parse(kTinyCatalog, catalog, error)) << error;
    ASSERT_EQ(catalog.stars.size(), 3u);
    EXPECT_NEAR(catalog.stars[0].raDeg, 101.287f, 1e-3f);
    EXPECT_NEAR(catalog.stars[0].decDeg, -16.716f, 1e-3f);
    EXPECT_NEAR(catalog.stars[0].vmag, -1.46f, 1e-3f);
    EXPECT_NEAR(catalog.stars[2].bv, 1.2f, 1e-3f);
    EXPECT_EQ(catalog.names.at(0), "Sirius");
    EXPECT_EQ(catalog.names.at(2), "Faint One");
    EXPECT_EQ(catalog.names.count(1), 0u);
}

TEST(StarCatalogTest, RejectsMalformedInput) {
    StarCatalog::Catalog catalog;
    std::string error;
    EXPECT_FALSE(StarCatalog::Parse("", catalog, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(catalog.stars.empty());
    EXPECT_FALSE(StarCatalog::Parse("{\"data\":[1,2,3,4]}", catalog, error));   // no fields
    EXPECT_FALSE(StarCatalog::Parse(
        R"({"fields":["vmag","bv","raDeg","decDeg"],"data":[1,2,3,4]})", catalog, error)); // wrong order
    EXPECT_FALSE(StarCatalog::Parse(
        R"({"fields":["raDeg","decDeg","vmag","bv"],"data":[1,2,3]})", catalog, error));   // not 4-tuples
    EXPECT_FALSE(StarCatalog::Parse(
        R"({"fields":["raDeg","decDeg","vmag","bv"],"data":[1,2,3,x]})", catalog, error)); // bad number
    EXPECT_FALSE(StarCatalog::Parse(
        R"({"fields":["raDeg","decDeg","vmag","bv"],"data":[400,2,3,0.5]})", catalog, error)); // RA out of range
    EXPECT_FALSE(StarCatalog::Parse(
        R"({"fields":["raDeg","decDeg","vmag","bv"],"data":[1,2,3,0.5,1,2,1.0,0.5]})", catalog, error)); // unsorted
    EXPECT_FALSE(StarCatalog::Parse(
        R"({"fields":["raDeg","decDeg","vmag","bv"],"data":[1,2,3,0.5)", catalog, error)); // unterminated
}

TEST(StarCatalogTest, ShippedCatalogIsSortedAndComplete) {
    StarCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(StarCatalog::LoadFromFile(kCatalogPath, catalog, error)) << error;
    EXPECT_EQ(catalog.stars.size(), 5000u);
    for (size_t i = 1; i < catalog.stars.size(); ++i) {
        ASSERT_GE(catalog.stars[i].vmag, catalog.stars[i - 1].vmag) << "row " << i;
    }
    // A tier's prefix must still contain the brightest stars.
    EXPECT_LT(catalog.stars.front().vmag, -1.0f);
    EXPECT_LT(catalog.stars[499].vmag, 4.0f);
    EXPECT_LT(catalog.stars.back().vmag, 6.5f);
    EXPECT_GE(catalog.names.size(), 50u);
}

TEST(StarCatalogTest, NamedStarsSitWhereTheyShould) {
    StarCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(StarCatalog::LoadFromFile(kCatalogPath, catalog, error)) << error;

    struct Expected { const char* name; double ra, dec, vmag; };
    const Expected expected[] = {
        {"Sirius", 101.287, -16.716, -1.46},   {"Vega", 279.235, 38.784, 0.03},
        {"Betelgeuse", 88.793, 7.407, 0.50},   {"Polaris", 37.954, 89.264, 2.02},
        {"Rigil Kentaurus", 219.90, -60.835, -0.01}, {"Antares", 247.352, -26.432, 0.96},
    };
    for (const Expected& e : expected) {
        const int index = indexOfName(catalog, e.name);
        ASSERT_GE(index, 0) << e.name;
        const StarCatalog::Star& star = catalog.stars[static_cast<size_t>(index)];
        EXPECT_NEAR(star.raDeg, e.ra, 0.01) << e.name;
        EXPECT_NEAR(star.decDeg, e.dec, 0.01) << e.name;
        EXPECT_NEAR(star.vmag, e.vmag, 0.01) << e.name;
    }
}

TEST(StarCatalogTest, StarColoursFollowBV) {
    float blue[3], white[3], red[3];
    StarCatalog::ColorFromBV(-0.25f, blue);
    StarCatalog::ColorFromBV(0.6f, white);
    StarCatalog::ColorFromBV(1.85f, red);
    EXPECT_GT(blue[2], blue[0]);   // hot stars lean blue
    EXPECT_GT(red[0], red[2]);     // cool stars lean red
    EXPECT_GT(red[0], red[1]);
    for (const float* c : {blue, white, red}) {
        for (int i = 0; i < 3; ++i) {
            EXPECT_GE(c[i], 0.0f);
            EXPECT_LE(c[i], 1.0f);
        }
    }
    // Softened toward white: even the reddest channel mix keeps some of every colour.
    EXPECT_GT(red[2], 0.4f);
    EXPECT_GT(blue[0], 0.4f);
}

// Polaris sits ~0.7 deg from the pole, so its altitude is the observer's latitude to within a
// degree. This exercises catalog → J2000 vector → Observer::HorizonFromEquatorialJ2000.
TEST(StarCatalogTest, PolarisAltitudeIsRoughlyTheLatitude) {
    StarCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(StarCatalog::LoadFromFile(kCatalogPath, catalog, error)) << error;
    const int index = indexOfName(catalog, "Polaris");
    ASSERT_GE(index, 0);
    const StarCatalog::Star& polaris = catalog.stars[static_cast<size_t>(index)];

    const double ra = polaris.raDeg * kDegToRad;
    const double dec = polaris.decDeg * kDegToRad;
    const double v[3] = {std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec)};

    for (const double lat : {20.0, 42.87, 52.0, 65.0}) {
        const Observer::Site site{lat, -106.31, 0.0};
        for (const double day : {0.0, 0.25, 0.5, 0.75}) {
            const double jd = Ephemeris::JulianDateFromYmd(2017, 8, 21) + day;
            double h[3];
            Observer::HorizonFromEquatorialJ2000(jd, site).Apply(v, h);
            double alt = 0.0, az = 0.0;
            Observer::AltAzFromDirection(h, alt, az);
            EXPECT_NEAR(alt, lat, 1.2) << "lat " << lat << " day " << day;
            // ...and it stays within a few degrees of due north.
            const double fromNorth = std::fmod(az + 180.0, 360.0) - 180.0;
            EXPECT_LT(std::fabs(fromNorth), lat < 30.0 ? 60.0 : 25.0) << "lat " << lat;
        }
    }
}

// --- Constellation lines --------------------------------------------------------------------

namespace {

const std::string kConstellationPath = std::string(SOLARSYSTEM_SOURCE_DIR) + "/resource/sky/constellation_lines.json";

double separationDeg(const StarCatalog::Star& a, const StarCatalog::Star& b) {
    const double cosSep = std::sin(a.decDeg * kDegToRad) * std::sin(b.decDeg * kDegToRad) +
                          std::cos(a.decDeg * kDegToRad) * std::cos(b.decDeg * kDegToRad) *
                              std::cos((a.raDeg - b.raDeg) * kDegToRad);
    return std::acos(std::fmin(1.0, std::fmax(-1.0, cosSep))) / kDegToRad;
}

bool hasSegment(const std::vector<StarCatalog::Constellation>& figures, const char* id, int a, int b) {
    for (const auto& figure : figures) {
        if (figure.id != id) {
            continue;
        }
        for (size_t i = 0; i + 1 < figure.lines.size(); i += 2) {
            if ((figure.lines[i] == a && figure.lines[i + 1] == b) || (figure.lines[i] == b && figure.lines[i + 1] == a)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

TEST(ConstellationLinesTest, ParsesTheGeneratorShapeAndRejectsMisjoinedFiles) {
    const std::string tiny = R"({"v":1,"starCount":3,"constellations":[
  {"id":"Ori","name":"Orion","core":true,"lines":[0,1,1,2]},
  {"id":"CMi","name":"Canis Minor","core":false,"lines":[2,0]}
]})";
    std::vector<StarCatalog::Constellation> figures;
    std::string error;
    ASSERT_TRUE(StarCatalog::ParseConstellations(tiny, 3, figures, error)) << error;
    ASSERT_EQ(figures.size(), 2u);
    EXPECT_EQ(figures[0].id, "Ori");
    EXPECT_TRUE(figures[0].core);
    EXPECT_EQ(figures[0].lines, (std::vector<int>{0, 1, 1, 2}));
    EXPECT_FALSE(figures[1].core);

    EXPECT_FALSE(StarCatalog::ParseConstellations(tiny, 4, figures, error)); // star file regenerated alone
    EXPECT_TRUE(figures.empty());
    const std::string outOfRange = R"({"starCount":3,"constellations":[{"id":"X","core":true,"lines":[0,3]}]})";
    EXPECT_FALSE(StarCatalog::ParseConstellations(outOfRange, 3, figures, error));
    const std::string odd = R"({"starCount":3,"constellations":[{"id":"X","core":true,"lines":[0,1,2]}]})";
    EXPECT_FALSE(StarCatalog::ParseConstellations(odd, 3, figures, error));
    EXPECT_FALSE(StarCatalog::ParseConstellations("{}", 3, figures, error));
}

TEST(ConstellationLinesTest, ShippedFiguresJoinTheShippedStars) {
    StarCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(StarCatalog::LoadFromFile(kCatalogPath, catalog, error)) << error;
    std::vector<StarCatalog::Constellation> figures;
    ASSERT_TRUE(StarCatalog::LoadConstellationsFromFile(kConstellationPath, static_cast<int>(catalog.stars.size()),
                                                        figures, error)) << error;

    int core = 0, segments = 0, coreSegmentsOnLow = 0;
    for (const auto& figure : figures) {
        core += figure.core ? 1 : 0;
        for (size_t i = 0; i + 1 < figure.lines.size(); i += 2) {
            const auto& a = catalog.stars[static_cast<size_t>(figure.lines[i])];
            const auto& b = catalog.stars[static_cast<size_t>(figure.lines[i + 1])];
            EXPECT_LT(separationDeg(a, b), 32.0) << figure.id;
            EXPECT_LT(std::fmax(a.vmag, b.vmag), 5.5f) << figure.id; // naked-eye figures only
            ++segments;
            if (figure.core && figure.lines[i] < 500 && figure.lines[i + 1] < 500) {
                ++coreSegmentsOnLow; // Low draws 500 stars; a line needs both of its stars drawn
            }
        }
    }
    EXPECT_EQ(core, 20);
    EXPECT_GE(segments, 150);
    EXPECT_GE(coreSegmentsOnLow, 60);

    // Orion's belt and the Big Dipper's pointers, by their catalog names.
    EXPECT_TRUE(hasSegment(figures, "Ori", indexOfName(catalog, "Alnitak"), indexOfName(catalog, "Alnilam")));
    EXPECT_TRUE(hasSegment(figures, "Ori", indexOfName(catalog, "Alnilam"), indexOfName(catalog, "Mintaka")));
    EXPECT_TRUE(hasSegment(figures, "UMa", indexOfName(catalog, "Dubhe"), indexOfName(catalog, "Merak")));
}
