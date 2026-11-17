#include "JobSystem.h"
#include "Log.h"
#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// A web build without -pthread has no threads to start (std::thread would fail at run time),
// so the pool compiles down to the inline path there.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define SOLARSYSTEM_JOBS_HAVE_THREADS 0
#else
#define SOLARSYSTEM_JOBS_HAVE_THREADS 1
#endif

namespace Jobs {
namespace {

struct Pool {
    std::mutex mutex;
    std::condition_variable workAvailable; // workers wait here
    std::condition_variable jobFinished;   // Drain / Future::Get wait here
    std::deque<std::function<void()>> queue;
    std::vector<std::thread> workers;
    std::size_t inFlight = 0;
    std::size_t completed = 0;
    bool stopping = false;

    Pool() = default;
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;
    // A path that never reaches Jobs::Stop (main() returning after a throwing constructor)
    // must still join: destroying a joinable std::thread calls std::terminate.
    ~Pool() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        workAvailable.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

Pool& GetPool() {
    static Pool pool;
    return pool;
}

const std::thread::id& MainThreadId() {
    // The first thread to touch the job system is the main thread: Application::InitSystems
    // starts the pool before anything submits.
    static const std::thread::id id = std::this_thread::get_id();
    return id;
}

void RunJob(Pool& pool, std::unique_lock<std::mutex>& lock) {
    std::function<void()> job = std::move(pool.queue.front());
    pool.queue.pop_front();
    lock.unlock();
    // packaged_task stores a thrown exception in its future, so nothing escapes here.
    job();
    lock.lock();
    --pool.inFlight;
    ++pool.completed;
    pool.jobFinished.notify_all();
}

void WorkerLoop(Pool& pool) {
    std::unique_lock<std::mutex> lock(pool.mutex);
    for (;;) {
        pool.workAvailable.wait(lock, [&] { return pool.stopping || !pool.queue.empty(); });
        if (pool.queue.empty()) {
            return; // stopping and drained
        }
        RunJob(pool, lock);
    }
}

#ifdef __EMSCRIPTEN__
// EM_JS bodies are JavaScript, and clang-format would split `===`.
// clang-format off
// `?jobs=N` on the page (-1 when absent); same shape as Log's `?log=` helper.
EM_JS(int, JobsQueryParameter, (), {
    try {
        if (typeof location === 'undefined') {
            return -1;
        }
        const value = new URLSearchParams(location.search).get('jobs');
        if (!value) {
            return -1;
        }
        const parsed = parseInt(value, 10);
        return isNaN(parsed) ? -1 : parsed;
    } catch (e) {
        return -1;
    }
});

// A threaded build loaded into a page that is not cross-origin isolated (no COEP, or an
// embedding iframe) can create its shared memory but cannot hand it to a Worker, so a
// thread would never start. Run inline there instead of hanging.
EM_JS(int, PageCanStartWorkers, (), {
    return (typeof crossOriginIsolated !== 'undefined' && crossOriginIsolated) ? 1 : 0;
});
// clang-format on
#endif

int RequestedWorkers(int requested) {
    if (requested >= 0) {
        return requested;
    }
#ifdef __EMSCRIPTEN__
    const int fromQuery = JobsQueryParameter();
    if (fromQuery >= 0) {
        return fromQuery;
    }
    if (!PageCanStartWorkers()) {
        return 0;
    }
#else
    if (const char* value = std::getenv("SOLARSYSTEM_JOBS")) {
        return std::max(0, std::atoi(value));
    }
#endif
    const unsigned cores = std::thread::hardware_concurrency();
    return cores > 1 ? static_cast<int>(cores) - 1 : 0;
}

} // namespace

void Start(int workers) {
    (void)MainThreadId();
    Pool& pool = GetPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    if (!pool.workers.empty()) {
        return;
    }
    pool.stopping = false;
    int count = RequestedWorkers(workers);
#if !SOLARSYSTEM_JOBS_HAVE_THREADS
    count = 0;
#elif defined(__EMSCRIPTEN__)
    // Every worker beyond PTHREAD_POOL_SIZE is spun up lazily, which on the main browser
    // thread means waiting for an event-loop turn that never comes while we block. The
    // link sets the pool from navigator.hardwareConcurrency, so cap at that minus one.
    count = std::min(count, std::max(0, static_cast<int>(std::thread::hardware_concurrency()) - 1));
#endif
    count = std::min(count, 64);
    for (int i = 0; i < count; ++i) {
        try {
            pool.workers.emplace_back([&pool] { WorkerLoop(pool); });
        } catch (const std::system_error& error) {
            LOG_WARN("Jobs", "could only start %d of %d workers (%s)", i, count, error.what());
            break;
        }
    }
    LOG_INFO("Jobs", "%zu worker thread(s)%s", pool.workers.size(),
             pool.workers.empty() ? " — jobs run inline on the main thread" : "");
}

void Stop() {
    Pool& pool = GetPool();
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        pool.stopping = true;
        workers.swap(pool.workers);
    }
    pool.workAvailable.notify_all();
    for (std::thread& worker : workers) {
        worker.join();
    }
    // Anything still queued (submitted after the workers saw `stopping`) runs here.
    Drain();
}

int WorkerCount() {
    Pool& pool = GetPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    return static_cast<int>(pool.workers.size());
}

bool Threaded() {
    return WorkerCount() > 0;
}

bool IsMainThread() {
    return std::this_thread::get_id() == MainThreadId();
}

std::size_t QueueDepth() {
    Pool& pool = GetPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    return pool.queue.size();
}

std::size_t InFlight() {
    Pool& pool = GetPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    return pool.inFlight;
}

std::size_t CompletedCount() {
    Pool& pool = GetPool();
    std::lock_guard<std::mutex> lock(pool.mutex);
    return pool.completed;
}

void Drain() {
    Pool& pool = GetPool();
    detail::WaitUntil([&pool] { return pool.inFlight == 0; });
}

namespace detail {

void Enqueue(std::function<void()> job) {
    Pool& pool = GetPool();
    std::unique_lock<std::mutex> lock(pool.mutex);
    pool.queue.push_back(std::move(job));
    ++pool.inFlight;
    if (pool.workers.empty() || pool.stopping) {
        // Inline: run it now, on the caller, before Submit returns.
        RunJob(pool, lock);
        return;
    }
    lock.unlock();
    pool.workAvailable.notify_one();
}

bool RunOne() {
    Pool& pool = GetPool();
    std::unique_lock<std::mutex> lock(pool.mutex);
    if (pool.queue.empty()) {
        return false;
    }
    RunJob(pool, lock);
    return true;
}

void WaitUntil(const std::function<bool()>& ready) {
    Pool& pool = GetPool();
    std::unique_lock<std::mutex> lock(pool.mutex);
    for (;;) {
        // `ready` is evaluated under the pool lock: Drain's predicate reads inFlight, and a
        // Future's reads only its own shared state, which never takes this lock.
        if (ready()) {
            return;
        }
        if (!pool.queue.empty()) {
            RunJob(pool, lock);
            continue;
        }
        pool.jobFinished.wait(lock);
    }
}

} // namespace detail
} // namespace Jobs
