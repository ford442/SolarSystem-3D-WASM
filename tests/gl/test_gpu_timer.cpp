// gfx/GpuTimer against a real context (the hidden window test_gl.cpp's environment makes):
// a timer brackets one draw, and the result comes back a frame or two later.
#include <gtest/gtest.h>

#include "gfx/GlPlatform.h"
#include <GLFW/glfw3.h>

#include "gfx/GlHandle.h"
#include "gfx/GpuTimer.h"
#include "gfx/Shader.h"

#include <cstddef>

namespace {

constexpr std::size_t kDraw = 0;
constexpr std::size_t kIdle = 1;

} // namespace

TEST(GpuTimer, BracketsOneDrawAndReportsItAFrameLater) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    ASSERT_TRUE(GpuTimer::Supported());
    // One screen-covering triangle (position, uv) through the tone-map program.
    const Shader shader("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    const float triangle[] = {-1.0f, -1.0f, 0.0f,  0.0f, 0.0f, 3.0f, -1.0f, 0.0f,
                              2.0f,  0.0f,  -1.0f, 3.0f, 0.0f, 0.0f, 2.0f};
    const GlVertexArray vao = GlVertexArray::Create();
    const GlBuffer vbo = GlBuffer::Create();
    glBindVertexArray(vao.get());
    glBindBuffer(GL_ARRAY_BUFFER, vbo.get());
    glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<const void*>(3 * sizeof(float))); // NOLINT(performance-no-int-to-ptr)
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 64, 64);

    GpuTimer timer(2);
    timer.SetEnabled(true);
    ASSERT_TRUE(timer.Enabled());
    EXPECT_FALSE(timer.HasResult());

    for (int frame = 0; frame < 8 && !timer.HasResult(); ++frame) {
        timer.BeginFrame();
        timer.Begin(kDraw);
        shader.Use();
        glBindVertexArray(vao.get());
        for (int i = 0; i < 64; ++i) {
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glBindVertexArray(0);
        timer.End(kDraw);
        timer.Begin(kIdle); // a nested Begin is ignored while kDraw is open, but this one is not nested
        timer.End(kIdle);
        timer.EndFrame();
        glFinish(); // what a swap would do: the frame's queries finish
    }
    ASSERT_TRUE(timer.HasResult());
    EXPECT_GT(timer.Ms(kDraw), 0.0);
    EXPECT_GE(timer.Ms(kIdle), 0.0);
    EXPECT_LT(timer.Ms(kDraw), 1000.0);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}

TEST(GpuTimer, NestedBeginIsIgnoredAndDisabledRecordsNothing) {
    if (!glfwGetCurrentContext()) {
        GTEST_SKIP() << "no GL context (run under Xvfb)";
    }
    GpuTimer timer(2);
    for (int frame = 0; frame < 3; ++frame) {
        timer.BeginFrame();
        timer.Begin(kDraw);
        timer.End(kDraw);
        timer.EndFrame();
    }
    EXPECT_FALSE(timer.HasResult()); // never enabled

    timer.SetEnabled(true);
    for (int frame = 0; frame < 4; ++frame) {
        timer.BeginFrame();
        timer.Begin(kDraw);
        timer.Begin(kIdle); // GL time queries cannot nest: ignored
        timer.End(kIdle);
        timer.End(kDraw);
        timer.EndFrame();
        glFinish();
    }
    EXPECT_TRUE(timer.HasResult());
    EXPECT_EQ(timer.Ms(kIdle), 0.0);
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
}
