#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Auxiliary_Modules/Ephemeris.h"
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

TEST_F(RenderOriginTest, CameraAnywhereInsideTheRebaseRadiusStaysPrecise) {
    const glm::dvec3 body = OrbitLayout::GetHeliocentricScenePosition(kFarBody);
    const glm::dvec3 camera = body + glm::dvec3(0.0, 0.0, kEarthRadiusScene);
    const glm::dvec3 directions[] = {
        {1.0, 0.0, 0.0}, {0.0, -1.0, 0.0}, {0.577, 0.577, 0.577}, {-0.3, 0.8, -0.52},
    };
    for (const glm::dvec3& direction : directions) {
        // The camera flew here from an origin just inside the rebase radius, so it has not
        // rebased and its float position is as coarse as it is ever allowed to get.
        const double reach = static_cast<double>(OrbitLayout::kRenderOriginRebaseDistance) - 1.0;
        const glm::dvec3 origin = camera + glm::normalize(direction) * reach;
        OrbitLayout::SetRenderOrigin(origin);
        const glm::vec3 cameraRender = OrbitLayout::ToRenderFrame(camera);
        ASSERT_EQ(OrbitLayout::ChooseRenderOrigin(cameraRender), origin);

        // Compare against where the camera really is: its float position defines that.
        const glm::dvec3 expected = body - OrbitLayout::ToHeliocentricScene(cameraRender);
        const glm::dvec3 relative(OrbitLayout::GetRenderPosition(kFarBody) - cameraRender);
        EXPECT_LT(glm::length(relative - expected), 1.0e-3 * kEarthRadiusScene);
    }
}

TEST_F(RenderOriginTest, RebasingMidFlightDoesNotMoveTheBodyOnScreen) {
    const glm::dvec3 body = OrbitLayout::GetHeliocentricScenePosition(kFarBody);
    const glm::dvec3 outward = glm::normalize(glm::dvec3(0.48, -0.2, 0.85));
    const glm::dvec3 sideways = glm::normalize(glm::cross(outward, glm::dvec3(0.0, 1.0, 0.0)));
    const double rebase = static_cast<double>(OrbitLayout::kRenderOriginRebaseDistance);

    // Put the body just inside the rebase radius so the camera, skimming past it at about
    // one Earth radius, crosses the boundary while it is that close.
    OrbitLayout::SetRenderOrigin(body - outward * (rebase - 0.3 * kEarthRadiusScene));
    glm::vec3 camera = OrbitLayout::ToRenderFrame(body + sideways * kEarthRadiusScene - outward * (0.5 * kEarthRadiusScene));
    const glm::vec3 front = glm::normalize(glm::vec3(-sideways + 0.15 * outward));
    const glm::vec3 step = glm::vec3(outward) * static_cast<float>(kEarthRadiusScene / 100.0);

    int rebases = 0;
    for (int frame = 0; frame < 100; ++frame) {
        camera += step; // what Camera::ProcessKeyboard does

        const glm::dvec3 previous = OrbitLayout::GetRenderOrigin();
        const glm::dvec3 next = OrbitLayout::ChooseRenderOrigin(camera);
        if (next == previous) {
            continue;
        }
        ++rebases;
        const glm::dvec3 cameraHeliocentric = OrbitLayout::ToHeliocentricScene(camera);
        EXPECT_LT(glm::distance(cameraHeliocentric, body), 1.2 * kEarthRadiusScene);
        const glm::vec2 before = ProjectToNdc(camera, front, OrbitLayout::GetRenderPosition(kFarBody));

        // Application::MoveRenderOrigin, reduced to what the camera and a body see.
        OrbitLayout::SetRenderOrigin(next);
        camera -= glm::vec3(next - previous);
        const glm::vec2 after = ProjectToNdc(camera, front, OrbitLayout::GetRenderPosition(kFarBody));

        EXPECT_LT(glm::length(camera), 1.0e-6f);
        EXPECT_LT(glm::distance(OrbitLayout::ToHeliocentricScene(camera), cameraHeliocentric), 1.0e-9);
        EXPECT_LT(std::fabs(before.x), 1.0f); // on screen, so the comparison means something
        EXPECT_LT(std::fabs(before.y), 1.0f);
        EXPECT_LT(glm::length(after - before), kOnePixelNdc);
    }
    EXPECT_EQ(rebases, 1);
}

TEST_F(RenderOriginTest, RealisticOriginOnlyMovesPastTheRebaseRadius) {
    const glm::dvec3 origin(-60000.0, 120.0, 31000.0);
    OrbitLayout::SetRenderOrigin(origin);
    const float inside = OrbitLayout::kRenderOriginRebaseDistance * 0.99f;
    EXPECT_EQ(OrbitLayout::ChooseRenderOrigin(glm::vec3(inside, 0.0f, 0.0f)), origin);
    EXPECT_EQ(OrbitLayout::ChooseRenderOrigin(glm::vec3(0.0f, -inside, 0.0f)), origin);

    const glm::vec3 outside(OrbitLayout::kRenderOriginRebaseDistance * 1.01f, 0.0f, 0.0f);
    EXPECT_EQ(OrbitLayout::ChooseRenderOrigin(outside), origin + glm::dvec3(outside));
}

TEST_F(RenderOriginTest, CompressedScaleKeepsTheSunAsTheRenderOrigin) {
    // Leaving realistic scale collapses whatever origin it had back onto the Sun.
    OrbitLayout::SetRenderOrigin(glm::dvec3(50000.0, 0.0, -2000.0));
    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Compressed);
    EXPECT_EQ(OrbitLayout::ChooseRenderOrigin(glm::vec3(4000.0f, 0.0f, 0.0f)), glm::dvec3(0.0));
    EXPECT_EQ(OrbitLayout::ChooseRenderOrigin(glm::vec3(0.0f)), glm::dvec3(0.0));
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
}
