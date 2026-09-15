#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include <glm/glm.hpp>

#include "Auxiliary_Modules/MissionCatalog.h"

namespace {

const char* kCatalog = R"({
  "version": 1,
  "units": "au",
  "note": "test fixture",
  "missions": [
    {
      "id": "voyager1",
      "name": "Voyager 1",
      "color": [1.0, 0.5, 0.2],
      "samples": [
        [2440000.0, 1.0, 0.0, 0.0],
        [2440010.0, 3.0, 0.0, 0.0],
        [2440020.0, 5.0, 2.0, 0.0],
        [2440030.0, 7.0, 2.0, 0.0],
        [2440040.0, 9.0, 2.0, 0.0]
      ]
    }
  ]
})";

} // namespace

TEST(MissionCatalogTest, LoadsMissionsAndSkipsUnknownKeys) {
    MissionCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(MissionCatalog::LoadFromString(kCatalog, catalog, error)) << error;
    ASSERT_EQ(catalog.missions.size(), 1u);
    EXPECT_EQ(catalog.version, 1);
    EXPECT_EQ(catalog.missions[0].id, "voyager1");
    EXPECT_EQ(catalog.missions[0].name, "Voyager 1");
    EXPECT_EQ(catalog.missions[0].samples.size(), 5u);
    EXPECT_FLOAT_EQ(catalog.missions[0].color.x, 1.0f);
    EXPECT_EQ(MissionCatalog::IndexById(catalog, "voyager1"), 0);
    EXPECT_EQ(MissionCatalog::IndexById(catalog, "missing"), -1);
}

TEST(MissionCatalogTest, InterpolatesAndClamps) {
    MissionCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(MissionCatalog::LoadFromString(kCatalog, catalog, error));
    const auto& mission = catalog.missions[0];
    glm::vec3 au{0.0f};
    ASSERT_TRUE(MissionCatalog::InterpolateAu(mission, 2440005.0, au));
    EXPECT_NEAR(au.x, 2.0f, 1.0e-4f);
    ASSERT_TRUE(MissionCatalog::InterpolateAu(mission, 2430000.0, au));
    EXPECT_NEAR(au.x, 1.0f, 1.0e-4f);
    ASSERT_TRUE(MissionCatalog::InterpolateAu(mission, 2450000.0, au));
    EXPECT_NEAR(au.x, 9.0f, 1.0e-4f);
}

TEST(MissionCatalogTest, DownsampleKeepsEndpoints) {
    MissionCatalog::Catalog catalog;
    std::string error;
    ASSERT_TRUE(MissionCatalog::LoadFromString(kCatalog, catalog, error));
    EXPECT_EQ(MissionCatalog::SampleStrideForQuality(0), 4);
    EXPECT_EQ(MissionCatalog::SampleStrideForQuality(2), 1);
    const auto low = MissionCatalog::DownsampledAu(catalog.missions[0], 0);
    ASSERT_GE(low.size(), 2u);
    EXPECT_NEAR(low.front().x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(low.back().x, 9.0f, 1.0e-4f);
    const auto full = MissionCatalog::DownsampledAu(catalog.missions[0], 2);
    EXPECT_EQ(full.size(), 5u);
}

TEST(MissionCatalogTest, RejectsInvalidRoot) {
    MissionCatalog::Catalog catalog;
    std::string error;
    EXPECT_FALSE(MissionCatalog::LoadFromString("[]", catalog, error));
    EXPECT_FALSE(error.empty());
}
