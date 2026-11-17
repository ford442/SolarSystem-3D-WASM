#ifndef SOLARSYSTEM_CORE_FRAMEPROFILE_H
#define SOLARSYSTEM_CORE_FRAMEPROFILE_H
#include <array>
#include <chrono>
#include <cstddef>
#include <string>

/**
 * Where one frame's time went, GL-free: CPU time per FramePasses stage (plus the HUD and the
 * upload pump), the GPU time of the same stages from gfx/GpuTimer (a couple of frames
 * late, since a query is read back only once the GPU has finished it), job-pool and upload
 * counters. Application fills one per frame; the [FrameStats] line logs it and
 * GetFrameStatsJson (?perf=1's HUD, web/src/perfHud.ts) reads it.
 */
namespace FrameProfile {

enum class Stage : int {
    Update,       // simulate, snapshot, terrain selection, cull
    Background,   // sky, orbit lines, flat corona
    Shadow,       // per-component shadow maps
    Scene,        // per-component colour passes
    Foreground,   // corona, belts, field lines, lens flare, HDR resolve
    Hud,          // text overlay and loading bars
    Uploads,      // texture queue and the budgeted upload pump
    SpaceWeather, // solar-wind particle advection (transform feedback); its draw is in Foreground
    Count,
};
constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::Count);

const char* StageName(Stage stage);

/** A frame slower than this counts as slow (the 30 fps budget the medium tier aims for). */
constexpr double kSlowFrameMs = 33.4;

struct Sample {
    double frameMs = 0.0; // wall time since the previous frame started
    std::array<double, kStageCount> cpuMs{};
    bool gpuSupported = false; // a timer query is available on this context
    bool gpuValid = false;     // gpuMs holds a finished frame's results
    std::array<double, kStageCount> gpuMs{};
    int jobWorkers = 0;
    std::size_t jobQueueDepth = 0;
    std::size_t jobsInFlight = 0;
    unsigned long long jobsCompleted = 0;
    std::size_t uploadBytes = 0;    // bytes the pump uploaded this frame
    std::size_t uploadsPending = 0; // upload tasks still queued
    unsigned long long uniformCalls = 0;
};

/** Running totals across frames, for the HUD's "worst frame" and the slow-frame count. */
struct Totals {
    unsigned long long frames = 0;
    unsigned long long slowFrames = 0; // frameMs > kSlowFrameMs
    double worstFrameMs = 0.0;         // since the last ResetWorst (each JSON read)
    void Add(const Sample& sample);
    void ResetWorst() { worstFrameMs = 0.0; }
};

/** {"frameMs":…,"cpu":{"update":…},"gpu":{…}|null,"jobs":{…},"uploads":{…},…} */
std::string ToJson(const Sample& sample, const Totals& totals);

/** Sums CPU time per stage over one frame (a stage may run many times, e.g. per component). */
class CpuStages {
public:
    using Clock = std::chrono::steady_clock;
    void Reset() { _ms.fill(0.0); }
    void Add(Stage stage, Clock::duration elapsed) {
        _ms[static_cast<std::size_t>(stage)] += std::chrono::duration<double, std::milli>(elapsed).count();
    }
    const std::array<double, kStageCount>& Ms() const { return _ms; }

private:
    std::array<double, kStageCount> _ms{};
};

} // namespace FrameProfile

#endif // SOLARSYSTEM_CORE_FRAMEPROFILE_H
