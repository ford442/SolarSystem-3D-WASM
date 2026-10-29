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

/**
 * Earth at `julianDate` as CatalogBody draws it: the model matrix is float, built as
 * Rz(tilt) · Scale · Ry(spin), and the camera sees only its 3×3.
 */
CameraNavigation::Body DrawnEarthAt(double julianDate, const glm::dvec3& center) {
    CameraNavigation::Body earth;
    earth.center = center;
    const double scale = SceneScale::KmToScene(6371.0) / 2.0;
    glm::mat4 model(1.0f);
    model = glm::rotate(model, glm::radians(-23.4f), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, glm::vec3(static_cast<float>(scale)));
    model = glm::rotate(model,
                        glm::radians(static_cast<float>(OrbitLayout::AxialSpinDegreesAt(OrbitLayout::Body::Earth, julianDate))),
                        glm::vec3(0.0f, 1.0f, 0.0f));
    earth.shape = glm::dmat3(glm::mat3(model));
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

TEST(CameraNavigation, FacetAlongGivesTheHitFacetsOutwardNormal) {
    std::vector<glm::vec3> positions;
    std::vector<unsigned> indices;
    MakeSphere(2.0, 64, 32, positions, indices);
    const glm::dvec3 a(positions[indices[600]]), b(positions[indices[601]]), c(positions[indices[602]]);
    // Off the facet's centre, so the radial direction and the facet normal differ.
    const glm::dvec3 toward = glm::normalize(0.6 * a + 0.3 * b + 0.1 * c);
    const auto hit = CameraNavigation::FacetAlong(toward, 2.0, positions, indices);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->radius, *CameraNavigation::FacetRadius(toward, 2.0, positions, indices), 1e-15);
    EXPECT_NEAR(glm::length(hit->normal), 1.0, 1e-12);
    EXPECT_NEAR(glm::dot(hit->normal, b - a), 0.0, 1e-6);
    EXPECT_NEAR(glm::dot(hit->normal, c - a), 0.0, 1e-6);
    EXPECT_GT(glm::dot(hit->normal, toward), 0.99);
    EXPECT_LT(glm::dot(hit->normal, toward), 1.0 - 1e-6);

    // A squashed body tilts world normals toward its poles, as the inverse transpose does.
    CameraNavigation::Body oblate;
    oblate.shape = glm::dmat3(1.0, 0.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.0, 1.0);
    const glm::dvec3 world = CameraNavigation::NormalToWorld(oblate, glm::normalize(glm::dvec3(1.0, 1.0, 0.0)));
    EXPECT_NEAR(world.x, 1.0 / std::sqrt(5.0), 1e-12);
    EXPECT_NEAR(world.y, 2.0 / std::sqrt(5.0), 1e-12);
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

TEST(CameraNavigation, RollTurnsUpAboutTheViewDirectionAndLookKeepsIt) {
    // Yaw 0 looks down +X; with world up +Y, roll 0 has up +Y and right +Z.
    Camera camera(0.001f, 20000.0f, 5.0f, 0.05f, glm::dvec3(0.0), glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f);
    EXPECT_NEAR(glm::distance(camera.GetUpVector(), glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f, 1e-6f);
    // Right-handed about +X: +Y turns towards +Z (the convention web/src/timeline.ts inverts).
    camera.SetRoll(90.0f);
    EXPECT_NEAR(glm::distance(camera.GetFrontVector(), glm::vec3(1.0f, 0.0f, 0.0f)), 0.0f, 1e-6f);
    EXPECT_NEAR(glm::distance(camera.GetUpVector(), glm::vec3(0.0f, 0.0f, 1.0f)), 0.0f, 1e-6f);
    EXPECT_NEAR(glm::distance(camera.GetRightVector(), glm::vec3(0.0f, -1.0f, 0.0f)), 0.0f, 1e-6f);
    // The view matrix maps up to screen +Y.
    const glm::vec3 screenUp = glm::mat3(camera.GetViewMatrix()) * glm::vec3(0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(screenUp.y, 1.0f, 1e-6f);
    camera.SetYawPitch(30.0f, 10.0f);
    EXPECT_FLOAT_EQ(camera.GetRoll(), 90.0f);
    EXPECT_NEAR(glm::dot(camera.GetUpVector(), camera.GetFrontVector()), 0.0f, 1e-6f);
}

TEST(CameraNavigation, HoveringCoRotatesAndOrbitOnlyFollows) {
    const double jd = 2460408.5; // 2024-04-08
    const glm::dvec3 centre(-1650.0, 12.0, 940.0);
    const glm::dvec3 drift = glm::dvec3(3.2e-3, 0.0, -1.1e-3); // a day of Earth's orbit, scene units
    const CameraNavigation::Body before = DrawnEarthAt(jd, centre);
    const double radius = before.meshRadius * glm::length(before.shape[0]);

    // 1 km up, over some point of the ground.
    const glm::dvec3 up = glm::normalize(glm::dvec3(0.3, 0.8, -0.5));
    const glm::dvec3 hover = centre + up * (radius + SceneScale::KmToScene(1.0));
    const double altitudeRadii = CameraNavigation::AnalyticAltitude(before, hover) / radius;
    EXPECT_LT(altitudeRadii, CameraNavigation::kHoverRadii);
    EXPECT_TRUE(CameraNavigation::CoRotates(false, altitudeRadii));

    // A sidereal day, a quarter of one, and a date jump of years: the camera's offset in the
    // drawn body frame is the same, so is its altitude, and it moved in the world.
    for (const double days : {0.99726968, 0.25, 4380.123}) {
        const CameraNavigation::Body after = DrawnEarthAt(jd + days, centre + drift * days);
        glm::dmat3 turn(1.0);
        const glm::dvec3 carried = CameraNavigation::CarryEye(before, after, hover, true, &turn);
        const glm::dvec3 was = CameraNavigation::ToModel(before, hover);
        const glm::dvec3 now = CameraNavigation::ToModel(after, carried);
        EXPECT_LT(glm::distance(was, now), 1e-10 * glm::length(was)) << days; // ~0.3 mm at Earth's radius
        EXPECT_NEAR(CameraNavigation::AnalyticAltitude(after, carried), SceneScale::KmToScene(1.0),
                    SceneScale::KmToScene(0.001))
            << days;
        // The turn is a proper rotation: the camera's orientation keeps its handedness.
        EXPECT_NEAR(glm::determinant(turn), 1.0, 1e-12);
        EXPECT_NEAR(glm::length(turn * glm::dvec3(0.0, 0.0, 1.0)), 1.0, 1e-12);
        if (days == 0.25) {
            EXPECT_GT(glm::distance(carried - after.center, hover - before.center), 0.5 * radius) << "a quarter day";
        }
    }

    // At 2 radii it does not: the anchor carries the body's translation and the camera's
    // inertial offset from the body is unchanged (the surface turns underneath).
    const glm::dvec3 orbit = centre + up * 2.0 * radius;
    EXPECT_FALSE(CameraNavigation::CoRotates(false, CameraNavigation::AnalyticAltitude(before, orbit) / radius));
    const CameraNavigation::Body after = DrawnEarthAt(jd + 0.99726968 / 3.0, centre + drift);
    glm::dmat3 turn(1.0);
    const glm::dvec3 carried = CameraNavigation::CarryEye(before, after, orbit, false, &turn);
    EXPECT_EQ(carried - after.center, orbit - before.center);
    EXPECT_EQ(turn, glm::dmat3(1.0));
}

TEST(CameraNavigation, CoRotationEngagesAndReleasesWithHysteresis) {
    using CameraNavigation::CoRotates;
    EXPECT_FALSE(CoRotates(false, 0.055)); // inside the release distance but not yet engaged
    EXPECT_TRUE(CoRotates(false, 0.049));
    EXPECT_TRUE(CoRotates(true, 0.055)); // engaged: holds through the gap
    EXPECT_FALSE(CoRotates(true, 0.061));
}

TEST(CameraNavigation, RotationBetweenDividesOutScaleAndKeepsTheTurn) {
    const glm::dmat3 tilt(glm::rotate(glm::dmat4(1.0), glm::radians(-23.4), glm::dvec3(0.0, 0.0, 1.0)));
    const auto spin = [&](double degrees, double scale) {
        return tilt * glm::dmat3(glm::rotate(glm::dmat4(1.0), glm::radians(degrees), glm::dvec3(0.0, 1.0, 0.0))) * scale;
    };
    // A scale toggle between the two poses must not turn into a spurious rotation.
    const glm::dmat3 turn = CameraNavigation::RotationBetween(spin(10.0, 1.0), spin(70.0, 5.0e3));
    const glm::dmat3 expected = tilt * glm::dmat3(glm::rotate(glm::dmat4(1.0), glm::radians(60.0), glm::dvec3(0.0, 1.0, 0.0))) *
                                glm::transpose(tilt);
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(glm::distance(turn[c], expected[c]), 0.0, 1e-12) << c;
    }
}

TEST(CameraNavigation, ReorientTurnsFrontAndUpTogetherAndKeepsRoll) {
    Camera camera(0.001f, 20000.0f, 5.0f, 0.05f, glm::dvec3(0.0), glm::vec3(0.0f, 1.0f, 0.0f), 30.0f, 10.0f);
    camera.SetRoll(25.0f);
    const glm::vec3 front = camera.GetFrontVector();
    const glm::vec3 up = camera.GetUpVector();
    const glm::dmat3 turn(glm::rotate(glm::dmat4(1.0), glm::radians(37.0), glm::normalize(glm::dvec3(0.2, 1.0, -0.3))));
    camera.Reorient(turn);
    EXPECT_NEAR(glm::distance(camera.GetFrontVector(), glm::vec3(turn * glm::dvec3(front))), 0.0f, 2e-6f);
    EXPECT_NEAR(glm::distance(camera.GetUpVector(), glm::vec3(turn * glm::dvec3(up))), 0.0f, 2e-6f);
    EXPECT_NEAR(glm::dot(camera.GetUpVector(), camera.GetFrontVector()), 0.0f, 1e-6f);
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
