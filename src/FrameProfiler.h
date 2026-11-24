#ifndef SOLARSYSTEM_FRAMEPROFILER_H
#define SOLARSYSTEM_FRAMEPROFILER_H
#include "core/FrameProfile.h"
#include "gfx/GpuTimer.h"
#include "render/graph/FramePlan.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

/**
 * Where a frame's time went (docs/ARCHITECTURE.md §7.5), owned by Application: CPU per
 * FrameProfile stage and per frame-graph pass kind, the GL_TIME_ELAPSED queries behind the GPU
 * column, and the sample the perf HUD (GetFrameStatsJson) and the debug-level [FrameStats]
 * line read. The frame graph times its own passes through rhi::GlCommandList (Renderer::
 * SetProfiling with Gpu(), Passes() and Stages()); Time() brackets the work outside the graph.
 */
class FrameProfiler {
public:
    /** GPU timer slots: one per stage, then one per pass kind (FrameProfile::PassTimings). */
    static constexpr std::size_t kGpuTimerSlots = FrameProfile::kStageCount + FrameProfile::kMaxPassKinds;

    /** Frame start: the frame time since the last, counters reset, the GPU timer opened. */
    void BeginFrame(double nowSeconds);
    /** Frame end: this frame's sample (its GPU columns a frame or two late) into the totals. */
    void EndFrame();
    /** Run `work` as `stage`: CPU time into Stages(), GPU time into the stage's timer slot. */
    template <class F>
    void Time(FrameProfile::Stage stage, F&& work) {
        const auto index = static_cast<std::size_t>(stage);
        if (_gpuTimer) {
            _gpuTimer->Begin(index);
        }
        const auto started = FrameProfile::CpuStages::Clock::now();
        std::forward<F>(work)();
        _cpuStages.Add(stage, FrameProfile::CpuStages::Clock::now() - started);
        if (_gpuTimer) {
            _gpuTimer->End(index);
        }
    }
    /**
     * Once a frame, before the scene updates: count the previous frame's glUniform* calls and,
     * at debug level every SOLARSYSTEM_FRAME_STATS=N frames (120), log its pass counts and profile.
     */
    void Report(const FramePlan::Stats& stats);
    /** The last frame's sample as JSON (FrameProfile::ToJson); resets the worst-frame window. */
    std::string Json();
    /** GPU stage timers: on for the perf HUD and debug-level FrameStats. */
    void SetGpuTimersEnabled(bool enabled);
    bool GpuTimersEnabled() const { return _gpuTimer && _gpuTimer->Enabled(); }
    /** Drop the GPU timer (its queries name objects of a context that is going or gone). */
    void ReleaseGpu() { _gpuTimer.reset(); }

#ifdef SOLARSYSTEM_WEBGPU
    /** Where the sample's WebGPU status comes from (gfx/rhi/wgpu/WgpuBringUp::StatusJson). */
    void SetWebGpuStatusSource(std::function<std::string()> source) { _webgpuStatus = std::move(source); }
#endif

    GpuTimer* Gpu() { return _gpuTimer.get(); }
    FrameProfile::PassTimings& Passes() { return _passTimings; }
    FrameProfile::CpuStages& Stages() { return _cpuStages; }

private:
    FrameProfile::CpuStages _cpuStages;
    FrameProfile::PassTimings _passTimings;
#ifdef SOLARSYSTEM_WEBGPU
    std::function<std::string()> _webgpuStatus;
#endif
    FrameProfile::Sample _sample;
    FrameProfile::Totals _totals;
    std::unique_ptr<GpuTimer> _gpuTimer;
    double _frameStart = -1.0;
};

#endif // SOLARSYSTEM_FRAMEPROFILER_H
