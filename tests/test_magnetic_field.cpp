#include <gtest/gtest.h>

#include "Auxiliary_Modules/MagneticFieldLineMesh.h"
#include "Auxiliary_Modules/MagneticFieldModel.h"
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
    EXPECT_GT(sun.toroidalStrength, 2.5f);
    EXPECT_GT(sun.flowSpeed, earth.flowSpeed);

    EXPECT_TRUE(earth.enabled);
    EXPECT_NEAR(earth.dipoleTiltDeg, 11.0f, 1.0e-4f);
    EXPECT_NEAR(earth.toroidalStrength, 0.0f, 1.0e-4f);

    EXPECT_TRUE(jupiter.enabled);
    EXPECT_NEAR(jupiter.dipoleTiltDeg, 10.0f, 1.0e-4f);
    EXPECT_GT(jupiter.dipoleMoment, earth.dipoleMoment);

    EXPECT_TRUE(uranus.enabled);
    EXPECT_NEAR(uranus.dipoleTiltDeg, 59.0f, 1.0e-4f);
    EXPECT_GT(uranus.ribbonWidth, earth.ribbonWidth);

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

namespace {

float WrapDeltaPhi(float a, float b) {
    float d = b - a;
    constexpr float kPi = 3.14159265358979323846f;
    while (d > kPi) {
        d -= 2.0f * kPi;
    }
    while (d < -kPi) {
        d += 2.0f * kPi;
    }
    return d;
}

float MeanAbsDphi(const MagneticFieldLine& line) {
    if (line.samples.size() < 2) {
        return 0.0f;
    }
    float sum = 0.0f;
    int n = 0;
    for (std::size_t i = 1; i < line.samples.size(); ++i) {
        const auto& a = line.samples[i - 1].position;
        const auto& b = line.samples[i].position;
        const float phi0 = std::atan2(a.z, a.x);
        const float phi1 = std::atan2(b.z, b.x);
        sum += std::abs(WrapDeltaPhi(phi0, phi1));
        ++n;
    }
    return n > 0 ? sum / static_cast<float>(n) : 0.0f;
}

} // namespace

TEST(MagneticFieldTracerTest, DipoleTracesAreClosedIsh) {
    MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Earth, 1);
    ASSERT_TRUE(params.enabled);

    const auto lines = MagneticFieldTracer::Trace(params);
    ASSERT_GE(lines.size(), 4u);

    int closed = 0;
    for (const auto& line : lines) {
        ASSERT_GE(line.samples.size(), 3u);
        const float r0 = glm::length(line.samples.front().position);
        const float r1 = glm::length(line.samples.back().position);
        if (r0 >= 1.0f && r0 <= 1.2f && r1 >= 1.0f && r1 <= 1.2f) {
            ++closed;
        }
    }
    EXPECT_GE(closed, 4);
}

TEST(MagneticFieldTracerTest, HighToroidalTwistsAzimuth) {
    MagneticFieldParams dipole;
    dipole.enabled = true;
    dipole.dipoleMoment = 0.55f;
    dipole.toroidalStrength = 0.0f;
    dipole.torusRadiusScale = 1.55f;
    dipole.extentScale = 7.5f;
    dipole.seedCount = 16;
    dipole.samplesPerLine = 80;

    MagneticFieldParams torus = dipole;
    torus.toroidalStrength = 2.4f;

    const auto dipoleLines = MagneticFieldTracer::Trace(dipole);
    const auto torusLines = MagneticFieldTracer::Trace(torus);
    ASSERT_FALSE(dipoleLines.empty());
    ASSERT_FALSE(torusLines.empty());
    ASSERT_EQ(dipoleLines.size(), torusLines.size());

    float dipoleMean = 0.0f;
    float torusMean = 0.0f;
    for (std::size_t i = 0; i < dipoleLines.size(); ++i) {
        dipoleMean += MeanAbsDphi(dipoleLines[i]);
        torusMean += MeanAbsDphi(torusLines[i]);
    }
    dipoleMean /= static_cast<float>(dipoleLines.size());
    torusMean /= static_cast<float>(torusLines.size());
    EXPECT_GT(torusMean, dipoleMean * 2.0f);
    EXPECT_GT(torusMean, 0.01f);
}

TEST(MagneticFieldModelTest, SampleFieldMatchesEvaluateB) {
    MagneticFieldParams params;
    params.enabled = true;
    params.dipoleMoment = 1.0f;
    params.toroidalStrength = 1.5f;
    params.torusRadiusScale = 1.55f;

    const glm::vec3 points[] = {
        {0.0f, 1.5f, 0.0f},
        {1.5f, 0.0f, 0.0f},
        {1.1f, 0.4f, 0.7f},
    };
    for (const auto& p : points) {
        const glm::vec3 a = MagneticFieldModel::SampleField(p, params);
        const glm::vec3 b = MagneticFieldTracer::EvaluateB(p, params);
        EXPECT_NEAR(a.x, b.x, 1.0e-6f);
        EXPECT_NEAR(a.y, b.y, 1.0e-6f);
        EXPECT_NEAR(a.z, b.z, 1.0e-6f);
    }
}

TEST(MagneticFieldModelTest, TiltMatrixRotatesAxis) {
    const glm::vec3 y(0.0f, 1.0f, 0.0f);
    const glm::vec3 tilted = MagneticFieldModel::TiltMatrix(90.0f) * y;
    EXPECT_NEAR(tilted.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(tilted.y, 0.0f, 1.0e-5f);
    EXPECT_NEAR(tilted.z, 1.0f, 1.0e-5f);

    const glm::vec3 identity = MagneticFieldModel::TiltMatrix(0.0f) * y;
    EXPECT_NEAR(identity.x, 0.0f, 1.0e-5f);
    EXPECT_NEAR(identity.y, 1.0f, 1.0e-5f);
    EXPECT_NEAR(identity.z, 0.0f, 1.0e-5f);
}

TEST(MagneticFieldTracerTest, DisabledBodyEmitsNoLines) {
    MagneticFieldParams params;
    params.enabled = false;
    params.dipoleMoment = 1.0f;
    params.seedCount = 16;
    params.samplesPerLine = 64;
    EXPECT_TRUE(MagneticFieldTracer::Trace(params).empty());
}

TEST(MagneticFieldTracerTest, CapsRespectBudget) {
    MagneticFieldParams params;
    params.enabled = true;
    params.dipoleMoment = 1.0f;
    params.extentScale = 4.0f;
    params.seedCount = 1000;
    params.samplesPerLine = 1000;

    const auto lines = MagneticFieldTracer::Trace(params);
    ASSERT_FALSE(lines.empty());
    EXPECT_LE(lines.size(), 64u);
    for (const auto& line : lines) {
        // samplesPerLine is a per-direction RK4 cap (forward + backward + seed).
        EXPECT_LE(line.samples.size(), 256u * 2u + 1u);
    }
}
