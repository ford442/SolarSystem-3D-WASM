// The WebGPU backend against a real device (docs/ARCHITECTURE.md §7.9): Dawn natively, on
// whatever Vulkan adapter the machine has (SwiftShader or lavapipe in CI, see
// tests/CMakeLists.txt). Without an adapter every test skips.
//
// The parity tests run the same GLSL pass on GL (a hidden GLFW window, Mesa under Xvfb) and on
// WebGPU (the WGSL tools/wgsl_shader_compiler made from it) and compare the pixels in CIE76 ΔE,
// the measure the backend's acceptance uses for golden images (ΔE < 2).
#include <gtest/gtest.h>

#include "gfx/GlPlatform.h"
#include <GLFW/glfw3.h>

#include "gfx/GlHandle.h"
#include "gfx/Shader.h"
#include "gfx/rhi/RhiTypes.h"
#include "gfx/rhi/wgpu/WgpuCommandList.h"
#include "gfx/rhi/wgpu/WgpuDevice.h"
#include "gfx/rhi/wgpu/WgpuProgram.h"
#include "gfx/rhi/wgpu/WgpuSelfTest.h"

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

std::unique_ptr<rhi::webgpu::Device> g_device;
GLFWwindow* g_window = nullptr;
std::string g_deviceError;

constexpr const char* kWgslDirectory = "resource/shaders-wgsl";

class WgpuEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        std::filesystem::current_path(SOLARSYSTEM_SOURCE_DIR);
        rhi::webgpu::Device::Options options;
        g_device = rhi::webgpu::Device::Create(options, g_deviceError);
        // The GL side of the parity tests, as tests/gl/test_gl.cpp makes it.
        if (glfwInit()) {
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            g_window = glfwCreateWindow(64, 64, "SolarSystemWgpuTests", nullptr, nullptr);
            if (g_window) {
                glfwMakeContextCurrent(g_window);
                glewExperimental = GL_TRUE;
                if (glewInit() != GLEW_OK) {
                    glfwDestroyWindow(g_window);
                    g_window = nullptr;
                }
            }
        }
    }
    void TearDown() override {
        g_device.reset();
        if (g_window) {
            glfwDestroyWindow(g_window);
            glfwTerminate();
            g_window = nullptr;
        }
    }
};

const ::testing::Environment* const g_environment = ::testing::AddGlobalTestEnvironment(new WgpuEnvironment);

#define REQUIRE_WEBGPU()                                                                   \
    do {                                                                                   \
        if (!g_device) {                                                                   \
            GTEST_SKIP() << "no WebGPU adapter: " << g_deviceError;                        \
        }                                                                                  \
        if (!std::filesystem::exists(std::string(kWgslDirectory) + "/manifest.json")) {    \
            GTEST_SKIP() << "no " << kWgslDirectory << " (build the wgsl_shaders target)"; \
        }                                                                                  \
    } while (0)

/** sRGB 8-bit → CIE L*a*b* (D65): what ΔE is measured in. */
glm::vec3 Lab(const std::uint8_t* rgb) {
    const auto linear = [](std::uint8_t c) {
        const double v = c / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    const double r = linear(rgb[0]), g = linear(rgb[1]), b = linear(rgb[2]);
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    const auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    return {static_cast<float>(116.0 * f(y) - 16.0), static_cast<float>(500.0 * (f(x) - f(y))),
            static_cast<float>(200.0 * (f(y) - f(z)))};
}

/** The largest CIE76 ΔE between two RGBA8 images of the same size. */
double MaxDeltaE(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    double worst = 0.0;
    for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
        worst = std::max(worst, static_cast<double>(glm::length(Lab(&a[i]) - Lab(&b[i]))));
    }
    return worst;
}

std::vector<std::uint8_t> Rgba8(int size, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::vector<std::uint8_t> pixels;
    for (int i = 0; i < size * size; ++i) {
        pixels.insert(pixels.end(), {r, g, b, 255});
    }
    return pixels;
}

/**
 * The staircase of tests/gl/test_gl.cpp's tone-map test, made asymmetric: value 4 where x < y,
 * a dim red ramp elsewhere. Rows bottom-up (row 0 is v = 0) as both APIs upload them.
 */
std::vector<glm::vec4> HdrInput(int size) {
    std::vector<glm::vec4> texels(static_cast<std::size_t>(size * size));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            texels[static_cast<std::size_t>(y * size + x)] =
                x < y ? glm::vec4(4.0f, 4.0f, 4.0f, 1.0f) : glm::vec4(0.05f * static_cast<float>(x), 0.0f, 0.1f, 1.0f);
        }
    }
    return texels;
}

constexpr float kQuad[] = {
    // position          uv    (HDR::InitQuadBuffers, drawn as a triangle strip)
    -1.0f, 1.0f,  0.0f, 0.0f, 1.0f, //
    -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, //
    1.0f,  1.0f,  0.0f, 1.0f, 1.0f, //
    1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
};

rhi::VertexLayout QuadLayout() {
    rhi::VertexLayout layout;
    layout.stride = 5 * sizeof(float);
    layout.Attribute(0, 3, 0).Attribute(1, 2, 3 * sizeof(float));
    return layout;
}

std::unique_ptr<rhi::webgpu::Program> MakeProgram(const rhi::webgpu::ShaderManifest& manifest, const char* vertex,
                                                  const char* fragment) {
    std::string error;
    auto program = rhi::webgpu::Program::Create(*g_device, manifest, {vertex, fragment, {}}, error);
    EXPECT_NE(program, nullptr) << error;
    return program;
}

rhi::webgpu::ShaderManifest LoadManifest() {
    rhi::webgpu::ShaderManifest manifest;
    std::string error;
    EXPECT_TRUE(manifest.Load(kWgslDirectory, error)) << error;
    return manifest;
}

} // namespace

TEST(WgpuShaderSelfTest, EveryWgslProgramCompilesAndLinks) {
    REQUIRE_WEBGPU();
    EXPECT_EQ(rhi::webgpu::SelfTestWgsl(*g_device, kWgslDirectory), 0);
    EXPECT_TRUE(g_device->TakeErrors().empty());
}

TEST(WgpuCommandList, ScreenFadeBlendsOverAClearedTarget) {
    REQUIRE_WEBGPU();
    const rhi::webgpu::ShaderManifest manifest = LoadManifest();
    auto program = MakeProgram(manifest, "screenFade.vs", "screenFade.fs");
    ASSERT_NE(program, nullptr);

    constexpr int kSize = 16;
    rhi::webgpu::CommandList cmd(*g_device);
    cmd.ResizeBackbuffer(kSize, kSize);
    cmd.BeginFrame();
    cmd.BeginPass({"screen-fade", "screen-fade", -1, -1});
    cmd.SetRenderTarget({0, 0, 0, kSize, kSize});
    rhi::ClearValue blue;
    blue.color = {0.0f, 0.0f, 1.0f, 1.0f};
    cmd.SetClearValue(blue);
    cmd.Clear(rhi::ClearFlags::ColorDepth);
    // Renderer::ScreenFade's state.
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetBlend(true).SetBlendFunc(
        rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha));
    program->SetVec4("fadeColor", glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
    cmd.SetProgram(program.get());
    cmd.DrawFullscreenTriangle(0);
    cmd.EndPass();
    cmd.EndFrame();

    const std::vector<std::uint8_t> pixels = cmd.ReadColor(0);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(kSize * kSize * 4));
    for (const int at : {0, kSize * kSize / 2 + 3, kSize * kSize - 1}) {
        SCOPED_TRACE(at);
        EXPECT_NEAR(pixels[at * 4 + 0], 128, 1);
        EXPECT_NEAR(pixels[at * 4 + 1], 0, 1);
        EXPECT_NEAR(pixels[at * 4 + 2], 128, 1);
    }
    EXPECT_EQ(cmd.GetStats().renderPasses, 1u); // the clear rode on the draw's pass
    EXPECT_TRUE(g_device->TakeErrors().empty());
}

TEST(WgpuCommandList, TheViewportOriginIsGlsBottomLeftAndAClearAloneStillRuns) {
    REQUIRE_WEBGPU();
    const rhi::webgpu::ShaderManifest manifest = LoadManifest();
    auto program = MakeProgram(manifest, "screenFade.vs", "screenFade.fs");
    ASSERT_NE(program, nullptr);

    constexpr int kSize = 16;
    rhi::webgpu::CommandList cmd(*g_device);
    cmd.ResizeBackbuffer(kSize, kSize);
    cmd.BeginFrame();
    cmd.BeginPass({"clear", "clear", -1, -1});
    cmd.SetRenderTarget({0, 0, 0, kSize, kSize});
    rhi::ClearValue green;
    green.color = {0.0f, 1.0f, 0.0f, 1.0f};
    cmd.SetClearValue(green);
    cmd.Clear(rhi::ClearFlags::Color);
    cmd.EndPass(); // nothing drew: an empty pass still clears

    cmd.BeginPass({"bottom", "fade", -1, -1});
    cmd.SetViewport(0, 0, kSize, kSize / 2); // GL: the bottom half
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetBlend(false));
    program->SetVec4("fadeColor", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    cmd.SetProgram(program.get());
    cmd.DrawFullscreenTriangle(0);
    cmd.EndPass();
    cmd.EndFrame();

    // Rows come back bottom-up like glReadPixels: the first half red, the rest still green.
    const std::vector<std::uint8_t> pixels = cmd.ReadColor(0);
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(kSize * kSize * 4));
    const auto at = [&](int x, int y) { return &pixels[static_cast<std::size_t>((y * kSize + x) * 4)]; };
    EXPECT_EQ(at(5, 0)[0], 255);
    EXPECT_EQ(at(5, kSize / 2 - 1)[0], 255);
    EXPECT_EQ(at(5, kSize / 2)[0], 0);
    EXPECT_EQ(at(5, kSize / 2)[1], 255);
    EXPECT_EQ(at(5, kSize - 1)[1], 255);
    EXPECT_EQ(cmd.GetStats().renderPasses, 2u);
    EXPECT_TRUE(g_device->TakeErrors().empty());
}

TEST(WgpuCommandList, PresentFlipsTheBackbufferIntoTopDownRows) {
    REQUIRE_WEBGPU();
    const rhi::webgpu::ShaderManifest manifest = LoadManifest();
    auto program = MakeProgram(manifest, "screenFade.vs", "screenFade.fs");
    ASSERT_NE(program, nullptr);
    constexpr int kSize = 8;
    rhi::webgpu::CommandList cmd(*g_device);
    cmd.ResizeBackbuffer(kSize, kSize);
    // A stand-in for the canvas's texture: an ordinary top-down WebGPU target.
    rhi::TextureDesc surfaceDesc;
    surfaceDesc.width = kSize;
    surfaceDesc.height = kSize;
    surfaceDesc.format = rhi::TextureFormat::Rgba8;
    const rhi::ObjectId surface = cmd.CreateTexture(surfaceDesc);
    const rhi::FramebufferId surfaceTarget = cmd.CreateFramebuffer(surface, 0);

    cmd.BeginFrame();
    cmd.BeginPass({"bottom-row", "fade", -1, -1});
    cmd.SetRenderTarget({0, 0, 0, kSize, 1}); // GL row 0: the bottom of the picture
    cmd.SetClearValue(rhi::ClearValue{});
    cmd.Clear(rhi::ClearFlags::Color);
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetBlend(false));
    program->SetVec4("fadeColor", glm::vec4(1.0f));
    cmd.SetProgram(program.get());
    cmd.DrawFullscreenTriangle(0);
    cmd.EndPass();
    cmd.PresentInto(surface);
    cmd.EndFrame();

    const std::vector<std::uint8_t> backbuffer = cmd.ReadColor(0);
    const std::vector<std::uint8_t> presented = cmd.ReadColor(surfaceTarget);
    ASSERT_EQ(backbuffer.size(), static_cast<std::size_t>(kSize * kSize * 4));
    ASSERT_EQ(presented.size(), backbuffer.size());
    const auto row = [&](const std::vector<std::uint8_t>& image, int y) {
        return static_cast<int>(image[static_cast<std::size_t>(y * kSize * 4 + 4)]);
    };
    // The backbuffer holds GL's order (the white row first in memory); the surface is top-down,
    // so the picture's bottom row is its last.
    EXPECT_EQ(row(backbuffer, 0), 255);
    EXPECT_EQ(row(backbuffer, kSize - 1), 0);
    EXPECT_EQ(row(presented, kSize - 1), 255);
    EXPECT_EQ(row(presented, 0), 0);
    EXPECT_TRUE(g_device->TakeErrors().empty());
}

TEST(WgpuParity, TheToneMapPassMatchesGlWithinDeltaE2) {
    REQUIRE_WEBGPU();
    if (!g_window) {
        GTEST_SKIP() << "no GL 4.6 context for the GL side (run under Xvfb)";
    }
    constexpr int kSize = 32;
    const std::vector<glm::vec4> input = HdrInput(kSize);
    const float exposure = 1.3f;
    const float gamma = 0.4545454f;

    // GL: HDR::Render's draw, onto an RGBA8 target.
    std::vector<std::uint8_t> gl(kSize * kSize * 4);
    {
        const Shader shader("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
        GlTexture source = GlTexture::Create();
        glBindTexture(GL_TEXTURE_2D, source.get());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, kSize, kSize, 0, GL_RGBA, GL_FLOAT, input.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        GlTexture color = GlTexture::Create();
        glBindTexture(GL_TEXTURE_2D, color.get());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        GlFramebuffer target = GlFramebuffer::Create();
        glBindFramebuffer(GL_FRAMEBUFFER, target.get());
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color.get(), 0);
        ASSERT_EQ(glCheckFramebufferStatus(GL_FRAMEBUFFER), static_cast<GLenum>(GL_FRAMEBUFFER_COMPLETE));
        GlVertexArray vao = GlVertexArray::Create();
        GlBuffer vbo = GlBuffer::Create();
        glBindVertexArray(vao.get());
        glBindBuffer(GL_ARRAY_BUFFER, vbo.get());
        glBufferData(GL_ARRAY_BUFFER, sizeof kQuad, kQuad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                              reinterpret_cast<void*>(3 * sizeof(float))); // NOLINT(performance-no-int-to-ptr)
        glViewport(0, 0, kSize, kSize);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        shader.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, source.get());
        shader.SetBool("hdr", true);
        shader.SetFloat("exposure", exposure);
        shader.SetFloat("gamma", gamma);
        shader.SetVec2("texelSize", 1.0f / kSize, 1.0f / kSize);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, gl.data());
        glBindVertexArray(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ASSERT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));
    }

    // WebGPU: the same pass through the backend.
    const rhi::webgpu::ShaderManifest manifest = LoadManifest();
    auto program = MakeProgram(manifest, "passThrough.vs", "hdr.fs");
    ASSERT_NE(program, nullptr);
    rhi::webgpu::CommandList cmd(*g_device);
    cmd.ResizeBackbuffer(kSize, kSize);
    rhi::TextureDesc hdrDesc;
    hdrDesc.width = kSize;
    hdrDesc.height = kSize;
    hdrDesc.format = rhi::TextureFormat::Rgba16F;
    const rhi::ObjectId hdrTexture = cmd.CreateTexture(hdrDesc);
    std::vector<std::uint16_t> halves;
    for (const glm::vec4& texel : input) {
        for (int c = 0; c < 4; ++c) {
            halves.push_back(glm::packHalf1x16(texel[c]));
        }
    }
    cmd.WriteTexture(hdrTexture, halves.data(), kSize * 8);
    const rhi::ObjectId quad = cmd.CreateVertexBuffer(kQuad, sizeof kQuad);

    cmd.BeginFrame();
    cmd.BeginPass({"hdr-composite", "hdr-composite", -1, -1});
    cmd.SetRenderTarget({0, 0, 0, kSize, kSize});
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetCullFace(false).SetBlend(false));
    cmd.SetBindGroup(rhi::BindGroup().Texture(0, hdrTexture));
    program->SetBool("hdr", true);
    program->SetFloat("exposure", exposure);
    program->SetFloat("gamma", gamma);
    program->SetVec2("texelSize", glm::vec2(1.0f / kSize));
    cmd.SetProgram(program.get());
    cmd.SetVertexBuffer(quad, QuadLayout());
    cmd.Draw(4, rhi::PrimitiveTopology::TriangleStrip);
    cmd.EndPass();
    cmd.EndFrame();
    const std::vector<std::uint8_t> wgpuPixels = cmd.ReadColor(0);
    ASSERT_EQ(wgpuPixels.size(), gl.size());
    EXPECT_TRUE(g_device->TakeErrors().empty());

    // Same orientation: only the staircase (x < y, the top-left half) has any green, in both.
    const auto green = [&](const std::vector<std::uint8_t>& image, int x, int y) {
        return static_cast<int>(image[static_cast<std::size_t>((y * kSize + x) * 4 + 1)]);
    };
    EXPECT_GT(green(gl, 2, kSize - 3), 200);
    EXPECT_GT(green(wgpuPixels, 2, kSize - 3), 200);
    EXPECT_LT(green(gl, kSize - 3, 2), 20);
    EXPECT_LT(green(wgpuPixels, kSize - 3, 2), 20);
    const double deltaE = MaxDeltaE(gl, wgpuPixels);
    RecordProperty("max_delta_e", std::to_string(deltaE));
    std::printf("[WgpuParity] hdr-composite: max ΔE %.3f over %d pixels\n", deltaE, kSize * kSize);
    EXPECT_LT(deltaE, 2.0) << "the WebGPU tone map differs from GL's by ΔE " << deltaE;
}

TEST(WgpuCommandList, TimestampsLandInThePassesTimerSlot) {
    REQUIRE_WEBGPU();
    if (!g_device->HasTimestampQuery()) {
        GTEST_SKIP() << "adapter has no timestamp-query";
    }
    const rhi::webgpu::ShaderManifest manifest = LoadManifest();
    auto program = MakeProgram(manifest, "screenFade.vs", "screenFade.fs");
    ASSERT_NE(program, nullptr);
    rhi::webgpu::CommandList cmd(*g_device);
    cmd.ResizeBackbuffer(256, 256);
    constexpr int kTimerSlot = 3; // FrameProfile slot kStageCount + 3
    for (int frame = 0; frame < 4 && !cmd.HasGpuResult(); ++frame) {
        cmd.BeginFrame();
        cmd.BeginPass({"screen-fade", "screen-fade", kTimerSlot, -1});
        cmd.SetRenderTarget({0, 0, 0, 256, 256});
        program->SetVec4("fadeColor", glm::vec4(0.5f));
        cmd.SetProgram(program.get());
        cmd.DrawFullscreenTriangle(0);
        cmd.EndPass();
        cmd.EndFrame();
        (void)cmd.ReadColor(0); // waits for the GPU, which lets the timestamp readback map
        g_device->Poll();
    }
    ASSERT_TRUE(cmd.HasGpuResult());
    EXPECT_GE(cmd.GpuMs(FrameProfile::kStageCount + kTimerSlot), 0.0);
}
