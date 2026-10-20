// Frustum culling and the shadow depth fit (gfx/Frustum.h): the side-plane test Application::
// CullScene runs per view, and the near/far range UpdateScene fits each light frustum to.

#include "gfx/Frustum.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

namespace {

Frustum LookingDownMinusZ() {
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.001f, 20000.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    return Frustum(projection * view);
}

TEST(Frustum, KeepsWhatIsAheadAndDropsWhatIsBehindOrBeside) {
    const Frustum frustum = LookingDownMinusZ();
    EXPECT_TRUE(frustum.Intersects({{0.0f, 0.0f, -10.0f}, 1.0f}));
    EXPECT_FALSE(frustum.Intersects({{0.0f, 0.0f, 10.0f}, 1.0f}));  // behind the camera
    EXPECT_FALSE(frustum.Intersects({{50.0f, 0.0f, -10.0f}, 1.0f})); // far off to the right
    EXPECT_FALSE(frustum.Intersects({{0.0f, -50.0f, -10.0f}, 1.0f}));
}

TEST(Frustum, IsConservativeAtTheEdgesAndAroundTheEye) {
    const Frustum frustum = LookingDownMinusZ();
    // Centre just outside the 30° half-angle, radius reaching back in.
    const float edge = 10.0f * std::tan(glm::radians(30.0f));
    EXPECT_TRUE(frustum.Intersects({{edge + 0.5f, 0.0f, -10.0f}, 1.0f}));
    EXPECT_FALSE(frustum.Intersects({{edge + 2.0f, 0.0f, -10.0f}, 1.0f}));
    // A shell around the camera (inside an atmosphere) is always drawn.
    EXPECT_TRUE(frustum.Intersects({{0.0f, 0.0f, 5.0f}, 6.0f}));
    // Past the far plane still passes: GL clips it, culling leaves it alone.
    EXPECT_TRUE(frustum.Intersects({{0.0f, 0.0f, -30000.0f}, 1.0f}));
}

TEST(FitDepthRange, CoversEveryBodyAlongTheLightWithAMargin) {
    const glm::vec3 sun(0.0f);
    const glm::vec3 direction(1.0f, 0.0f, 0.0f);
    // Planet at 36000 (Uranus on realistic scale, past the camera's 20000 far plane), a moon
    // on the far side and one between the planet and the Sun.
    const std::vector<BoundingSphere> bodies = {
        {{36000.0f, 0.0f, 0.0f}, 8.0f}, {{36030.0f, 5.0f, 0.0f}, 1.0f}, {{35980.0f, 0.0f, 3.0f}, 0.5f}};
    const DepthRange range = FitDepthRange(sun, direction, bodies, 0.5f, 0.001f);
    EXPECT_FLOAT_EQ(range.nearPlane, 35980.0f - 0.5f - 0.5f);
    EXPECT_FLOAT_EQ(range.farPlane, 36030.0f + 1.0f + 0.5f);
}

TEST(FitDepthRange, NeverGoesBehindTheLightAndStaysNonEmpty) {
    const DepthRange around = FitDepthRange(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                                            {{{0.0f, 0.0f, 1.0f}, 5.0f}}, 0.1f, 0.001f);
    EXPECT_FLOAT_EQ(around.nearPlane, 0.001f);
    EXPECT_FLOAT_EQ(around.farPlane, 6.1f);
    const DepthRange none = FitDepthRange(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), {}, 0.1f, 0.001f);
    EXPECT_GT(none.farPlane, none.nearPlane);
}

} // namespace
