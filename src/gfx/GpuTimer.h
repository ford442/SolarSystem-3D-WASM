#ifndef SOLARSYSTEM_GFX_GPUTIMER_H
#define SOLARSYSTEM_GFX_GPUTIMER_H
#include "GlHandle.h"
#include <array>
#include <cstddef>
#include <vector>

/**
 * GPU time per frame stage from GL_TIME_ELAPSED queries: core GL natively,
 * EXT_disjoint_timer_query_webgl2 on the web (GlCapabilities::gpuTimerQuery enables it).
 *
 * A stage may be bracketed many times a frame (the shadow and colour passes alternate per
 * component), so each bracket takes its own query and the stage's time is their sum. Time
 * queries cannot nest: a Begin while another stage is open is ignored, and that stage's
 * time stays with the open one.
 *
 * Double-buffered: frame N records into one query set while the other (frame N-1's) is read
 * back. WebGL never makes a result available in the frame that issued it, and a native
 * driver that has not finished yet just costs that frame its sample: a set is never read
 * while its results are pending, and is not reused until they have been read. A disjoint
 * event (GPU reset, clock change) discards the frames it overlapped.
 */
class GpuTimer {
public:
    explicit GpuTimer(std::size_t stageCount);

    /** Whether this context has a time-elapsed query at all (probed once). */
    static bool Supported();
    /** Off by default: queries cost a little and most frames nobody reads them. */
    void SetEnabled(bool enabled);
    bool Enabled() const { return _enabled; }

    void BeginFrame();
    void Begin(std::size_t stage);
    void End(std::size_t stage);
    void EndFrame();

    /** True once a whole frame's queries have been read back. */
    bool HasResult() const { return _hasResult; }
    /** That frame's GPU milliseconds for `stage` (0 if it was never bracketed). */
    double Ms(std::size_t stage) const { return stage < _resultMs.size() ? _resultMs[stage] : 0.0; }

private:
    struct Slot {
        std::vector<std::vector<GlQuery>> pool; // per stage, grown on demand
        std::vector<std::size_t> used;          // queries issued this frame, per stage
        bool recorded = false;                  // holds results not yet read back
    };

    bool Harvest(Slot& slot);

    std::array<Slot, 2> _slots;
    std::size_t _stageCount;
    std::size_t _current = 0;
    bool _enabled = false;
    bool _recording = false;
    std::ptrdiff_t _open = -1; // stage with a query in progress
    bool _hasResult = false;
    std::vector<double> _resultMs;
};

#endif // SOLARSYSTEM_GFX_GPUTIMER_H
