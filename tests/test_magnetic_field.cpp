#include <gtest/gtest.h>

#include "Auxiliary_Modules/MagneticFieldLineMesh.h"
#include "Auxiliary_Modules/MagneticFieldTracer.h"

#include <cmath>
#include <glm/geometric.hpp>

TEST(MagneticFieldTracerTest, DipolePointsOutwardNearNorthPole) {
    MagneticFieldParams params;
    params.enabled = true;
    params.dipoleMoment = 1.0f;
    params.toroidalStrength = 0.0f;

    const glm::vec3 pole(0.0f, 1.5f, 0.0f);
    const glm::vec3 b = MagneticFieldTracer::EvaluateB(pole, params);
    EXPECT_GT(b.y, 0.0f);
    EXPECT_LT(std::abs(b.x), std::abs(b.y) * 0.25f);
}

TEST(MagneticFieldTracerTest, DipoleIsMostlyHorizontalAtEquator) {
    MagneticFieldParams params;
    params.enabled = true;
    params.dipoleMoment = 1.0f;
    params.toroidalStrength = 0.0f;

    const glm::vec3 equator(1.5f, 0.0f, 0.0f);
    const glm::vec3 b = MagneticFieldTracer::EvaluateB(equator, params);
    EXPECT_LT(b.y, 0.0f);
    EXPECT_GT(std::abs(b.y), std::abs(b.x));
}

TEST(MagneticFieldTracerTest, SunTorusHasAzimuthalComponent) {
    MagneticFieldParams params;
    params.enabled = true;
    params.dipoleMoment = 0.2f;
    params.toroidalStrength = 2.0f;
    params.torusRadiusScale = 1.55f;

    const glm::vec3 torusPoint(1.55f, 0.0f, 0.0f);
    const glm::vec3 b = MagneticFieldTracer::EvaluateB(torusPoint, params);
    EXPECT_GT(std::abs(b.z), 0.2f);
}

TEST(MagneticFieldTracerTest, TraceProducesMonotonicArcLength) {
    MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Earth, 1);
    ASSERT_TRUE(params.enabled);

    const auto lines = MagneticFieldTracer::Trace(params);
    ASSERT_GE(lines.size(), 4u);

    int finite = 0;
    for (const auto& line : lines) {
        ASSERT_GE(line.samples.size(), 3u);
        for (std::size_t i = 1; i < line.samples.size(); ++i) {
            EXPECT_GE(line.samples[i].arcLength, line.samples[i - 1].arcLength - 1.0e-5f);
            EXPECT_TRUE(std::isfinite(line.samples[i].position.x));
            EXPECT_TRUE(std::isfinite(line.samples[i].position.y));
            EXPECT_TRUE(std::isfinite(line.samples[i].position.z));
        }
        EXPECT_NEAR(line.samples.front().arcLength, 0.0f, 1.0e-4f);
        EXPECT_NEAR(line.samples.back().arcLength, 1.0f, 1.0e-3f);
        ++finite;
    }
    EXPECT_GE(finite, 4);
}

TEST(MagneticFieldCatalogTest, WeakBodiesDisabled) {
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Venus, 2).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Mars, 2).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Pluto, 2).enabled);
    EXPECT_TRUE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Sun, 0).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Earth, 0).enabled);
    EXPECT_TRUE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Earth, 1).enabled);
    EXPECT_TRUE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Uranus, 1).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Mercury, 1).enabled);
    EXPECT_TRUE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Mercury, 2).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Saturn, 1).enabled);
    EXPECT_TRUE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Saturn, 2).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Neptune, 1).enabled);
    EXPECT_TRUE(MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Neptune, 2).enabled);
}

TEST(MagneticFieldCatalogTest, IntrinsicValuesAreEducationalDefaults) {
    const auto sun = MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Sun);
    const auto earth = MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Earth);
    const auto jupiter = MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Jupiter);
    const auto uranus = MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Uranus);

    EXPECT_TRUE(sun.enabled);
    EXPECT_NEAR(sun.dipoleTiltDeg, 7.25f, 1.0e-4f);
    EXPECT_GT(sun.toroidalStrength, 2.0f);
    EXPECT_GT(sun.flowSpeed, earth.flowSpeed);

    EXPECT_TRUE(earth.enabled);
    EXPECT_NEAR(earth.dipoleTiltDeg, 11.0f, 1.0e-4f);
    EXPECT_NEAR(earth.toroidalStrength, 0.0f, 1.0e-4f);

    EXPECT_TRUE(jupiter.enabled);
    EXPECT_NEAR(jupiter.dipoleTiltDeg, 10.0f, 1.0e-4f);
    EXPECT_GT(jupiter.dipoleMoment, earth.dipoleMoment);

    EXPECT_TRUE(uranus.enabled);
    EXPECT_NEAR(uranus.dipoleTiltDeg, 59.0f, 1.0e-4f);

    EXPECT_FALSE(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Venus).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Mars).enabled);
    EXPECT_FALSE(MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Pluto).enabled);
}

TEST(MagneticFieldRibbonTest, ExpandCreatesTwoTrianglesPerSegment) {
    MagneticFieldLine line;
    line.samples = {
        {{0.0f, 0.0f, 0.0f}, 0.0f},
        {{1.0f, 0.0f, 0.0f}, 0.5f},
        {{2.0f, 0.0f, 0.0f}, 1.0f},
    };

    const auto verts = MagneticFieldLineMesh::Expand({line});
    ASSERT_EQ(verts.size(), 12u);

    EXPECT_FLOAT_EQ(verts[0].lineUV, -1.0f);
    EXPECT_FLOAT_EQ(verts[1].lineUV, 1.0f);
    EXPECT_FLOAT_EQ(verts[0].arcLength, 0.0f);
    EXPECT_FLOAT_EQ(verts[2].arcLength, 0.5f);
    EXPECT_NEAR(verts[0].tangent.x, 1.0f, 1.0e-5f);
    EXPECT_NEAR(glm::length(verts[0].tangent), 1.0f, 1.0e-5f);
}

TEST(MagneticFieldRibbonTest, ExpandSkipsDegenerateSegments) {
    MagneticFieldLine line;
    line.samples = {
        {{0.0f, 0.0f, 0.0f}, 0.0f},
        {{0.0f, 0.0f, 0.0f}, 0.0f},
        {{0.0f, 1.0f, 0.0f}, 1.0f},
    };
    const auto verts = MagneticFieldLineMesh::Expand({line});
    ASSERT_EQ(verts.size(), 6u);
    EXPECT_NEAR(verts[0].tangent.y, 1.0f, 1.0e-5f);
}

TEST(MagneticFieldRibbonTest, TracedEarthLinesExpandToQuads) {
    MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Earth, 1);
    ASSERT_TRUE(params.enabled);
    const auto lines = MagneticFieldTracer::Trace(params);
    const auto verts = MagneticFieldLineMesh::Expand(lines);
    EXPECT_GT(verts.size(), 24u);
    EXPECT_EQ(verts.size() % 6u, 0u);
}

TEST(MagneticFieldCatalogTest, SunAnimatesFasterThanEarth) {
    const auto sun = MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Sun);
    const auto earth = MagneticFieldCatalog::IntrinsicParamsForBody(OrbitLayout::Body::Earth);
    EXPECT_GT(sun.flowSpeed, earth.flowSpeed);
    EXPECT_GT(sun.opacity, 0.0f);
    EXPECT_GT(earth.opacity, 0.0f);
}
