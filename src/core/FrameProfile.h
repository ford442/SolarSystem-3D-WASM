#ifndef SOLARSYSTEM_CORE_FRAMEPROFILE_H
#define SOLARSYSTEM_CORE_FRAMEPROFILE_H
#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

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

/** Pass kinds the timers keep apart ("scene", "shadow", "aurora-bloom", …); more share the last. */
constexpr std::size_t kMaxPassKinds = 48;

/** One pass kind's share of a frame: every frame-graph pass of that kind summed. */
struct PassSample {
    std::string kind;
    Stage stage = Stage::Update;
    int count = 0; // passes of this kind that ran (per component, per view)
    double cpuMs = 0.0;
    double gpuMs = 0.0; // 0 unless Sample::gpuValid
};

struct Sample {
    /** The GPU API the frame was recorded against: rhi::BackendId / BackendName ("webgl2", "WebGL 2"). */
    const char* backend = "opengl";
    const char* backendName = "OpenGL 4.6";
    /**
     * The "webgpu" member as a JSON object: the device beside the GL frame and how its WGSL
     * modules compiled (gfx/rhi/wgpu/WgpuBringUp::StatusJson). Empty is null, which is all a
     * build without the WebGPU backend ever writes, so it carries no code for it.
     */
    std::string webgpuJson;
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
    std::vector<PassSample> passes; // per frame-graph pass kind, in first-registered order
};

/** Running totals across frames, for the HUD's "worst frame" and the slow-frame count. */
struct Totals {
    unsigned long long frames = 0;
    unsigned long long slowFrames = 0; // frameMs > kSlowFrameMs
    double worstFrameMs = 0.0;         // since the last ResetWorst (each JSON read)
    void Add(const Sample& sample);
    void ResetWorst() { worstFrameMs = 0.0; }
};

/** {"backend":…,"backendName":…,"frameMs":…,"cpu":{"update":…},"gpu":{…}|null,"jobs":{…},"uploads":{…},"passes":[…],…} */
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

/**
 * Timer slots by pass kind for the frame graph (render/graph/FramePlan): a kind keeps the slot
 * it was first given, so the GPU timer's query pools and the HUD's rows stay put across frames.
 * Slot s is GpuTimer stage kStageCount + s; the stages themselves keep slots 0…kStageCount-1.
 */
class PassTimings {
public:
    using Clock = CpuStages::Clock;
    /** `kind`'s slot (registered on first use and billed to `stage`); past kMaxPassKinds, the last slot. */
    int SlotFor(const std::string& kind, Stage stage);
    /** Start a frame: CPU times and run counts back to zero (the slots stay). */
    void Reset();
    void Add(int slot, Clock::duration elapsed);
    std::size_t Count() const { return _kinds.size(); }
    const std::string& Kind(std::size_t slot) const { return _kinds[slot]; }
    Stage StageOf(std::size_t slot) const { return _stages[slot]; }
    double CpuMs(std::size_t slot) const { return _cpuMs[slot]; }
    int Runs(std::size_t slot) const { return _runs[slot]; }

private:
    std::vector<std::string> _kinds;
    std::vector<Stage> _stages;
    std::vector<double> _cpuMs;
    std::vector<int> _runs;
};

} // namespace FrameProfile

#endif // SOLARSYSTEM_CORE_FRAMEPROFILE_H
