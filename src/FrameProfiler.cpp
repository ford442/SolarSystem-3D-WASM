#include "FrameProfiler.h"
#include "assets/UploadQueue.h"
#include "core/FrameProfile.h"
#include "core/JobSystem.h"
#include "core/Log.h"
#include "gfx/GpuTimer.h"
#include "gfx/Shader.h"
#include "render/graph/FramePlan.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

void FrameProfiler::BeginFrame(double nowSeconds) {
    if (!_gpuTimer && GpuTimer::Supported()) {
        _gpuTimer = std::make_unique<GpuTimer>(kGpuTimerSlots);
#ifndef __EMSCRIPTEN__
        // Natively the GPU column comes with debug-level [FrameStats] (or SOLARSYSTEM_GPU_TIMERS=1);
        // the web turns it on from the perf HUD (SetGpuTimersEnabled).
        const char* forced = std::getenv("SOLARSYSTEM_GPU_TIMERS");
        _gpuTimer->SetEnabled(forced ? std::string(forced) != "0" : Log::Enabled(Log::Level::Debug));
#endif
    }
    _sample.frameMs = _frameStart < 0.0 ? 0.0 : (nowSeconds - _frameStart) * 1000.0;
    _frameStart = nowSeconds;
    _cpuStages.Reset();
    _passTimings.Reset();
    if (_gpuTimer) {
        _gpuTimer->BeginFrame();
    }
}

void FrameProfiler::EndFrame() {
    if (_gpuTimer) {
        _gpuTimer->EndFrame();
    }
    FrameProfile::Sample& sample = _sample;
    sample.cpuMs = _cpuStages.Ms();
    sample.gpuSupported = GpuTimer::Supported();
    sample.gpuValid = _gpuTimer && _gpuTimer->Enabled() && _gpuTimer->HasResult();
    for (std::size_t i = 0; i < FrameProfile::kStageCount; ++i) {
        sample.gpuMs[i] = sample.gpuValid ? _gpuTimer->Ms(i) : 0.0;
    }
    // A stage's GPU time is its own bracket (Update) plus every pass billed to it.
    sample.passes.resize(_passTimings.Count());
    for (std::size_t slot = 0; slot < _passTimings.Count(); ++slot) {
        FrameProfile::PassSample& pass = sample.passes[slot];
        pass.kind = _passTimings.Kind(slot);
        pass.stage = _passTimings.StageOf(slot);
        pass.count = _passTimings.Runs(slot);
        pass.cpuMs = _passTimings.CpuMs(slot);
        pass.gpuMs = sample.gpuValid ? _gpuTimer->Ms(FrameProfile::kStageCount + slot) : 0.0;
        sample.gpuMs[static_cast<std::size_t>(pass.stage)] += pass.gpuMs;
    }
    sample.jobWorkers = Jobs::WorkerCount();
    sample.jobQueueDepth = Jobs::QueueDepth();
    sample.jobsInFlight = Jobs::InFlight();
    sample.jobsCompleted = Jobs::CompletedCount();
    sample.uploadBytes = UploadQueue::Get().LastPumpBytes();
    sample.uploadsPending = UploadQueue::Get().Pending();
    if (sample.frameMs > 0.0) {
        _totals.Add(sample);
    }
}

std::string FrameProfiler::Json() {
    std::string json = FrameProfile::ToJson(_sample, _totals);
    _totals.ResetWorst();
    return json;
}

void FrameProfiler::SetGpuTimersEnabled(bool enabled) {
    if (!_gpuTimer && enabled && GpuTimer::Supported()) {
        _gpuTimer = std::make_unique<GpuTimer>(kGpuTimerSlots);
    }
    if (_gpuTimer) {
        _gpuTimer->SetEnabled(enabled);
    }
}

void FrameProfiler::Report(const FramePlan::Stats& stats) {
    // At debug level (SOLARSYSTEM_LOG=debug / ?log=debug), log the previous frame's pass
    // counts and profile every N frames: SOLARSYSTEM_FRAME_STATS=N, 120 by default.
    static const int every = [] {
        const char* value = std::getenv("SOLARSYSTEM_FRAME_STATS");
        return value ? std::max(0, std::atoi(value)) : 120;
    }();
    static unsigned long long frame = 0;
    const unsigned long long uniformCalls = Shader::UniformCallCount();
    Shader::ResetUniformCallCount();
    _sample.uniformCalls = uniformCalls;
    if (every <= 0 || frame++ % static_cast<unsigned long long>(every) != 0) {
        return;
    }
    LOG_DEBUG("FrameStats",
              "updates=%d viewBinds=%d shadowPasses=%d componentDraws=%d bodyDraws=%d culled=%d uniformCalls=%llu "
              "graphPasses=%d graphCulled=%d",
              stats.sceneUpdates, stats.viewBinds, stats.shadowPasses, stats.componentDraws, stats.bodyDraws,
              stats.culledComponents, uniformCalls, stats.passes, stats.culledPasses);
    if (!Log::Enabled(Log::Level::Debug)) {
        return;
    }
    const FrameProfile::Sample& p = _sample;
    std::string cpu;
    std::string gpu;
    for (std::size_t i = 0; i < FrameProfile::kStageCount; ++i) {
        const char* name = FrameProfile::StageName(static_cast<FrameProfile::Stage>(i));
        char part[48];
        std::snprintf(part, sizeof(part), "%s%s=%.2f", i ? " " : "", name, p.cpuMs[i]);
        cpu += part;
        std::snprintf(part, sizeof(part), "%s%s=%.2f", i ? " " : "", name, p.gpuMs[i]);
        gpu += part;
    }
    LOG_DEBUG("FrameStats",
              "frame=%.2fms cpu[%s] gpu[%s] jobs workers=%d queued=%zu inFlight=%zu "
              "uploads bytes=%zu pending=%zu slowFrames=%llu/%llu",
              p.frameMs, cpu.c_str(), p.gpuValid ? gpu.c_str() : "n/a", p.jobWorkers, p.jobQueueDepth, p.jobsInFlight,
              p.uploadBytes, p.uploadsPending, _totals.slowFrames, _totals.frames);
}
