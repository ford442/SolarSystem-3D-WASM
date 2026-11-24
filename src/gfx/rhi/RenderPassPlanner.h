#ifndef SOLARSYSTEM_GFX_RHI_RENDERPASSPLANNER_H
#define SOLARSYSTEM_GFX_RHI_RENDERPASSPLANNER_H
#include "RhiTypes.h"
#include <array>
#include <cstdint>

/**
 * GL's "bind, clear, draw whenever" turned into WebGPU render passes (docs/ARCHITECTURE.md
 * §7.9). GL-free and unit-tested (tests/test_rhi_webgpu_model.cpp).
 *
 * In GL a clear is a command like any other: it can come before, between or after draws, and
 * the viewport and the bound framebuffer are context state that outlive any pass. In WebGPU a
 * clear is a render pass's load op, chosen when the pass begins, and the viewport belongs to
 * the pass encoder. The planner takes the CommandList calls in GL order and decides where
 * WebGPU render passes begin and end so the attachments end up holding the same pixels:
 *
 *  - a render pass opens lazily, at the first draw after a target change or a clear;
 *  - clears before that draw become its load ops (merged: Color then Depth is one pass);
 *  - a clear after draws ends the open pass, and the next one opens with that clear;
 *  - a clear that no draw follows (a target change or the end of the graph pass comes first)
 *    is still executed, as an empty pass that only clears;
 *  - as in glClear, the depth part of a clear does nothing while depth writes are off;
 *  - the viewport is tracked like GL context state, across targets and passes, and every pass
 *    opens with the current one;
 *  - frame-graph passes never share a render pass, so each one's GPU time is its own
 *    (WebGPU only timestamps render pass boundaries).
 */
namespace rhi {

struct Viewport {
    std::int32_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Viewport& other) const {
        return x == other.x && y == other.y && width == other.width && height == other.height;
    }
    bool operator!=(const Viewport& other) const { return !(*this == other); }
};

/** What a cleared attachment is cleared to (glClearColor / glClearDepth). */
struct ClearValue {
    std::array<float, 4> color{0.0f, 0.0f, 0.0f, 1.0f};
    float depth = 1.0f;
};

enum class LoadOp : std::uint8_t { Load, Clear };

struct RenderPassBegin {
    FramebufferId framebuffer = 0;
    LoadOp colorLoad = LoadOp::Load;
    LoadOp depthLoad = LoadOp::Load;
    ClearValue clear;
    Viewport viewport;
    /** The frame-graph pass's timer slot (PassInfo::timerSlot), -1 untimed. */
    int timerSlot = -1;
};

class RenderPassPlanner {
public:
    /** Where the planner's decisions go: the WebGPU command list, or a test's recorder. */
    class Sink {
    public:
        virtual ~Sink() = default;
        virtual void BeginRenderPass(const RenderPassBegin& begin) = 0;
        virtual void SetPassViewport(const Viewport& viewport) = 0;
        virtual void EndRenderPass() = 0;
    };

    explicit RenderPassPlanner(Sink& sink) : _sink(&sink) {}

    /** A frame-graph pass starts (CommandList::BeginPass): it never shares a render pass with the last one. */
    void BeginGraphPass(int timerSlot);
    /** It ends: run any clear still pending and close the render pass. */
    void EndGraphPass();

    /** glBindFramebuffer: a different target ends the open pass (and runs a pending clear first). */
    void BindFramebuffer(FramebufferId framebuffer);
    /** glViewport: encoded into the open pass, or remembered for the next one. */
    void SetViewport(const Viewport& viewport);
    /**
     * glClear on the bound target. `depthWritable` is glDepthMask: off, the depth half of the
     * clear does nothing, as in GL. `value` is the clear colour and depth in force.
     */
    void Clear(ClearFlags flags, const ClearValue& value, bool depthWritable = true);
    /** Before a draw: opens a render pass on the bound target if none is open. */
    void BeforeDraw();

    bool InRenderPass() const { return _open; }
    FramebufferId Framebuffer() const { return _framebuffer; }
    const Viewport& CurrentViewport() const { return _viewport; }
    /** Render passes opened since construction (the empty clear-only ones included). */
    std::uint64_t RenderPassCount() const { return _renderPasses; }

private:
    void Open();
    void Close();
    /** A clear nothing has drawn after yet: run it as a pass of its own. */
    void FlushPendingClear();

    Sink* _sink;
    FramebufferId _framebuffer = 0;
    Viewport _viewport;
    bool _open = false;
    bool _pendingColor = false;
    bool _pendingDepth = false;
    ClearValue _pendingValue;
    int _timerSlot = -1;
    std::uint64_t _renderPasses = 0;
};

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_RENDERPASSPLANNER_H
