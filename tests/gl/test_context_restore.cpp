// A lost context, for real: objects built on the test window's context, then a second,
// unshared context made current (none of the old names exist there, as after a WebGL loss
// or a GL_KHR_robustness reset), then gfx/GpuResourceRegistry's InvalidateAll + RecreateAll.
// Every owner must draw again from its own CPU state. The round trip ends back on the
// original context, so the programs other tests keep in statics stay valid.
#include <gtest/gtest.h>

#include "gfx/GlPlatform.h"
#include <GLFW/glfw3.h>

#include "assets/UploadQueue.h"
#include "gfx/FrameUniforms.h"
#include "gfx/GlHandle.h"
#include "gfx/GpuResourceRegistry.h"
#include "gfx/HDR.h"
#include "gfx/Mesh.h"
#include "gfx/Shader.h"
#include "gfx/ShadowMapFBO.h"
#include "gfx/TextureImage2D.h"
#include "gfx/Vertex.h"
#include "../dds_test_writer.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

/** Makes an unshared 4.6 core context current; the destructor goes back to the original. */
class SwappedContext {
public:
    SwappedContext() : _original(glfwGetCurrentContext()) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        _window = glfwCreateWindow(64, 64, "lost context", nullptr, nullptr); // no share
    }
    ~SwappedContext() {
        if (_window) {
            glfwMakeContextCurrent(_original);
            glfwDestroyWindow(_window); // frees whatever was made on it
        }
    }
    SwappedContext(const SwappedContext&) = delete;
    SwappedContext& operator=(const SwappedContext&) = delete;

    bool Ok() const { return _window != nullptr; }
    void MakeFreshCurrent() const { glfwMakeContextCurrent(_window); }
    void MakeOriginalCurrent() const { glfwMakeContextCurrent(_original); }

private:
    GLFWwindow* _original;
    GLFWwindow* _window = nullptr;
};

void DrainUploads() {
    for (int i = 0; i < 1000 && UploadQueue::Get().Pending() > 0; ++i) {
        UploadQueue::Get().Pump(1 << 24);
    }
}

GLenum FramebufferStatus(GLuint fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return status;
}

/** Restore exactly as Application::RestoreGpuContext does, minus the app-level state. */
void LoseAndRestore(const SwappedContext& context, bool toFresh) {
    const std::size_t zeroed = GpuResources::InvalidateAll();
    EXPECT_GT(zeroed, 0u);
    EXPECT_EQ(GpuResources::LiveHandleCount(), 0u);
    if (toFresh) {
        context.MakeFreshCurrent();
    } else {
        context.MakeOriginalCurrent();
    }
    EXPECT_GT(GpuResources::RecreateAll(), 0u);
    DrainUploads();
}

/** The full-target quad as a Mesh (position at location 0, which orbitPath.vs reads). */
Mesh QuadMesh() {
    std::vector<Vertex> vertices(4);
    vertices[0].position = {-1.0f, -1.0f, 0.0f};
    vertices[1].position = {1.0f, -1.0f, 0.0f};
    vertices[2].position = {1.0f, 1.0f, 0.0f};
    vertices[3].position = {-1.0f, 1.0f, 0.0f};
    return Mesh(std::move(vertices), {0, 1, 2, 0, 2, 3}, {});
}

/** Draw `mesh` with `shader` into a fresh 8×8 target; the centre texel. */
std::array<unsigned char, 4> DrawCentre(const Mesh& mesh, const Shader& shader) {
    constexpr int kSize = 8;
    const GlTexture color = GlTexture::Create();
    glBindTexture(GL_TEXTURE_2D, color.get());
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    const GlFramebuffer fbo = GlFramebuffer::Create();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo.get());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.get(), 0);
    glViewport(0, 0, kSize, kSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    shader.Use();
    mesh.Draw(shader);
    std::array<unsigned char, 4> centre{};
    glReadPixels(kSize / 2, kSize / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return centre;
}

} // namespace

TEST(ContextRestore, EveryOwnerDrawsAgainOnAFreshContextAndBack) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    UploadQueue::Get().Clear();
    const SwappedContext context;
    ASSERT_TRUE(context.Ok());

    TextureImage2D texture("resource/textures_low/Mercury_Diffuse_Low.dds");
    ShadowMapFBO shadow(64, 64);
    const Shader toneMap("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    HDR hdrOn(toneMap, 32, 16, true);
    const Mesh mesh = QuadMesh();
    const Shader shader("resource/shaders/orbitPath.vs", "resource/shaders/orbitPath.fs");
    // Set once, before the loss, and never again: the restored program must replay them.
    shader.Use();
    shader.SetMat4("model", glm::mat4(1.0f));
    shader.SetMat4("view", glm::mat4(1.0f));
    shader.SetMat4("projection", glm::mat4(1.0f));
    shader.SetVec4("color", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
    FrameUniformBuffer ubo;
    ubo.Upload(0, FrameUniformData{});
    GlBuffer raw = GlBuffer::Create();
    ASSERT_EQ(DrawCentre(mesh, shader)[1], 255);
    ASSERT_NE(texture.GetTexture(), 0u);
    const std::uint32_t generation = GpuResources::Generation();

    for (const bool toFresh : {true, false}) {
        SCOPED_TRACE(toFresh ? "lost: on a fresh context" : "and back to the original");
        LoseAndRestore(context, toFresh);
        EXPECT_EQ(raw.get(), 0u) << "an eager handle nobody restores is left empty, not deleted";

        ASSERT_NE(texture.GetTexture(), 0u);
        EXPECT_TRUE(glIsTexture(texture.GetTexture()));
        EXPECT_EQ(texture.GetWidth(), 4u);
        EXPECT_EQ(texture.SourcePath(), "resource/textures_low/Mercury_Diffuse_Low.dds");
        EXPECT_EQ(FramebufferStatus(shadow.GetFBO()), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
        EXPECT_EQ(FramebufferStatus(hdrOn.GetHdrFBO()), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));

        const std::array<unsigned char, 4> centre = DrawCentre(mesh, shader);
        EXPECT_EQ(centre[0], 0);
        EXPECT_EQ(centre[1], 255) << "the colour set before the loss was not replayed";

        // Lazily built owners need no RestoreGpu: their next use rebuilds.
        ubo.Upload(0, FrameUniformData{});
        ubo.Bind(0);
        GLint bound = 0;
        glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, FrameUniformBuffer::kBinding, &bound);
        EXPECT_TRUE(glIsBuffer(static_cast<GLuint>(bound)));
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
        raw = GlBuffer::Create();
    }
    EXPECT_EQ(GpuResources::Generation(), generation + 2);
}

TEST(ContextRestore, ABandedUploadInterruptedByTheLossFinishesOnTheNewContext) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    UploadQueue::Get().Clear();
    const SwappedContext context;
    ASSERT_TRUE(context.Ok());
    const std::string path = (std::filesystem::temp_directory_path() / "restore_banded.dds").string();
    DdsTestWriter::Write(path, DdsTestWriter::Make(DdsTestWriter::Format::Rgba32, 256, 256, 1, 5));

    TextureImage2D texture(path);
    std::optional<TextureImage2D::AsyncResult> result;
    texture.ReloadTextureAsync(path, [&result](TextureImage2D::AsyncResult r) { result = r; });
    for (int i = 0; i < 4; ++i) {
        UploadQueue::Get().Pump(4096); // a few bands into the old context
    }
    ASSERT_FALSE(result.has_value()) << "the upload should still be in progress";

    for (const bool toFresh : {true, false}) {
        GpuResources::InvalidateAll();
        if (toFresh) {
            context.MakeFreshCurrent();
        } else {
            context.MakeOriginalCurrent();
        }
        GpuResources::RecreateAll(); // the pending reload is not repeated: it restarts by itself
        for (int i = 0; i < 100000 && UploadQueue::Get().Pending() > 0; ++i) {
            UploadQueue::Get().Pump(1 << 16);
        }
        ASSERT_NE(texture.GetTexture(), 0u);
        EXPECT_TRUE(glIsTexture(texture.GetTexture()));
        EXPECT_EQ(texture.GetWidth(), 256u);
        EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    }
    EXPECT_EQ(result, TextureImage2D::AsyncResult::Uploaded);
    std::filesystem::remove(path);
}
