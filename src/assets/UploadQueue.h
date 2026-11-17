#ifndef SOLARSYSTEM_ASSETS_UPLOADQUEUE_H
#define SOLARSYSTEM_ASSETS_UPLOADQUEUE_H
#include <cstddef>
#include <deque>
#include <functional>
#include <string>

/**
 * Main-thread GL work fed by jobs, paced by a per-frame byte budget.
 *
 * A job (core/JobSystem.h) decodes a texture or parses a model into a CPU payload; the
 * upload of that payload is a Task here. Application::PumpUploads runs Pump(budget) once a
 * frame: tasks whose job has finished (`ready`) take steps, oldest first, until the frame's
 * bytes are spent. A task may take several frames (a big texture uploads in row bands), and
 * the first step of a frame always runs even if it alone is over budget, so one item larger
 * than the budget still lands instead of starving.
 *
 * GL-free (the steps carry the GL), so the scheduling is unit tested.
 */
class UploadQueue {
public:
    struct Step {
        std::size_t bytes = 0; // bytes this step put on the GPU
        bool finished = true;  // false: call again next time there is budget
    };
    /** Upload up to `budget` bytes (a step may overshoot once, never by more than one band). */
    using Task = std::function<Step(std::size_t budget)>;

    static UploadQueue& Get();

    void Push(std::string label, std::function<bool()> ready, Task task);

    struct PumpResult {
        std::size_t bytes = 0;
        int steps = 0;
        int finished = 0;
    };
    PumpResult Pump(std::size_t budgetBytes);

    /** Tasks not finished yet (waiting on their job, or part way through an upload). */
    std::size_t Pending() const { return _tasks.size(); }
    /** Bytes the last Pump() uploaded (the perf HUD's "upload" lane). */
    std::size_t LastPumpBytes() const { return _lastPumpBytes; }
    /** Drop everything without running it (teardown: the targets are going away). */
    void Clear();

private:
    struct Entry {
        std::string label;
        std::function<bool()> ready;
        Task task;
    };
    std::deque<Entry> _tasks;
    std::size_t _lastPumpBytes = 0;
    std::size_t _clears = 0;
};

#endif // SOLARSYSTEM_ASSETS_UPLOADQUEUE_H
