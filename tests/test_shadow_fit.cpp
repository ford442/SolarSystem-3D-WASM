// The light box's sideways fit (gfx/ShadowFit.h): the ±3-radii box from orbit, and near the
// ground the part of the planet the camera sees. Lengths are kilometres, Earth's radius 6371.

#include "gfx/ShadowFit.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

namespace {

constexpr double kRadius = 6371.0;
const glm::dvec3 kCentre(0.0);
const glm::dvec3 kSun(1.496e8, 0.0, 0.0);

ShadowFit::LightBasis Light() {
    return ShadowFit::LightLookAt(kSun, kCentre);
}

/** A camera `altitudeKm` above the planet's `up` pole direction, looking `pitchDeg` from straight down. */
ShadowFit::View HoverView(double altitudeKm, double pitchFromNadirDeg, double fovDeg = 45.0, double aspect = 16.0 / 9.0) {
    const glm::dvec3 up = glm::normalize(glm::dvec3(0.3, 0.9, -0.2));
    const glm::dvec3 east = glm::normalize(glm::cross(up, glm::dvec3(0.0, 0.0, 1.0)));
    ShadowFit::View view;
    view.eye = kCentre + up * (kRadius + altitudeKm);
    const double pitch = glm::radians(pitchFromNadirDeg);
    view.front = glm::normalize(-up * std::cos(pitch) + glm::cross(east, up) * std::sin(pitch));
    view.right = glm::normalize(glm::cross(view.front, up));
    view.up = glm::cross(view.right, view.front);
    view.tanHalfFovY = std::tan(glm::radians(fovDeg) * 0.5);
    view.aspect = aspect;
    return view;
}

bool Inside(const ShadowFit::Box& box, const glm::dvec2& p) {
    return std::abs(p.x - box.centerX) <= box.halfExtent && std::abs(p.y - box.centerY) <= box.halfExtent;
}

TEST(ShadowFit, VisibleCapIsFarTighterThanThreeRadiiAtTheGround) {
    // 1 km up, looking steeply down: the camera sees a patch a few km across, where the
    // ±3-radii box (19 000 km a side on this map) would spend a texel on kilometres.
    const ShadowFit::Box cap = ShadowFit::FitVisibleCap(Light(), HoverView(1.0, 20.0), kCentre, kRadius);
    ASSERT_GT(cap.halfExtent, 0.0);
    EXPECT_LT(cap.halfExtent, 50.0);
    EXPECT_LT(cap.halfExtent, 3.0 * kRadius * 0.01);
    // …and at 4096 texels across, a texel on the order of the surface patch or smaller.
    const ShadowFit::Box fitted = ShadowFit::Quantize(cap, ShadowFit::kMinCapRadii * kRadius);
    EXPECT_LT(2.0 * fitted.halfExtent / 4096.0, 0.05);
}

TEST(ShadowFit, CapReachesTheHorizonWhenTheCameraLooksAtIt) {
    // 1 km up the horizon is sqrt(2 R h) = 113 km away; looking at it the cap spans that.
    const ShadowFit::Box cap = ShadowFit::FitVisibleCap(Light(), HoverView(1.0, 88.0), kCentre, kRadius);
    EXPECT_GT(cap.halfExtent, 50.0);
    EXPECT_LT(cap.halfExtent, 400.0);
}

TEST(ShadowFit, BoxHoldsEverySurfacePointTheCameraSees) {
    for (const double pitch : {0.0, 30.0, 60.0, 85.0}) {
        for (const double altitude : {0.001, 1.0, 100.0, 6000.0}) {
            const ShadowFit::View view = HoverView(altitude, pitch);
            const ShadowFit::Box box =
                ShadowFit::Quantize(ShadowFit::FitVisibleCap(Light(), view, kCentre, kRadius), ShadowFit::kMinCapRadii * kRadius);
            // Every surface point a 101 × 101 grid of pixels sees (a sphere grid would miss a
            // footprint a metre across), and the camera's own nadir.
            int checked = 0;
            for (int i = 0; i <= 100; ++i) {
                for (int j = 0; j <= 100; ++j) {
                    const double a = -1.0 + 0.02 * i;
                    const double b = -1.0 + 0.02 * j;
                    const glm::dvec3 direction = glm::normalize(view.front + view.right * (a * view.tanHalfFovY * view.aspect) +
                                                                view.up * (b * view.tanHalfFovY));
                    const glm::dvec3 toEye = view.eye - kCentre;
                    const double along = glm::dot(toEye, direction);
                    const double discriminant = along * along - (glm::dot(toEye, toEye) - kRadius * kRadius);
                    if (discriminant < 0.0 || -along - std::sqrt(discriminant) <= 0.0) {
                        continue;
                    }
                    const glm::dvec3 p = view.eye + direction * (-along - std::sqrt(discriminant));
                    ++checked;
                    EXPECT_TRUE(Inside(box, Light().ToLight(p))) << "pitch " << pitch << " altitude " << altitude;
                }
            }
            // Pitch 85 from 6000 km has the whole frustum above the horizon: nothing to see.
            if (!(pitch >= 60.0 && altitude >= 6000.0)) {
                EXPECT_GT(checked, 0) << "pitch " << pitch << " altitude " << altitude;
            }
        }
    }
}

TEST(ShadowFit, LookingAtTheSkyKeepsTheBoxAtTheMinimumUnderTheCamera) {
    ShadowFit::View view = HoverView(1.0, 0.0);
    view.front = -view.front; // straight up
    view.up = -view.up;
    const ShadowFit::Box cap = ShadowFit::FitVisibleCap(Light(), view, kCentre, kRadius);
    ASSERT_GE(cap.halfExtent, 0.0);
    const ShadowFit::Box fitted = ShadowFit::Quantize(cap, ShadowFit::kMinCapRadii * kRadius);
    EXPECT_NEAR(fitted.halfExtent, ShadowFit::kMinCapRadii * kRadius, 0.2 * ShadowFit::kMinCapRadii * kRadius);
    EXPECT_TRUE(Inside(fitted, Light().ToLight(kCentre + glm::normalize(view.eye - kCentre) * kRadius)));
}

TEST(ShadowFit, ARingOrMoonInViewWidensTheBox) {
    const ShadowFit::View view = HoverView(6000.0, 10.0);
    const ShadowFit::Box plain = ShadowFit::FitVisibleCap(Light(), view, kCentre, kRadius);
    const glm::dvec2 at = Light().ToLight(kCentre) + glm::dvec2(2.0 * kRadius, 0.0);
    // Light-space x is `right`; build a sphere at that offset in the world.
    const glm::dvec3 world = kCentre + Light().right * at.x;
    const ShadowFit::Box widened = ShadowFit::FitVisibleCap(
        Light(), view, kCentre, kRadius, {{glm::vec3(world), 500.0f}});
    EXPECT_GT(widened.halfExtent, plain.halfExtent);
    EXPECT_TRUE(Inside(widened, Light().ToLight(world)));
}

TEST(ShadowFit, NoCapToFitInsideTheBody) {
    ShadowFit::View view = HoverView(1.0, 0.0);
    view.eye = kCentre + glm::dvec3(0.0, 0.5 * kRadius, 0.0);
    EXPECT_LT(ShadowFit::FitVisibleCap(Light(), view, kCentre, kRadius).halfExtent, 0.0);
}

TEST(ShadowFit, HoveringOnTheFacetsBelowTheAnalyticSphereStillFitsACap) {
    // The floor holds the camera 0.5 m over sphere.obj's facets, which sag ~8 km under the
    // analytic sphere between vertices: the camera is "below" `radius` and must still get a
    // cap, a few tens of kilometres at most, not the full box.
    ShadowFit::View view = HoverView(-8.0, 30.0);
    const ShadowFit::Box cap = ShadowFit::FitVisibleCap(Light(), view, kCentre, kRadius);
    ASSERT_GT(cap.halfExtent, 0.0);
    EXPECT_LT(cap.halfExtent, 100.0);
    // Looking straight down: a footprint a few metres across, the minimum box.
    const ShadowFit::Box down = ShadowFit::FitVisibleCap(Light(), HoverView(-8.0, 0.0), kCentre, kRadius);
    ASSERT_GT(down.halfExtent, 0.0);
    EXPECT_LT(down.halfExtent, 0.1);
}

TEST(ShadowFit, QuantizedBoxDoesNotSwimAsTheCameraMovesAndStillCovers) {
    const ShadowFit::Box first = ShadowFit::Quantize(ShadowFit::FitVisibleCap(Light(), HoverView(1.0, 20.0), kCentre, kRadius),
                                                     ShadowFit::kMinCapRadii * kRadius);
    // A metre of drift over a 20 km box moves the centre by far less than a snap step, and
    // the extent is on a quarter-octave step, so the box is the same one.
    int same = 0;
    for (int i = 1; i <= 10; ++i) {
        const double altitude = 1.0 + 0.0001 * i;
        const ShadowFit::Box next = ShadowFit::Quantize(
            ShadowFit::FitVisibleCap(Light(), HoverView(altitude, 20.0), kCentre, kRadius), ShadowFit::kMinCapRadii * kRadius);
        same += next.halfExtent == first.halfExtent && next.centerX == first.centerX && next.centerY == first.centerY;
    }
    EXPECT_GE(same, 9);

    const ShadowFit::Box raw = ShadowFit::FitVisibleCap(Light(), HoverView(1.0, 20.0), kCentre, kRadius);
    EXPECT_GE(first.halfExtent - std::abs(first.centerX - raw.centerX), raw.halfExtent);
    EXPECT_GE(first.halfExtent - std::abs(first.centerY - raw.centerY), raw.halfExtent);
    // The quarter-octave step costs at most 19 % (plus the snap).
    EXPECT_LT(first.halfExtent, 1.2 * std::max(raw.halfExtent, ShadowFit::kMinCapRadii * kRadius) + 1e-9);
}

TEST(ShadowFit, CapEngagesWithinFewRadiiAndReleasesWithHysteresis) {
    using ShadowFit::UseVisibleCap;
    EXPECT_TRUE(UseVisibleCap(false, 1.0));
    EXPECT_FALSE(UseVisibleCap(false, 3.2)); // not engaged yet
    EXPECT_TRUE(UseVisibleCap(true, 3.2));   // engaged: holds through the gap
    EXPECT_FALSE(UseVisibleCap(true, 4.2));
    EXPECT_FALSE(UseVisibleCap(false, 100.0)); // interplanetary: the full box
}

} // namespace
