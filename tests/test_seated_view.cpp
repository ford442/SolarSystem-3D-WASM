#include <gtest/gtest.h>

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Auxiliary_Modules/SeatedView.h"

namespace {

/** Horizon-frame (+X East, +Y Up, +Z -North) unit direction for an azimuth/elevation in degrees. */
glm::vec3 horizonDirection(float azimuthDeg, float elevationDeg) {
    const float az = glm::radians(azimuthDeg), el = glm::radians(elevationDeg);
    return glm::vec3(std::sin(az) * std::cos(el), std::sin(el), -std::cos(az) * std::cos(el));
}

void expectNear(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

} // namespace

// Facing straight ahead (identity head pose), the sky direction at the heading azimuth is
// dead ahead in the headset (-Z) and the zenith stays up.
TEST(SeatedViewTest, HeadingAzimuthAppearsStraightAhead) {
    for (const float heading : {0.0f, 30.0f, 90.0f, 180.0f, 270.0f, -60.0f}) {
        const glm::mat3 view = SeatedView::ViewRotation(glm::mat3(1.0f), heading);
        expectNear(view * horizonDirection(heading, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
        expectNear(view * horizonDirection(heading, 40.0f),
                   glm::vec3(0.0f, std::sin(glm::radians(40.0f)), -std::cos(glm::radians(40.0f))));
        expectNear(view * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }
}

// Facing north (heading 0): east is on the right of the view, west on the left — the sky is not
// mirrored.
TEST(SeatedViewTest, EastIsToTheRightWhenFacingNorth) {
    const glm::mat3 view = SeatedView::ViewRotation(glm::mat3(1.0f), 0.0f);
    EXPECT_GT((view * horizonDirection(90.0f, 0.0f)).x, 0.99f);
    EXPECT_LT((view * horizonDirection(270.0f, 0.0f)).x, -0.99f);
    // Facing south (heading 180): east swaps to the left.
    const glm::mat3 south = SeatedView::ViewRotation(glm::mat3(1.0f), 180.0f);
    EXPECT_LT((south * horizonDirection(90.0f, 0.0f)).x, -0.99f);
}

// Turning the head left (counter-clockwise from above) brings a smaller azimuth ahead: facing
// north and looking left shows the west. The eye view is the inverse of the head rotation.
TEST(SeatedViewTest, TurningTheHeadLeftShowsDecreasingAzimuth) {
    const glm::mat3 headLeft90 = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0)));
    const glm::mat3 eyeView = glm::inverse(headLeft90);
    const glm::mat3 view = SeatedView::ViewRotation(eyeView, 0.0f);
    expectNear(view * horizonDirection(270.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)); // west ahead
    // North is now off to the right of the view (the head turned away from it to the left).
    EXPECT_GT((view * horizonDirection(0.0f, 0.0f)).x, 0.99f);
    // A snap turn of +30 deg of heading (turn right) rotates the sky the other way by 30.
    const glm::mat3 a = SeatedView::ViewRotation(glm::mat3(1.0f), 0.0f);
    const glm::mat3 b = SeatedView::ViewRotation(glm::mat3(1.0f), 30.0f);
    expectNear(b * horizonDirection(30.0f, 0.0f), a * horizonDirection(0.0f, 0.0f));
}

TEST(SeatedViewTest, ResultIsARotation) {
    const glm::mat3 eye = glm::mat3(glm::rotate(glm::mat4(1.0f), 0.7f, glm::normalize(glm::vec3(0.3f, 1.0f, -0.2f))));
    const glm::mat3 view = SeatedView::ViewRotation(eye, 123.0f);
    EXPECT_NEAR(glm::determinant(view), 1.0f, 1e-5f);
    expectNear(view * glm::transpose(view) * glm::vec3(1, 2, 3), glm::vec3(1, 2, 3), 1e-4f);
}

// The flat-screen camera (Euler yaw/pitch, azimuth = yaw + 90) and the seated headset path must
// show the same sky for the same heading: with a level head, ViewRotation(identity, yaw + 90)
// equals the camera's own view rotation.
TEST(SeatedViewTest, MatchesTheFlatCameraForALevelHead) {
    for (const float yaw : {-90.0f, 0.0f, 37.0f, 180.0f, 250.0f}) {
        const float yawRad = glm::radians(yaw);
        const glm::vec3 front(std::cos(yawRad), 0.0f, std::sin(yawRad));
        const glm::mat3 camera = glm::mat3(glm::lookAt(glm::vec3(0.0f), front, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::mat3 seated = SeatedView::ViewRotation(glm::mat3(1.0f), yaw + 90.0f);
        for (int c = 0; c < 3; ++c) {
            expectNear(camera[c], seated[c], 1e-5f);
        }
    }
}
