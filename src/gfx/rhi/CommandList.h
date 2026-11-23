#ifndef SOLARSYSTEM_GFX_RHI_COMMANDLIST_H
#define SOLARSYSTEM_GFX_RHI_COMMANDLIST_H
#include "RhiTypes.h"
#include <cstdint>

/**
 * What a frame-graph pass records its GPU work against (docs/ARCHITECTURE.md §7.5). GL-free:
 * GlCommandList executes each call immediately on the current context; a test records them
 * (tests/test_frame_graph.cpp), and a WebGPU backend would encode them into a command buffer.
 *
 * Scene objects still draw themselves through their gfx/ objects (SpaceObject::Draw), so a
 * pass sets its target, state and bindings here and then issues those draws. BeginPass/EndPass
 * are called by FrameGraph::Graph::Execute around every pass, never by the pass itself: that is
 * where the per-pass GPU timer scope and debug group go.
 */
namespace rhi {

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual void BeginPass(const PassInfo& pass) = 0;
    virtual void EndPass() = 0;

    /** Bind `target.framebuffer` and set the viewport to its rectangle. */
    virtual void SetRenderTarget(const RenderTarget& target) = 0;
    /** Bind a framebuffer and leave the viewport alone (a pass that returns to the window's). */
    virtual void BindFramebuffer(FramebufferId framebuffer) = 0;
    virtual void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) = 0;
    virtual void Clear(ClearFlags flags) = 0;
    virtual void SetPipelineState(const PipelineState& state) = 0;
    virtual void SetBindGroup(const BindGroup& group) = 0;
    /** One triangle covering the viewport, positions from gl_VertexID (screenFade.vs). */
    virtual void DrawFullscreenTriangle(ObjectId emptyVertexArray) = 0;

    /** The framebuffer the frame composites into (FBO 0, or the XRWebGLLayer's under WebXR). */
    FramebufferId DefaultFramebuffer() const { return _defaultFramebuffer; }
    void SetDefaultFramebuffer(FramebufferId framebuffer) { _defaultFramebuffer = framebuffer; }

private:
    FramebufferId _defaultFramebuffer = 0;
};

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_COMMANDLIST_H
