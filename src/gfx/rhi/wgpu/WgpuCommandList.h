#ifndef SOLARSYSTEM_GFX_RHI_WGPU_WGPUCOMMANDLIST_H
#define SOLARSYSTEM_GFX_RHI_WGPU_WGPUCOMMANDLIST_H
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/PipelineTracker.h"
#include "gfx/rhi/RenderPassPlanner.h"
#include "gfx/rhi/RhiTypes.h"
#include "gfx/rhi/TimestampPairs.h"
#include "core/FrameProfile.h"

#include <webgpu/webgpu_cpp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

/**
 * The WebGPU backend of rhi::CommandList (docs/ARCHITECTURE.md §7.9). Built only with CMake
 * SOLARSYSTEM_WEBGPU=ON.
 *
 * GL-style calls go in; WebGPU objects come out:
 *  - PipelineState deltas fold into the state GL would hold (PipelineTracker), and each draw
 *    finds or builds the pipeline for (program, vertex layout, topology, target formats, that
 *    state) — so a pass ported from GL keeps relying on what the previous pass left;
 *  - targets, clears and viewports become render passes and load ops (RenderPassPlanner);
 *  - every draw's loose uniforms go into a per-frame ring with dynamic offsets, uploaded once
 *    before the frame is submitted;
 *  - each frame-graph pass's render passes are timestamped (timestamp-query) into its timer
 *    slot, read back a frame or more late, like GpuTimer's GL_TIME_ELAPSED queries.
 *
 * Everything renders y-flipped (ShaderSource's SpirvWebGpu vertex epilogue), so every target
 * holds GL's row order: GL's bottom-left viewport origin applies unchanged, gl_FragCoord and
 * render-target reads agree with GL, and ReadColor returns rows the way glReadPixels does.
 * FramebufferId 0 is an offscreen backbuffer for that reason; Present blits it, flipped, into
 * the surface's texture.
 *
 * Resources are this backend's own ids (ObjectId / FramebufferId), so BindGroup and
 * RenderTarget mean the same thing on both backends. The draw-level calls (SetProgram,
 * SetVertexBuffer, Draw) live here until the passes stop drawing through GL objects and they
 * move into rhi::CommandList (docs/plans/WEBGPU_BACKEND_PLAN.md, phase 2).
 */
namespace rhi::webgpu {

class Device;
class Program;

class CommandList final : public rhi::CommandList, private RenderPassPlanner::Sink {
public:
    explicit CommandList(Device& device);
    ~CommandList() override;
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;

    // --- Resources ------------------------------------------------------------------------
    enum class Filter : std::uint8_t { Linear, Nearest };
    /** A 2D texture (Rgba8, Rgba16F, Depth24, Depth32F) sampled with `filter`, clamped. */
    ObjectId CreateTexture(const TextureDesc& desc, Filter filter = Filter::Linear);
    /** Replace mip 0 with tightly packed rows, bottom row first (as glTexImage2D takes them). */
    void WriteTexture(ObjectId texture, const void* data, std::size_t bytesPerRow);
    ObjectId CreateUniformBuffer(std::size_t size);
    ObjectId CreateVertexBuffer(const void* data, std::size_t size);
    void WriteBuffer(ObjectId buffer, const void* data, std::size_t size);
    /** A render target from a colour texture and/or a depth texture (either may be 0). */
    FramebufferId CreateFramebuffer(ObjectId color, ObjectId depth);
    /** (Re)size FramebufferId 0: an RGBA8 colour texture with a Depth24 depth, like the GL window. */
    void ResizeBackbuffer(std::uint32_t width, std::uint32_t height);
    ObjectId BackbufferTexture() const { return _backbufferColor; }

    // --- Frame ----------------------------------------------------------------------------
    /** Start recording a frame (one command encoder; timestamp pairs reset). */
    void BeginFrame();
    /** Close any open pass, upload the uniform ring, resolve timestamps and submit. */
    void EndFrame();
    /** Blit the backbuffer into `target` (the surface texture), flipping rows to WebGPU's top-down order. */
    void Present(const wgpu::TextureView& target, wgpu::TextureFormat format);
    /** Present into one of this list's own textures (a golden capture, or a test's stand-in surface). */
    void PresentInto(ObjectId texture);

    // --- Draws ----------------------------------------------------------------------------
    void SetProgram(Program* program) { _program = program; }
    /** One interleaved vertex buffer for the following draws (0: none, as DrawFullscreenTriangle uses). */
    void SetVertexBuffer(ObjectId buffer, const VertexLayout& layout);
    void Draw(std::uint32_t vertexCount, PrimitiveTopology topology = PrimitiveTopology::Triangles,
              std::uint32_t instanceCount = 1);
    /** glClearColor / glClearDepth for the clears that follow. */
    void SetClearValue(const ClearValue& value) { _clearValue = value; }

    // --- rhi::CommandList -----------------------------------------------------------------
    void BeginPass(const PassInfo& pass) override;
    void EndPass() override;
    void SetRenderTarget(const RenderTarget& target) override;
    void BindFramebuffer(FramebufferId framebuffer) override;
    void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) override;
    void Clear(ClearFlags flags) override;
    void SetPipelineState(const PipelineState& state) override;
    void SetBindGroup(const BindGroup& group) override;
    /** The bound program's three vertices, no vertex buffer (screenFade.vs builds them from gl_VertexID). */
    void DrawFullscreenTriangle(ObjectId emptyVertexArray) override;

    // --- Readback and profiling --------------------------------------------------------------
    /**
     * Native: the colour texels of `framebuffer` in its format (RGBA8: 4 bytes, RGBA16F: 8),
     * rows bottom-up (glReadPixels order), after everything submitted so far. Empty on the web
     * (a page cannot block on the GPU) or for a target with no colour.
     */
    std::vector<std::uint8_t> ReadColor(FramebufferId framebuffer);
    /** Where CPU pass times go (either may be null), as GlCommandList::SetProfiling. */
    void SetProfiling(FrameProfile::PassTimings* passes, FrameProfile::CpuStages* stages);
    /** Whether a finished frame's timestamps have been read back yet. */
    bool HasGpuResult() const { return _gpuValid; }
    /** That frame's GPU milliseconds for timer slot `slot` (FrameProfile::kStageCount + pass timerSlot). */
    double GpuMs(std::size_t slot) const { return slot < _gpuMs.size() ? _gpuMs[slot] : 0.0; }

    struct Stats {
        std::uint64_t pipelinesCreated = 0;
        std::uint64_t renderPasses = 0;
        std::uint64_t draws = 0;
        std::uint64_t uniformBytes = 0; // this frame's ring use
    };
    const Stats& GetStats() const { return _stats; }
    const ResolvedPipelineState& CurrentState() const { return _tracker.Current(); }

private:
    struct Texture {
        wgpu::Texture texture;
        wgpu::TextureView view;
        wgpu::Sampler sampler;
        TextureDesc desc;
    };
    struct Framebuffer {
        ObjectId color = 0;
        ObjectId depth = 0;
    };
    struct UniformRing {
        wgpu::Buffer buffer;
        std::vector<std::uint8_t> staging;
        std::size_t used = 0;
    };
    struct Timestamps {
        wgpu::QuerySet querySet;
        wgpu::Buffer resolve;
        std::array<wgpu::Buffer, 2> readback;
        std::array<bool, 2> busy{false, false};
        std::array<TimestampPairs, 2> pairs{TimestampPairs(0), TimestampPairs(0)};
        std::size_t current = 0;
    };

    // RenderPassPlanner::Sink
    void BeginRenderPass(const RenderPassBegin& begin) override;
    void SetPassViewport(const Viewport& viewport) override;
    void EndRenderPass() override;

    const Texture* FindTexture(ObjectId id) const;
    wgpu::RenderPipeline PipelineFor(PrimitiveTopology topology);
    /** Copy the program's dirty uniform blocks into the ring; their offsets for the draw. */
    bool StageUniforms(std::array<std::uint32_t, 2>& offsets);
    bool BindForDraw();
    void ApplyViewport(const Viewport& viewport);
    void CollectTimestamps();
    wgpu::Sampler SamplerFor(Filter filter);

    Device* _device;
    wgpu::CommandEncoder _encoder;
    wgpu::RenderPassEncoder _pass;
    RenderPassPlanner _planner;
    PipelineTracker _tracker;
    ClearValue _clearValue;

    std::vector<Texture> _textures;         // ObjectId − 1
    std::vector<wgpu::Buffer> _buffers;     // ObjectId − 1
    std::vector<Framebuffer> _framebuffers; // FramebufferId − 1 (0 is the backbuffer)
    ObjectId _backbufferColor = 0;
    ObjectId _backbufferDepth = 0;
    std::array<wgpu::Sampler, 2> _samplers;

    Program* _program = nullptr;
    ObjectId _vertexBuffer = 0;
    VertexLayout _vertexLayout;
    std::vector<VertexLayout> _layouts; // id − 1 is the PipelineKey's vertexLayout
    std::uint32_t _vertexLayoutId = 0;
    std::array<ObjectId, 16> _unitTextures{};
    std::array<ObjectId, 4> _uniformBuffers{};

    std::unordered_map<PipelineKey, wgpu::RenderPipeline, PipelineKeyHash> _pipelines;
    UniformRing _ring;
    std::array<std::uint32_t, 2> _lastOffsets{};
    Program* _lastStaged = nullptr;

    Timestamps _timestamps;
    std::vector<double> _gpuMs;
    bool _gpuValid = false;

    FrameProfile::PassTimings* _passTimings = nullptr;
    FrameProfile::CpuStages* _cpuStages = nullptr;
    PassInfo _open;
    bool _inPass = false;
    FrameProfile::CpuStages::Clock::time_point _started{};

    // The present blit (a fixed WGSL program, not from the manifest).
    wgpu::ShaderModule _blitModule;
    wgpu::BindGroupLayout _blitLayout;
    std::unordered_map<std::uint32_t, wgpu::RenderPipeline> _blitPipelines; // by surface format
    Stats _stats;
};

} // namespace rhi::webgpu

#endif // SOLARSYSTEM_GFX_RHI_WGPU_WGPUCOMMANDLIST_H
