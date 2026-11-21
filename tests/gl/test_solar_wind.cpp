// Space weather against a real context (the hidden window test_gl.cpp's environment makes):
// render/SolarWindRenderer's transform-feedback update seeds every active group, moves it,
// keeps it finite and sends some of a magnetosphere's particles down the field lines; and
// star.fs's SUNSPOTS permutation darkens the butterfly bands, and only them.
#include <gtest/gtest.h>

#include "gfx/GlPlatform.h"
#include <GLFW/glfw3.h>

#include "gfx/FrameUniforms.h"
#include "gfx/GlHandle.h"
#include "gfx/Shader.h"
#include "gfx/SpaceWeatherUniforms.h"
#include "gfx/TextureUnits.h"
#include "render/SolarWindRenderer.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr int kParticles = 7000;
constexpr float kEdgeAu = 6.5f;
constexpr float kSpawnAu = 0.01f;
constexpr float kStandoff = 10.0f;

ShaderSpec UpdateSpec() {
    ShaderSpec spec("resource/shaders/solarWindUpdate.vs", "resource/shaders/solarWindUpdate.fs");
    spec.feedbackVaryings = {"vState0", "vState1"};
    return spec;
}

/** Quiet wind, no CMEs, one Earth-like magnetosphere 1 AU out on +x. */
SpaceWeatherUniformData QuietFrame(float dt, unsigned frame) {
    SpaceWeatherUniformData data;
    data.sun = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);
    data.wind = glm::vec4(0.1f, kEdgeAu, 0.1f, static_cast<float>(frame) * dt);
    data.step = glm::vec4(dt, 0.35f, kSpawnAu, 0.2f);
    data.look = glm::vec4(2.0f, 1.0f, kEdgeAu, static_cast<float>(frame));
    data.counts = glm::ivec4(1, 0, 1, 0);
    data.knotAu[0] = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
    data.knotScene[0] = glm::vec4(1900.0f, 0.0f, 0.0f, 0.0f);
    SpaceWeatherBodyData& earth = data.bodies[0];
    earth.helio = glm::vec4(1900.0f, 0.0f, 0.0f, 1.0f);
    earth.render = glm::vec4(1900.0f, 0.0f, 0.0f, 1.0f);
    earth.axis = glm::vec4(0.0f, 1.0f, 0.0f, kStandoff);
    earth.sunward = glm::vec4(-1.0f, 0.0f, 0.0f, 0.58f);
    earth.state = glm::vec4(2.0f / 9.0f, 1.0f, 1.0f, 0.0f);
    earth.oval = glm::radians(glm::vec4(64.0f, 72.0f, 70.0f, 77.0f));
    earth.colorLow = glm::vec4(0.1f, 1.0f, 0.2f, 1.0f);
    earth.colorHigh = glm::vec4(1.0f, 0.1f, 0.1f, kStandoff);
    earth.colorEdge = glm::vec4(0.3f, 0.2f, 1.0f, 1.0f);
    earth.shell = glm::vec4(1.016f, 1.047f, 2.0f, 2.0f);
    return data;
}

std::vector<float> ReadParticles(const SolarWindRenderer& wind) {
    std::vector<float> state(static_cast<std::size_t>(wind.GroupFirst(SolarWindRenderer::kMaxGroups - 1) +
                                                      wind.GroupCount(SolarWindRenderer::kMaxGroups - 1)) *
                             SolarWindRenderer::kFloatsPerParticle);
    glBindBuffer(GL_ARRAY_BUFFER, wind.CurrentBuffer());
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(state.size() * sizeof(float)), state.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return state;
}

GLint BlockBinding(const Shader& shader) {
    shader.Use();
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    const GLuint block = glGetUniformBlockIndex(static_cast<GLuint>(program), "MagnetosphereUBO");
    if (block == GL_INVALID_INDEX) {
        return -1;
    }
    GLint binding = -1;
    glGetActiveUniformBlockiv(static_cast<GLuint>(program), block, GL_UNIFORM_BLOCK_BINDING, &binding);
    return binding;
}

} // namespace

TEST(SolarWind, BothProgramsReadTheMagnetosphereUboAtItsBinding) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    const Shader update(UpdateSpec());
    const Shader draw("resource/shaders/solarWind.vs", "resource/shaders/solarWind.fs");
    EXPECT_EQ(BlockBinding(update), static_cast<GLint>(SpaceWeatherUniformBuffer::kBinding));
    EXPECT_EQ(BlockBinding(draw), static_cast<GLint>(SpaceWeatherUniformBuffer::kBinding));
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(SolarWind, FeedbackSeedsMovesAndPrecipitatesTheActiveGroups) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    const Shader update(UpdateSpec());
    SpaceWeatherUniformBuffer ubo;
    SolarWindRenderer wind;
    wind.SetParticleCount(kParticles);
    ASSERT_EQ(wind.ParticleCount(), kParticles);

    std::array<bool, SolarWindRenderer::kMaxGroups> active{};
    active[0] = true; // the background
    active[1] = true; // body slot 0

    constexpr float kDt = 0.05f;
    ubo.Upload(QuietFrame(kDt, 0));
    ubo.Bind();
    wind.Update(update, active);
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    const auto radius = [](const std::vector<float>& s, int i) {
        const std::size_t o = static_cast<std::size_t>(i) * SolarWindRenderer::kFloatsPerParticle;
        return std::sqrt(s[o] * s[o] + s[o + 1] * s[o + 1] + s[o + 2] * s[o + 2]);
    };
    const auto mode = [](const std::vector<float>& s, int i) {
        return s[static_cast<std::size_t>(i) * SolarWindRenderer::kFloatsPerParticle + 4];
    };

    // Seeded to a steady state: the background spread from the Sun to each parcel's turning
    // radius, the magnetosphere's group in its box, a tenth already on the way down.
    std::vector<float> seeded = ReadParticles(wind);
    double backgroundMean = 0.0;
    for (int i = wind.GroupFirst(0); i < wind.GroupFirst(0) + wind.GroupCount(0); ++i) {
        ASSERT_EQ(mode(seeded, i), 0.0f) << i;
        const float r = radius(seeded, i);
        ASSERT_TRUE(std::isfinite(r));
        EXPECT_GE(r, kSpawnAu * 0.999f);
        EXPECT_LE(r, kEdgeAu * 1.001f);
        backgroundMean += r;
    }
    backgroundMean /= wind.GroupCount(0);
    EXPECT_GT(backgroundMean, 0.3);

    int precipitating = 0;
    for (int i = wind.GroupFirst(1); i < wind.GroupFirst(1) + wind.GroupCount(1); ++i) {
        const float m = mode(seeded, i);
        ASSERT_TRUE(m == 0.0f || m == 1.0f || m == 2.0f) << i;
        ASSERT_TRUE(std::isfinite(radius(seeded, i)));
        EXPECT_LE(radius(seeded, i), 22.0f * kStandoff);
        precipitating += m > 0.5f ? 1 : 0;
    }
    EXPECT_GT(precipitating, wind.GroupCount(1) / 30);
    // A group that was not active is still empty.
    EXPECT_EQ(mode(seeded, wind.GroupFirst(2)), -1.0f);

    // Two seconds on: the stream moves outward, stays finite, and keeps feeding the ovals.
    for (unsigned frame = 1; frame <= 40; ++frame) {
        ubo.Upload(QuietFrame(kDt, frame));
        wind.Update(update, active);
    }
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    const std::vector<float> later = ReadParticles(wind);
    int moved = 0;
    for (int i = wind.GroupFirst(0); i < wind.GroupFirst(0) + wind.GroupCount(0); ++i) {
        ASSERT_TRUE(std::isfinite(radius(later, i)));
        moved += radius(later, i) != radius(seeded, i) ? 1 : 0;
    }
    EXPECT_GT(moved, wind.GroupCount(0) * 9 / 10);
    precipitating = 0;
    for (int i = wind.GroupFirst(1); i < wind.GroupFirst(1) + wind.GroupCount(1); ++i) {
        ASSERT_TRUE(std::isfinite(radius(later, i)));
        // Nothing of the wind is left inside the magnetopause (on the Sun line at least).
        const std::size_t o = static_cast<std::size_t>(i) * SolarWindRenderer::kFloatsPerParticle;
        if (mode(later, i) == 0.0f && -later[o] > 0.0f && std::abs(later[o + 1]) < 1.0f &&
            std::abs(later[o + 2]) < 1.0f) {
            EXPECT_GE(radius(later, i), kStandoff * 0.99f) << i;
        }
        precipitating += mode(later, i) > 0.5f ? 1 : 0;
    }
    EXPECT_GT(precipitating, 0);
}

namespace {

constexpr int kPhotosphereSize = 128;

/**
 * star.fs over a quad whose object-space points (x, y, 1) normalise onto the cap of the
 * sphere facing +z, latitude asin(y / |p|); rgb luminance per pixel, bottom row first.
 */
std::vector<float> RenderPhotosphere(const Shader& star, float coverage) {
    const GlFramebuffer fbo = GlFramebuffer::Create();
    const GlTexture color = GlTexture::Create();
    glBindTexture(GL_TEXTURE_2D, color.get());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kPhotosphereSize, kPhotosphereSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.get(), 0);

    // A white spectrum texel: the photosphere's colour is then the noise and spots alone.
    const GlTexture spectrum = GlTexture::Create();
    const std::uint8_t white[4] = {255, 255, 255, 255};
    glBindTexture(GL_TEXTURE_2D, spectrum.get());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glBindTextureUnit(Unit(TextureUnit::Diffuse), spectrum.get());

    FrameUniformBuffer frame;
    FrameUniformData data;
    data.cameraPos = glm::vec4(0.0f, 0.0f, 10.0f, 1.0f);
    data.zCoef = 1.0f; // log2(1 + w) * zCoef - 1 = 0 at w = 1: the quad sits mid-depth
    frame.Upload(0, data);
    frame.Bind(0);

    const float quad[] = {-1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
                          -1.0f, 1.0f,  1.0f, 1.0f, -1.0f, 1.0f, 1.0f,  1.0f, 1.0f};
    const GlVertexArray vao = GlVertexArray::Create();
    const GlBuffer vbo = GlBuffer::Create();
    glBindVertexArray(vao.get());
    glBindBuffer(GL_ARRAY_BUFFER, vbo.get());
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    star.Use();
    star.SetMat4("model", glm::mat4(1.0f));
    star.SetVec3("shiftStarColor", glm::vec3(1.0f));
    star.SetVec3("colorMult", glm::vec3(1.0f));
    star.SetFloat("uColorMap", 0.5f);
    star.SetBool("isVisible", true);
    // This cycle's spots at ±20° (half-width 7°), none from the previous one.
    star.SetVec4("uSpotBands", glm::radians(glm::vec4(20.0f, 7.0f, 8.0f, 4.0f)));
    star.SetVec2("uSpotCoverage", glm::vec2(coverage, 0.0f));
    star.SetFloat("uSpotDays", 1500.0f);

    glViewport(0, 0, kPhotosphereSize, kPhotosphereSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kPhotosphereSize * kPhotosphereSize * 4));
    glReadPixels(0, 0, kPhotosphereSize, kPhotosphereSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindVertexArray(0);
    std::vector<float> luminance(static_cast<std::size_t>(kPhotosphereSize * kPhotosphereSize));
    for (std::size_t i = 0; i < luminance.size(); ++i) {
        luminance[i] = (pixels[i * 4] + pixels[i * 4 + 1] + pixels[i * 4 + 2]) / (3.0f * 255.0f);
    }
    return luminance;
}

/**
 * Over pixels whose |latitude| (degrees) is in [lo, hi]: the mean luminance, and the fraction
 * of them darker than 0.6 (spots; the granulation never goes that dark).
 */
std::array<float, 2> BandStats(const std::vector<float>& luminance, float lo, float hi) {
    double sum = 0.0;
    int count = 0;
    int dark = 0;
    for (int row = 0; row < kPhotosphereSize; ++row) {
        for (int col = 0; col < kPhotosphereSize; ++col) {
            const float x = (static_cast<float>(col) + 0.5f) / kPhotosphereSize * 2.0f - 1.0f;
            const float y = (static_cast<float>(row) + 0.5f) / kPhotosphereSize * 2.0f - 1.0f;
            const float lat = std::abs(glm::degrees(std::asin(y / std::sqrt(x * x + y * y + 1.0f))));
            if (lat < lo || lat > hi) {
                continue;
            }
            const float l = luminance[static_cast<std::size_t>(row * kPhotosphereSize + col)];
            sum += l;
            dark += l < 0.6f ? 1 : 0;
            ++count;
        }
    }
    return {count > 0 ? static_cast<float>(sum / count) : 0.0f,
            count > 0 ? static_cast<float>(dark) / static_cast<float>(count) : 0.0f};
}

} // namespace

TEST(Photosphere, SunspotsDarkenTheButterflyBandsAndNothingElse) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    const Shader star(ShaderSpec("resource/shaders/star.vs", "resource/shaders/star.fs", {"PHOTOSPHERE", "SUNSPOTS"}));
    const std::vector<float> quiet = RenderPhotosphere(star, 0.0f);
    const std::vector<float> active = RenderPhotosphere(star, 1.0f);
    ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    const auto quietBand = BandStats(quiet, 16.0f, 24.0f);
    const auto activeBand = BandStats(active, 16.0f, 24.0f);
    const auto quietEquator = BandStats(quiet, 0.0f, 5.0f);
    const auto activeEquator = BandStats(active, 0.0f, 5.0f);
    ASSERT_GT(quietBand[0], 0.5f) << "the photosphere drew";
    // At maximum the wings are spotted (a few percent of their area, as on the Sun); at
    // minimum nothing is.
    EXPECT_EQ(quietBand[1], 0.0f);
    EXPECT_GT(activeBand[1], 0.01f);
    EXPECT_LT(activeBand[1], 0.25f);
    EXPECT_LT(activeBand[0], quietBand[0]);
    // The equator, outside both wings, is untouched.
    EXPECT_EQ(activeEquator[1], 0.0f);
    EXPECT_NEAR(activeEquator[0], quietEquator[0], 0.01f);
}
