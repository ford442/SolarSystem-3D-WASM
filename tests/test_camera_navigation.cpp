#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Solar_System/OrbitLayout.h"
#include "Solar_System/SceneScale.h"
#include "core/Camera.h"
#include "core/CameraNavigation.h"

namespace {

constexpr double kKmPerUnit = SceneScale::kKmPerSceneUnit;
constexpr double kMetre = 1.0e-3 / kKmPerUnit;

/** A UV sphere like sphere.obj: `sectors` around, `stacks` pole to pole. */
void MakeSphere(double radius, int sectors, int stacks, std::vector<glm::vec3>& positions, std::vector<unsigned>& indices) {
    for (int i = 0; i <= stacks; ++i) {
        const double phi = M_PI * i / stacks;
        for (int j = 0; j <= sectors; ++j) {
            const double theta = 2.0 * M_PI * j / sectors;
            positions.emplace_back(radius * std::sin(phi) * std::cos(theta), radius * std::cos(phi),
                                   radius * std::sin(phi) * std::sin(theta));
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < sectors; ++j) {
            const unsigned a = i * (sectors + 1) + j;
            const unsigned b = a + sectors + 1;
            indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
}

/** Earth on realistic scale: sphere.obj (radius 2) scaled to 6371 km, somewhere 1 AU out. */
CameraNavigation::Body RealisticEarth() {
    CameraNavigation::Body earth;
    earth.center = glm::dvec3(-1650.0, 12.0, 940.0);
    const double scale = SceneScale::KmToScene(6371.0) / 2.0;
    earth.shape = glm::dmat3(glm::rotate(glm::dmat4(1.0), glm::radians(23.4), glm::dvec3(0.0, 0.0, 1.0))) * scale;
    earth.meshRadius = 2.0;
    return earth;
}

} // namespace

TEST(CameraNavigation, AltitudeIsMeasuredAboveTheDrawnFigure) {
    const CameraNavigation::Body earth = RealisticEarth();
    const double radius = SceneScale::KmToScene(6371.0);
    const glm::dvec3 up = glm::normalize(glm::dvec3(0.3, 0.8, -0.52));
    for (const double metres : {1.0, 100.0, 1.0e6}) {
        const glm::dvec3 eye = earth.center + up * (radius + metres * kMetre);
        EXPECT_NEAR(CameraNavigation::AnalyticAltitude(earth, eye), metres * kMetre, 1.0e-6 * metres * kMetre);
    }
    EXPECT_LT(CameraNavigation::AnalyticAltitude(earth, earth.center + up * (0.5 * radius)), 0.0);
}

TEST(CameraNavigation, OblateAltitudeFollowsThePolarFlattening) {
    // Saturn-like: the pole is 10% closer than the equator; shape squashes the mesh's Y.
    CameraNavigation::Body body;
    body.shape = glm::dmat3(glm::scale(glm::dmat4(1.0), glm::dvec3(10.0, 9.0, 10.0)));
    EXPECT_NEAR(CameraNavigation::AnalyticAltitude(body, glm::dvec3(0.0, 19.0, 0.0)), 1.0, 1e-12);
    EXPECT_NEAR(CameraNavigation::AnalyticAltitude(body, glm::dvec3(21.0, 0.0, 0.0)), 1.0, 1e-12);
}

TEST(CameraNavigation, FloorLiftsTheEyeOutOfTheSurfaceAndLeavesItAloneAbove) {
    const CameraNavigation::Body earth = RealisticEarth();
    const double radius = SceneScale::KmToScene(6371.0);
    const glm::dvec3 up = glm::normalize(glm::dvec3(-0.1, 0.2, 0.97));
    const double margin = 0.5 * kMetre;

    const glm::dvec3 inside = earth.center + up * (radius - 300.0 * kMetre);
    const glm::dvec3 lifted = CameraNavigation::Floor(earth, inside, earth.meshRadius, margin);
    EXPECT_NEAR(CameraNavigation::AnalyticAltitude(earth, lifted), margin, 1.0e-3 * margin);
    EXPECT_LT(glm::length(glm::normalize(lifted - earth.center) - up), 1e-12); // straight up, not sideways

    const glm::dvec3 above = earth.center + up * (radius + 2.0 * kMetre);
    EXPECT_EQ(CameraNavigation::Floor(earth, above, earth.meshRadius, margin), above);
}

TEST(CameraNavigation, RaycastFindsTheFacetsBelowTheAnalyticSphere) {
    std::vector<glm::vec3> positions;
    std::vector<unsigned> indices;
    MakeSphere(2.0, 64, 32, positions, indices);

    // Down through the middle of a facet: the facet sits inside the sphere there.
    const glm::dvec3 a(positions[indices[600]]), b(positions[indices[601]]), c(positions[indices[602]]);
    const glm::dvec3 down = -glm::normalize((a + b + c) / 3.0);
    const glm::dvec3 origin = -down * 3.0;
    const auto hit = CameraNavigation::RaycastTriangles(origin, down, positions, indices);
    ASSERT_TRUE(hit.has_value());
    const double surface = 3.0 - *hit;
    EXPECT_LT(surface, 2.0);
    EXPECT_GT(surface, 2.0 * std::cos(M_PI / 32.0) - 1e-6); // within the sag of one facet

    EXPECT_FALSE(CameraNavigation::RaycastTriangles(origin, -down, positions, indices).has_value());
}

TEST(CameraNavigation, FloorFindsTheFacetFromAboveAndFromBeneathIt) {
    std::vector<glm::vec3> positions;
    std::vector<unsigned> indices;
    MakeSphere(2.0, 64, 32, positions, indices);
    const CameraNavigation::Body earth = RealisticEarth();
    const glm::dvec3 a(positions[indices[600]]), b(positions[indices[601]]), c(positions[indices[602]]);
    const glm::dvec3 up = glm::normalize((a + b + c) / 3.0);
    const auto facet = CameraNavigation::FacetRadius(up, earth.meshRadius, positions, indices);
    ASSERT_TRUE(facet.has_value());
    const double margin = 0.5 * kMetre;
    const glm::dvec3 onFacet = earth.center + earth.shape * (up * *facet);

    // 300 m below the facet (a pose or a fast step put it there) and 2 km above it: either
    // way the same facet is found, and the floor stands the eye half a metre on it.
    for (const double metres : {-300.0, 2000.0}) {
        const glm::dvec3 eye = onFacet + glm::normalize(onFacet - earth.center) * (metres * kMetre);
        const glm::dvec3 model = CameraNavigation::ToModel(earth, eye);
        const auto found = CameraNavigation::FacetRadius(glm::normalize(model), earth.meshRadius, positions, indices);
        ASSERT_TRUE(found.has_value()) << metres;
        EXPECT_NEAR(*found, *facet, 1e-12) << metres;
        const glm::dvec3 floored = CameraNavigation::Floor(earth, eye, *found, margin);
        EXPECT_NEAR(CameraNavigation::Altitude(earth, floored, *found), std::max(margin, metres * kMetre),
                    1.0e-3 * kMetre)
            << metres;
    }
}

TEST(CameraNavigation, CompressedClipPlanesAreTheHistoricalOnes) {
    for (const double altitude : {1.0e-6, 0.5, 300.0}) {
        const CameraNavigation::ClipPlanes clip = CameraNavigation::ChooseClipPlanes(false, altitude, kKmPerUnit);
        EXPECT_EQ(clip.nearPlane, 0.001f);
        EXPECT_EQ(clip.farPlane, 20000.0f);
        EXPECT_EQ(clip.logDepthC, 1.0f);
    }
}

TEST(CameraNavigation, RealisticNearPlaneFollowsAltitudeDownToAQuarterMetre) {
    const auto atOneMetre = CameraNavigation::ChooseClipPlanes(true, 1.0 * kMetre, kKmPerUnit);
    EXPECT_NEAR(atOneMetre.nearPlane, 0.5 * kMetre, 1e-3 * kMetre);
    EXPECT_NEAR(atOneMetre.logDepthC, 1.0 / atOneMetre.nearPlane, 1e-3 / atOneMetre.nearPlane);
    EXPECT_GE(atOneMetre.farPlane, 1.0e6f);

    const auto onTheGround = CameraNavigation::ChooseClipPlanes(true, 0.0, kKmPerUnit);
    EXPECT_NEAR(onTheGround.nearPlane, 0.25 * kMetre, 1e-3 * kMetre);
    const auto farOut = CameraNavigation::ChooseClipPlanes(true, 1000.0, kKmPerUnit);
    EXPECT_EQ(farOut.nearPlane, 0.001f);
}

TEST(CameraNavigation, RealisticSpeedIsAltitudePerSecondFromMillimetresToAnAu) {
    EXPECT_EQ(CameraNavigation::SpeedScale(false, 1.0e-9, kKmPerUnit, 1900.0), 1.0);
    EXPECT_EQ(CameraNavigation::SpeedScale(false, 5000.0, kKmPerUnit, 1900.0), 1.0);

    // At the default movement speed (5) the camera covers its altitude in a second.
    EXPECT_NEAR(5.0 * CameraNavigation::SpeedScale(true, 10.0 * kMetre, kKmPerUnit, 1900.0), 10.0 * kMetre, 1e-15);
    EXPECT_NEAR(5.0 * CameraNavigation::SpeedScale(true, 0.0, kKmPerUnit, 1900.0), 1.0e-3 * kMetre, 1e-18);
    EXPECT_EQ(5.0 * CameraNavigation::SpeedScale(true, 1.0e6, kKmPerUnit, 1900.0), 1900.0);
}

TEST(CameraNavigation, AnchorEngagesReleasesWithHysteresisAndHandsOver) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    // Engage only inside the engage distance.
    EXPECT_EQ(CameraNavigation::ChooseAnchor(-1, {100.0, 70.0}, false), -1);
    EXPECT_EQ(CameraNavigation::ChooseAnchor(-1, {100.0, 60.0}, false), 1);
    // Between engage and release the current anchor holds…
    EXPECT_EQ(CameraNavigation::ChooseAnchor(1, {100.0, 75.0}, false), 1);
    // …and past release it lets go.
    EXPECT_EQ(CameraNavigation::ChooseAnchor(1, {100.0, 85.0}, false), -1);
    // Another body takes over only when it is much closer in its own radii.
    EXPECT_EQ(CameraNavigation::ChooseAnchor(0, {20.0, 12.0}, false), 0);
    EXPECT_EQ(CameraNavigation::ChooseAnchor(0, {20.0, 9.0}, false), 1);
    // A focus flight keeps its target from far away.
    EXPECT_EQ(CameraNavigation::ChooseAnchor(1, {3.0, 5000.0}, true), 1);
    // Non-candidates never win.
    EXPECT_EQ(CameraNavigation::ChooseAnchor(-1, {nan, -1.0, 10.0}, false), 2);
}

TEST(CameraNavigation, CameraCarriesItsTransitionWithIt) {
    Camera camera(0.001f, 20000.0f, 5.0f, 0.05f, glm::dvec3(10.0, 0.0, 0.0));
    camera.StartTransitionTo(glm::dvec3(20.0, 0.0, 0.0), -90.0f, 0.0f, 2.0f);
    const glm::dvec3 drift(1.0e5, -3.0, 2.5e4); // a planet's orbital motion over a frame
    camera.Carry(drift);
    EXPECT_EQ(camera.GetWorldPosition(), glm::dvec3(10.0, 0.0, 0.0) + drift);
    camera.UpdateTransition(2.0f);
    EXPECT_EQ(camera.GetWorldPosition(), glm::dvec3(20.0, 0.0, 0.0) + drift);
}

TEST(CameraNavigation, CameraMovesInDoubleAtTheSpeedScale) {
    // 90 AU out on realistic scale a metre step is far under a float's spacing (1.6 km
    // there); a double's is about 2 mm.
    const glm::dvec3 start(171000.0, 0.0, 0.0);
    Camera camera(0.001f, 20000.0f, 5.0f, 0.05f, start, glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f);
    camera.SetSpeedScale(kMetre / 5.0);
    camera.ProcessKeyboard(CameraVector::FORWARD, 1.0f);
    EXPECT_NEAR(glm::distance(camera.GetWorldPosition(), start), kMetre, 5e-3 * kMetre);

    // At the origin the view matrix is a pure rotation.
    const glm::mat4 view = camera.GetViewMatrix();
    EXPECT_EQ(glm::vec3(view[3]), glm::vec3(0.0f));
}

TEST(SceneScale, CompressedKeepsArtSizesAndRealisticIsTrueScale) {
    OrbitLayout::ResetForTests();
    const BodyCatalog::Entry* earth = BodyCatalog::FindById("earth");
    const BodyCatalog::Entry* moon = BodyCatalog::FindById("moon");
    ASSERT_NE(earth, nullptr);
    ASSERT_NE(moon, nullptr);
    EXPECT_EQ(SceneScale::BodySizeFactor(*earth), 1.0f);
    EXPECT_EQ(SceneScale::BodyRadius(*earth), 2.0f * earth->earthRadiusScale);
    EXPECT_EQ(SceneScale::SunRadius(), 1.0f);
    EXPECT_EQ(SceneScale::SatelliteCircularRadius(*moon), moon->sceneOrbitRadius);

    OrbitLayout::SetScaleMode(OrbitLayout::ScaleMode::Realistic);
    EXPECT_NEAR(SceneScale::BodyRadius(*earth) * kKmPerUnit, 6371.0, 0.01);
    EXPECT_NEAR(SceneScale::BodyRadius(*moon) * kKmPerUnit, moon->radiusKm, 0.01);
    EXPECT_NEAR(SceneScale::SunRadius() * kKmPerUnit, SceneScale::kSunRadiusKm, 1.0);
    EXPECT_NEAR(SceneScale::SatelliteCircularRadius(*moon) * kKmPerUnit, moon->keplerian.aKm, 1.0);
    EXPECT_NEAR(SceneScale::SatelliteSceneUnitsPerAu(*moon), OrbitLayout::kAuToSceneUnits, 1e-9);
    OrbitLayout::ResetForTests();
}
