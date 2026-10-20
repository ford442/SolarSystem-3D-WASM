// GL-touching tests (#149): a hidden GLFW window with a real 4.6 core context. Run them under
// Xvfb/llvmpipe in CI (see native-linux-build.yml); without a display every test skips.
#include <gtest/gtest.h>

#include "gfx/GlPlatform.h"
#include <GLFW/glfw3.h>

#include "Solar_System/CatalogMaterial.h"
#include "core/Log.h"
#include "gfx/FrameUniforms.h"
#include "gfx/GlCapabilities.h"
#include "gfx/GlHandle.h"
#include "gfx/HDR.h"
#include "gfx/LineStripBatch.h"
#include "gfx/Shader.h"
#include "gfx/ShadowMapFBO.h"
#include "gfx/TextureImage2D.h"
#include "gfx/TextureUnits.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

GLFWwindow* g_window = nullptr;

class GlContextEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // Shaders and textures are read by repo-relative path, as the app does.
        std::filesystem::current_path(SOLARSYSTEM_SOURCE_DIR);
        if (!glfwInit()) {
            return;
        }
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        g_window = glfwCreateWindow(64, 64, "SolarSystemGlTests", nullptr, nullptr);
        if (!g_window) {
            glfwTerminate();
            return;
        }
        glfwMakeContextCurrent(g_window);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            glfwDestroyWindow(g_window);
            g_window = nullptr;
            glfwTerminate();
        }
    }

    void TearDown() override {
        if (g_window) {
            glfwDestroyWindow(g_window);
            glfwTerminate();
            g_window = nullptr;
        }
    }
};

const ::testing::Environment* const g_environment =
    ::testing::AddGlobalTestEnvironment(new GlContextEnvironment);

#define REQUIRE_GL_CONTEXT()                                                                         \
    do {                                                                                             \
        if (!g_window) {                                                                             \
            GTEST_SKIP() << "no GL 4.6 context (run under Xvfb with MESA_GL_VERSION_OVERRIDE=4.6)";   \
        }                                                                                            \
    } while (0)

GLuint CurrentProgram() {
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    return static_cast<GLuint>(program);
}

GLenum FramebufferStatus(GLuint fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return status;
}

const Shader& PlanetProgram(unsigned variant) {
    static std::array<std::unique_ptr<Shader>, CatalogMaterial::kVariantCount> programs;
    if (!programs[variant]) {
        programs[variant] = std::make_unique<Shader>("resource/shaders/planetLighting.vs",
                                                     "resource/shaders/planetLighting.fs",
                                                     CatalogMaterial::Defines(variant));
    }
    return *programs[variant];
}

} // namespace

TEST(GlHandle, CreatesMovesAndDeletesARealObject) {
    REQUIRE_GL_CONTEXT();
    GlBuffer buffer = GlBuffer::Create();
    ASSERT_TRUE(buffer);
    const GLuint id = buffer.get();
    glBindBuffer(GL_ARRAY_BUFFER, id); // a generated name becomes a buffer object on first bind
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    EXPECT_TRUE(glIsBuffer(id));

    GlBuffer moved = std::move(buffer);
    EXPECT_FALSE(buffer); // NOLINT(bugprone-use-after-move): the moved-from handle must be empty
    EXPECT_EQ(moved.get(), id);
    EXPECT_TRUE(glIsBuffer(id));

    moved.reset();
    EXPECT_FALSE(moved);
    EXPECT_FALSE(glIsBuffer(id));
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(Shader, EveryPlanetVariantLinksAndReadsFrameUboAtItsBinding) {
    REQUIRE_GL_CONTEXT();
    for (unsigned variant = 0; variant < CatalogMaterial::kVariantCount; ++variant) {
        SCOPED_TRACE(variant);
        const Shader& shader = PlanetProgram(variant);
        shader.Use();
        const GLuint program = CurrentProgram();
        ASSERT_NE(program, 0u);
        EXPECT_TRUE(shader.UsesFrameUniforms());
        const GLuint block = glGetUniformBlockIndex(program, "FrameUBO");
        ASSERT_NE(block, GL_INVALID_INDEX);
        GLint binding = -1;
        glGetActiveUniformBlockiv(program, block, GL_UNIFORM_BLOCK_BINDING, &binding);
        EXPECT_EQ(binding, static_cast<GLint>(FrameUniformBuffer::kBinding));
    }
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(Shader, SamplerUnitsComeFromTheirLayoutBindingName) {
    REQUIRE_GL_CONTEXT();
    const Shader& shader = PlanetProgram(CatalogMaterial::kNightTexture | CatalogMaterial::kSpecularMap);
    shader.Use();
    const GLuint program = CurrentProgram();
    const auto unitOf = [program](const char* sampler) {
        GLint unit = -1;
        glGetUniformiv(program, glGetUniformLocation(program, sampler), &unit);
        return unit;
    };
    EXPECT_EQ(unitOf("mainDiffuseTexture"), static_cast<GLint>(Unit(TextureUnit::Diffuse)));
    EXPECT_EQ(unitOf("normalMap"), static_cast<GLint>(Unit(TextureUnit::Normal)));
    EXPECT_EQ(unitOf("nightTexture"), static_cast<GLint>(Unit(TextureUnit::Night)));
    EXPECT_EQ(unitOf("specularMap"), static_cast<GLint>(Unit(TextureUnit::Specular)));
    EXPECT_EQ(unitOf("shadowMap"), static_cast<GLint>(Unit(TextureUnit::ShadowMap)));
}

TEST(Shader, UniformCacheSkipsUnchangedValuesAndUnknownNames) {
    REQUIRE_GL_CONTEXT();
    const Shader& shader = PlanetProgram(0);
    shader.Use();
    const auto callsFor = [](auto&& set) {
        const unsigned long long before = Shader::UniformCallCount();
        set();
        return Shader::UniformCallCount() - before;
    };
    shader.SetFloat("ambientFactor", 0.123f);
    EXPECT_EQ(callsFor([&] { shader.SetFloat("ambientFactor", 0.123f); }), 0u);
    EXPECT_EQ(callsFor([&] { shader.SetFloat("ambientFactor", 0.5f); }), 1u);
    EXPECT_EQ(callsFor([&] { shader.SetFloat("noSuchUniform", 1.0f); }), 0u);

    GLfloat value = 0.0f;
    glGetUniformfv(CurrentProgram(), glGetUniformLocation(CurrentProgram(), "ambientFactor"), &value);
    EXPECT_FLOAT_EQ(value, 0.5f);
}

TEST(Framebuffers, ShadowMapAndHdrTargetsAreComplete) {
    REQUIRE_GL_CONTEXT();
    ShadowMapFBO shadowMap(256, 256);
    EXPECT_EQ(FramebufferStatus(shadowMap.GetFBO()), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
    shadowMap.Resize(512, 128);
    EXPECT_EQ(shadowMap.GetShadowMapWidth(), 512);
    EXPECT_EQ(FramebufferStatus(shadowMap.GetFBO()), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));

    const Shader hdrShader("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    HDR hdr(hdrShader, 64, 48);
    ASSERT_EQ(hdr.IsEnabled(), GetGlCapabilities().colorBufferFloat);
    if (hdr.IsEnabled()) {
        EXPECT_EQ(FramebufferStatus(hdr.GetHdrFBO()), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
        hdr.Resize(32, 32);
        EXPECT_EQ(FramebufferStatus(hdr.GetHdrFBO()), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
    }
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(Draw, ALineStripBatchQuadFillsTheTarget) {
    REQUIRE_GL_CONTEXT();
    constexpr int kSize = 16;
    GlTexture color = GlTexture::Create();
    glBindTexture(GL_TEXTURE_2D, color.get());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GlFramebuffer fbo = GlFramebuffer::Create();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.get(), 0);
    ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
    glViewport(0, 0, kSize, kSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // The full clip-space square, through the same VAO layout every ribbon shader reads.
    const glm::vec3 corners[4] = {{-1.0f, -1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}};
    std::vector<RibbonVertex> verts;
    LineStripBatch::AppendQuad(verts, corners, glm::vec3(0.0f, 0.0f, 1.0f));
    LineStripBatch batch;
    batch.Upload(verts);
    ASSERT_EQ(batch.VertexCount(), 6);

    const Shader shader("resource/shaders/orbitPath.vs", "resource/shaders/orbitPath.fs");
    shader.Use();
    shader.SetMat4("model", glm::mat4(1.0f));
    shader.SetMat4("view", glm::mat4(1.0f));
    shader.SetMat4("projection", glm::mat4(1.0f));
    shader.SetVec4("color", glm::vec4(1.0f, 0.5f, 0.0f, 1.0f));
    batch.Draw();

    std::array<unsigned char, 4> centre{}, corner{};
    glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre.data());
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    EXPECT_EQ(centre[0], 255);
    EXPECT_NEAR(centre[1], 128, 1);
    EXPECT_EQ(centre[2], 0);
    EXPECT_EQ(corner[0], 255); // both triangles drawn, not just one
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(TextureImage2D, UploadsAPlaceholderDds) {
    REQUIRE_GL_CONTEXT();
    // The fallback checkerboard is also 4x4, so only the log tells a real upload from it.
    std::vector<std::string> warnings;
    Log::SetSinkForTesting([&warnings](Log::Level level, const std::string& line) {
        if (level >= Log::Level::Warn) {
            warnings.push_back(line);
        }
    });
    const TextureImage2D texture("resource/textures_low/Mercury_Diffuse_Low.dds");
    Log::SetSinkForTesting({});
    EXPECT_EQ(warnings, std::vector<std::string>{});
    ASSERT_NE(texture.GetTexture(), 0u);
    EXPECT_TRUE(glIsTexture(texture.GetTexture()));
    EXPECT_EQ(texture.GetWidth(), 4u);
    EXPECT_EQ(texture.GetHeight(), 4u);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}
