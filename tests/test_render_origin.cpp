#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "astro/Ephemeris.h"
#include "Solar_System/OrbitLayout.h"

namespace {

// Earth's radius on realistic scale: 6371 km at 1900 scene units per AU, about 0.081.
constexpr double kEarthRadiusKm = 6371.0;
constexpr double kAuKm = 149597870.7;
constexpr double kEarthRadiusScene = kEarthRadiusKm / kAuKm * OrbitLayout::kAuToSceneUnits;

// One pixel of a 1080-line viewport, in NDC.
constexpr float kOnePixelNdc = 2.0f / 1080.0f;

constexpr OrbitLayout::Body kFarBody = OrbitLayout::Body::Eris;

/** Where a point lands on screen, with the same float matrices the renderer builds. */
glm::vec2 ProjectToNdc(const glm::vec3& cameraRender, const glm::vec3& front, const glm::vec3& pointRender) {
    const glm::mat4 view = glm::lookAt(cameraRender, cameraRender + front, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.001f, 20000.0f);
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), pointRender);
    const glm::vec4 clip = projection * view * model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    return glm::vec2(clip) / clip.w;
}

/** GetOffset as it was computed before the render origin: float angles, float radius. */
glm::vec3 LegacyFloatOffset(OrbitLayout::Body body) {
    if (body == OrbitLayout::Body::Sun) {
        return glm::vec3(0.0f);
    }
    const auto pos = Ephemeris::Position(static_cast<int>(body), OrbitLayout::GetJulianDate());
    const float radius = OrbitLayout::GetOrbitRadius(body);
    const float lon = static_cast<float>(pos.lonRad);
    const float lat = static_cast<float>(pos.latRad);
    const float cosLat = std::cos(lat);
    return glm::vec3(radius * std::cos(lon) * cosLat, radius * std::sin(lat), radius * std::sin(lon) * cosLat);
}

} // namespace

class RenderOriginTest : public ::testing::Test {
protected:
    void SetUp() override {
        OrbitLayout::ResetForTests();
        OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
        ASSERT_GE(OrbitLayout::GetHeliocentricRadiusAu(kFarBody), 50.0f);
    }
    void TearDown() override {
        OrbitLayout::ResetForTests();
    }
};

TEST_F(RenderOriginTest, CameraOneEarthRadiusFromAFarBodySeesItAtTheDoubleOffset) {
    const glm::dvec3 body = OrbitLayout::GetHeliocentricScenePosition(kFarBody);
    const glm::dvec3 camera = body + glm::normalize(glm::dvec3(0.6, 0.3, -0.74)) * kEarthRadiusScene;
    const glm::dvec3 expected = body - camera;
    const double tolerance = 1.0e-4 * kEarthRadiusScene;

    // A deep-linked pose: the origin moves onto it, so the camera stores an exact zero.
    OrbitLayout::SetRenderOrigin(camera);
    const glm::vec3 cameraRender = OrbitLayout::ToRenderFrame(camera);
    EXPECT_EQ(cameraRender, glm::vec3(0.0f));
    const glm::dvec3 relative(OrbitLayout::GetRenderPosition(kFarBody) - cameraRender);
    EXPECT_LT(glm::length(relative - expected), tolerance);

    // The bar is tighter than the float grid out there, and plain float positions miss it.
    const float r = static_cast<float>(glm::length(body));
    const double halfUlp = 0.5 * static_cast<double>(std::nextafter(r, std::numeric_limits<float>::infinity()) - r);
    EXPECT_LT(tolerance, halfUlp);
    const glm::dvec3 floatRelative(glm::vec3(body) - glm::vec3(camera));
    EXPECT_GT(glm::length(floatRelative - expected), tolerance);
}

TEST_F(RenderOriginTest, OriginOnTheCameraEveryFrameKeepsAFlybyOnItsExactTrack) {
    // Application::MoveRenderOrigin puts the origin on the camera once per frame, so the
    // camera is always at an exact zero and the view matrix has no translation. Fly past a
    // body 90 AU out at a tenth of an Earth radius per frame and compare where the float
    // pipeline puts it on screen with a double-precision projection of the true geometry.
    const glm::dvec3 body = OrbitLayout::GetHeliocentricScenePosition(kFarBody);
    const glm::dvec3 outward = glm::normalize(glm::dvec3(0.48, -0.2, 0.85));
    const glm::dvec3 sideways = glm::normalize(glm::cross(outward, glm::dvec3(0.0, 1.0, 0.0)));
    const glm::vec3 front = glm::normalize(glm::vec3(-sideways + 0.15 * outward));
    glm::dvec3 camera = body + sideways * kEarthRadiusScene - outward * (2.0 * kEarthRadiusScene);

    for (int frame = 0; frame < 40; ++frame) {
        camera += outward * (0.1 * kEarthRadiusScene);
        OrbitLayout::SetRenderOrigin(camera);
        const glm::vec3 cameraRender = OrbitLayout::ToRenderFrame(camera);
        ASSERT_EQ(cameraRender, glm::vec3(0.0f));

        const glm::vec2 drawn = ProjectToNdc(cameraRender, front, OrbitLayout::GetRenderPosition(kFarBody));
        const glm::dmat4 view = glm::lookAt(glm::dvec3(0.0), glm::dvec3(front), glm::dvec3(0.0, 1.0, 0.0));
        const glm::dmat4 projection = glm::perspective(glm::radians(45.0), 16.0 / 9.0, 0.001, 20000.0);
        const glm::dvec4 clip = projection * view * glm::dvec4(body - camera, 1.0);
        const glm::dvec2 exact = glm::dvec2(clip) / clip.w;
        if (std::fabs(exact.x) < 1.0 && std::fabs(exact.y) < 1.0) {
            EXPECT_LT(glm::length(glm::dvec2(drawn) - exact), 0.01 * kOnePixelNdc) << "frame " << frame;
        }
    }
}

TEST_F(RenderOriginTest, WithoutTheFloatingOriginTheSameFlybyIsOffByPixels) {
    // The control for the test above: the camera's own float position 90 AU out (what an
    // origin pinned at the Sun gives) puts the body tens of pixels off at this range.
    const glm::dvec3 body = OrbitLayout::GetHeliocentricScenePosition(kFarBody);
    const glm::dvec3 camera = body + glm::dvec3(0.25, 0.1, 0.3) * kEarthRadiusScene;
    // Looking a little to one side of the body, so its position error shows on screen.
    const glm::vec3 front = glm::normalize(glm::vec3(body - camera) + glm::vec3(0.002f, 0.0f, 0.0f));
    const glm::vec2 pinned = ProjectToNdc(glm::vec3(camera), front, glm::vec3(body));
    const glm::vec2 floating = ProjectToNdc(glm::vec3(0.0f), front, glm::vec3(body - camera));
    EXPECT_GT(glm::length(pinned - floating), 10.0f * kOnePixelNdc);
}

TEST_F(RenderOriginTest, CompressedScaleMatchesTheFloatPositionsItReplaced) {
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Compressed);
    OrbitLayout::SetRenderOrigin(glm::dvec3(0.0));

    // With the origin at the Sun the render frame is the heliocentric frame, and positions
    // match the float computation they replaced.
    for (const double jd : {Ephemeris::kJ2000, Ephemeris::JulianDateFromYmd(2031, 7, 4)}) {
        OrbitLayout::SetJulianDate(jd);
        for (int i = 0; i < OrbitLayout::kBodyCount; ++i) {
            const auto body = static_cast<OrbitLayout::Body>(i);
            const glm::vec3 render = OrbitLayout::GetRenderPosition(body);
            EXPECT_EQ(render, OrbitLayout::GetOffset(body)) << "body " << i;
            const glm::vec3 legacy = LegacyFloatOffset(body);
            const float tolerance = 1.0e-5f * glm::length(legacy) + 1.0e-5f;
            EXPECT_LT(glm::distance(render, legacy), tolerance) << "body " << i;
        }
    }

    // A camera-centred origin is the same frame shifted: compressed scale moves its origin
    // with the camera too, and offsets between bodies do not change past float rounding.
    const glm::dvec3 camera = OrbitLayout::GetHeliocentricScenePosition(OrbitLayout::Body::Earth) + glm::dvec3(30.0, 4.0, 22.0);
    OrbitLayout::SetRenderOrigin(camera);
    for (int i = 0; i < OrbitLayout::kBodyCount; ++i) {
        const auto body = static_cast<OrbitLayout::Body>(i);
        const glm::dvec3 exact = OrbitLayout::GetHeliocentricScenePosition(body) - camera;
        EXPECT_LT(glm::distance(glm::dvec3(OrbitLayout::GetRenderPosition(body)), exact), 1.0e-7 * glm::length(exact) + 1e-9)
            << "body " << i;
    }
}
