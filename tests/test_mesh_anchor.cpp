#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Solar_System/SceneScale.h"
#include "gfx/MeshAnchor.h"

namespace {

constexpr double kMetre = 1.0e-3 / SceneScale::kKmPerSceneUnit;

struct Scene {
    glm::dvec3 bodyWorld;
    glm::mat4 model; // as Transformable builds it: render-frame translation, then shape
    glm::vec3 vertex; // a mesh vertex near the eye (model space, float as uploaded)
    glm::dvec3 eyeWorld;
};

/**
 * Realistic Earth (sphere.obj radius 2 at 6371 km) at 1 AU, spun and tilted, with the eye
 * `metres` above the surface right over a mesh vertex. The render origin is on the eye.
 */
Scene EarthWithEyeAbove(double metres) {
    Scene scene;
    scene.bodyWorld = glm::dvec3(-1650.123456, 12.5, 940.25);
    const float scale = static_cast<float>(SceneScale::KmToScene(6371.0) / 2.0);
    glm::mat4 shape(1.0f);
    shape = glm::scale(shape, glm::vec3(scale));
    shape = glm::rotate(shape, glm::radians(23.4f), glm::vec3(0.0f, 0.0f, 1.0f));
    shape = glm::rotate(shape, glm::radians(131.7f), glm::vec3(0.0f, 1.0f, 0.0f));
    scene.vertex = glm::normalize(glm::vec3(0.31f, 0.77f, -0.56f)) * 2.0f;
    const glm::dmat3 shapeD{glm::mat3(shape)};
    const glm::dvec3 vertexWorld = scene.bodyWorld + shapeD * glm::dvec3(scene.vertex);
    scene.eyeWorld = vertexWorld + glm::normalize(vertexWorld - scene.bodyWorld) * (metres * kMetre);
    scene.model = glm::translate(glm::mat4(1.0f), glm::vec3(scene.bodyWorld - scene.eyeWorld)) * shape;
    return scene;
}

/** Where the vertex really is in the render frame (origin = eye), in double. */
glm::dvec3 Exact(const Scene& scene) {
    return scene.bodyWorld + glm::dmat3(glm::mat3(scene.model)) * glm::dvec3(scene.vertex) - scene.eyeWorld;
}

} // namespace

TEST(MeshAnchor, VertexUnderTheEyeIsPlacedToTheMillimetre) {
    const Scene scene = EarthWithEyeAbove(1.0);
    const MeshAnchor anchor =
        ComputeMeshAnchor(glm::mat3(scene.model), scene.eyeWorld - scene.bodyWorld, glm::dvec3(0.0));
    // planetLighting.vs, NEAR_SURFACE, in float.
    const glm::vec3 anchored = glm::mat3(scene.model) * (scene.vertex - anchor.anchor) + anchor.anchorOffset;
    EXPECT_LT(glm::distance(glm::dvec3(anchored), Exact(scene)), 1.0e-3 * kMetre);
    EXPECT_NEAR(glm::length(glm::dvec3(anchored)), kMetre, 1.0e-3 * kMetre);
}

TEST(MeshAnchor, TheModelMatrixAloneIsOffByAFractionOfAMetre) {
    // The control: model * aPos sums a 6371 km translation and a 6371 km offset in float.
    const Scene scene = EarthWithEyeAbove(1.0);
    const glm::vec3 naive(scene.model * glm::vec4(scene.vertex, 1.0f));
    EXPECT_GT(glm::distance(glm::dvec3(naive), Exact(scene)), 0.05 * kMetre);
}

TEST(MeshAnchor, AnXrEyeOffsetFromTheOriginIsCarriedThroughTheOffset) {
    const Scene scene = EarthWithEyeAbove(2.0);
    const glm::dvec3 eyeRender(0.032 * kMetre, 0.0, 0.0); // half an IPD to the right of the head
    const glm::dvec3 eyeWorld = scene.eyeWorld + eyeRender;
    const MeshAnchor anchor = ComputeMeshAnchor(glm::mat3(scene.model), eyeWorld - scene.bodyWorld, eyeRender);
    const glm::vec3 anchored = glm::mat3(scene.model) * (scene.vertex - anchor.anchor) + anchor.anchorOffset;
    EXPECT_LT(glm::distance(glm::dvec3(anchored), Exact(scene)), 1.0e-3 * kMetre);
}

namespace {

// common/log_depth.glsl, in float as the GPU runs it.
float LogDepthNdc(float w, float c, float zCoef) {
    return std::log2(std::max(1e-6f, c * w + 1.0f)) * zCoef - 1.0f;
}

} // namespace

TEST(LogDepth, CompressedScaleKeepsTheHistoricalCurveBitForBit) {
    const float far = 20000.0f;
    const auto zCoef = static_cast<float>(2.0 / std::log2(1.0 * far + 1.0));
    const auto legacyZCoef = static_cast<float>(2.0 / std::log2(far + 1.0));
    ASSERT_EQ(zCoef, legacyZCoef);
    for (const float w : {0.0005f, 0.3f, 7.0f, 38.0f, 1900.0f, 19999.0f}) {
        const float legacy = std::log2(std::max(1e-6f, w + 1.0f)) * legacyZCoef - 1.0f;
        EXPECT_EQ(LogDepthNdc(w, 1.0f, zCoef), legacy) << w;
    }
}

TEST(LogDepth, RealisticCurveResolvesCentimetresAtAMetreAndKilometresAtAnAu) {
    const double nearPlane = 0.5 * kMetre;
    const auto c = static_cast<float>(1.0 / nearPlane);
    const float far = 1.0e6f;
    const auto zCoef = static_cast<float>(2.0 / std::log2(static_cast<double>(c) * far + 1.0));
    // 24-bit depth: window depth (ndc + 1) / 2 quantised to 2^-24.
    const auto depthStep = [&](double w, double dw) {
        const double a = (LogDepthNdc(static_cast<float>(w), c, zCoef) + 1.0) * 0.5;
        const double b = (LogDepthNdc(static_cast<float>(w + dw), c, zCoef) + 1.0) * 0.5;
        return (b - a) * 16777216.0;
    };
    EXPECT_GT(depthStep(1.0 * kMetre, 0.01 * kMetre), 1.0);     // 1 cm apart at 1 m
    EXPECT_GT(depthStep(1900.0, 1000.0 / SceneScale::kKmPerSceneUnit), 1.0); // 1000 km apart at 1 AU
    EXPECT_LT(LogDepthNdc(far, c, zCoef), 1.0f + 1e-6f);
    EXPECT_GT(LogDepthNdc(static_cast<float>(nearPlane), c, zCoef), -1.0f);
}
