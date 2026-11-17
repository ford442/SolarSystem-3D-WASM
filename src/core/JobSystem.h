#ifndef SOLARSYSTEM_CORE_JOBSYSTEM_H
#define SOLARSYSTEM_CORE_JOBSYSTEM_H
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

/**
 * A small fixed thread pool for the CPU work that used to stall the main thread between
 * frames: texture parse/decode, model parse, catalogue JSON, field-line tracing (ARCHITECTURE
 * §9.3).
 *
 * The one rule: **no GL call inside a job.** A job produces a CPU-side payload
 * (TexturePayload, MeshPayload, a parsed catalogue) and the main thread consumes it, usually
 * through assets/UploadQueue's per-frame byte budget. scripts/check-gl-handles.sh rejects a gl*
 * call written inside a Jobs::Submit(...) argument, and GlHandle's Create asserts it runs on
 * the main thread in debug builds.
 *
 * Pool size: hardware_concurrency() - 1 workers (the main thread is the last core, and it
 * runs queued jobs itself while it waits in Drain()/Future::Get()). SOLARSYSTEM_JOBS=0
 * (native) or `?jobs=0` (web) runs every job inline in Submit, which is also what a web build
 * without pthreads, or a page without SharedArrayBuffer, always does — same code path, same
 * results, just serial. SOLARSYSTEM_JOBS=N pins N workers.
 *
 * FIFO, no work stealing, no priorities: the jobs are coarse (one texture, one model), so
 * one locked deque is not the bottleneck.
 */
namespace Jobs {

/** Start the pool. `workers` < 0 picks from SOLARSYSTEM_JOBS, else hardware_concurrency()-1. */
void Start(int workers = -1);
/** Drain and join. Safe to call twice; Submit after Stop runs inline. */
void Stop();

/** Workers running (0 = every job runs inline in Submit). */
int WorkerCount();
bool Threaded();
bool IsMainThread();

/** Jobs submitted but not yet picked up. */
std::size_t QueueDepth();
/** Jobs submitted and not yet finished (queued + running). */
std::size_t InFlight();
/** Jobs finished since start (for the [FrameStats] line and the perf HUD). */
std::size_t CompletedCount();

/** Block until every submitted job has finished. The calling thread runs queued jobs meanwhile. */
void Drain();

namespace detail {
void Enqueue(std::function<void()> job);
/** Run one queued job on this thread; false if the queue was empty. */
bool RunOne();
/** Block until `ready()` holds, running queued jobs on this thread while waiting. */
void WaitUntil(const std::function<bool()>& ready);
} // namespace detail

/**
 * The result of a job. Move-only; Get() blocks (helping the pool meanwhile) and rethrows
 * whatever the job threw, so a bad file surfaces as the same std::runtime_error it did when
 * the work ran inline.
 */
template <class T>
class Future {
public:
    Future() = default;
    explicit Future(std::future<T> future) : _future(std::move(future)) {}

    bool Valid() const { return _future.valid(); }
    bool Ready() const {
        return _future.valid() && _future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    void Wait() const {
        if (!_future.valid()) {
            return;
        }
        detail::WaitUntil([this] { return Ready(); });
    }
    T Get() {
        Wait();
        return _future.get();
    }

private:
    std::future<T> _future;
};

/**
 * Queue `work` (any callable) and return its Future. With no workers it runs right here, so
 * the Future is already Ready when Submit returns.
 */
template <class F>
auto Submit(F&& work) -> Future<std::invoke_result_t<std::decay_t<F>>> {
    using Result = std::invoke_result_t<std::decay_t<F>>;
    auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(work));
    Future<Result> future(task->get_future());
    detail::Enqueue([task] { (*task)(); });
    return future;
}

} // namespace Jobs

#endif // SOLARSYSTEM_CORE_JOBSYSTEM_H
