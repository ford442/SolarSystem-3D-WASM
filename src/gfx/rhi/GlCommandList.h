#ifndef SOLARSYSTEM_GFX_RHI_GLCOMMANDLIST_H
#define SOLARSYSTEM_GFX_RHI_GLCOMMANDLIST_H
#include "CommandList.h"
#include "RhiTypes.h"
#include "core/FrameProfile.h"
#include <cstdint>

class GpuTimer;

namespace rhi {

/**
 * The GL backend of CommandList: every call runs immediately on the current context (desktop
 * GL 4.6 natively, WebGL 2 on the web). PipelineState applies only the fields a pass set.
 *
 * BeginPass/EndPass are the per-pass profiling scope: the pass's GL_TIME_ELAPSED query
 * (GpuTimer slot FrameProfile::kStageCount + PassInfo::timerSlot), its CPU time into the
 * kind's PassTimings row and its stage's CpuStages total, and natively, with a KHR_debug
 * context, a debug group named after the pass so RenderDoc and apitrace show the graph.
 */
class GlCommandList final : public CommandList {
public:
    /** Where pass times go; any may be null (and the GPU timer may be disabled). */
    void SetProfiling(GpuTimer* gpu, FrameProfile::PassTimings* passes, FrameProfile::CpuStages* stages);
    /** Native only: push a KHR_debug group per pass (needs a debug context to be useful). */
    void SetDebugGroups(bool enabled) { _debugGroups = enabled; }

    void BeginPass(const PassInfo& pass) override;
    void EndPass() override;
    void SetRenderTarget(const RenderTarget& target) override;
    void BindFramebuffer(FramebufferId framebuffer) override;
    void SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) override;
    void Clear(ClearFlags flags) override;
    void SetPipelineState(const PipelineState& state) override;
    void SetBindGroup(const BindGroup& group) override;
    void DrawFullscreenTriangle(ObjectId emptyVertexArray) override;

private:
    GpuTimer* _gpu = nullptr;
    FrameProfile::PassTimings* _passes = nullptr;
    FrameProfile::CpuStages* _stages = nullptr;
    bool _debugGroups = false;
    PassInfo _open;
    bool _inPass = false;
    FrameProfile::CpuStages::Clock::time_point _started{};
};

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_GLCOMMANDLIST_H
