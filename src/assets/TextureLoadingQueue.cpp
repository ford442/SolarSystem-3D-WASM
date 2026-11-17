#include "TextureLoadingQueue.h"
#include "../gfx/TextureImage2D.h"
#include "WebResourceFetcher.h"
#include "../core/Log.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten/heap.h>
#endif

TextureLoadingQueue& TextureLoadingQueue::GetInstance() {
    static TextureLoadingQueue instance;
    return instance;
}

namespace {
const char* CategoryName(TextureLoadCategory category) {
    switch (category) {
        case TextureLoadCategory::Planet: return "planet";
        case TextureLoadCategory::Satellite: return "satellite";
        case TextureLoadCategory::Ring: return "ring";
        case TextureLoadCategory::Clouds: return "clouds";
        case TextureLoadCategory::Count: break;
    }
    return "unknown";
}

#ifdef __EMSCRIPTEN__
size_t GetWasmHeapBytes() {
    return emscripten_get_heap_size();
}

constexpr size_t kMemoryPressureBytes = static_cast<size_t>(768) * 1024 * 1024;
#endif
}

int TextureLoadingQueue::GetEffectiveMaxConcurrentLoads() {
    if (_maxConcurrentLoads <= 0) {
        return 0;
    }

#ifdef __EMSCRIPTEN__
    const size_t heapBytes = GetWasmHeapBytes();
    if (heapBytes >= kMemoryPressureBytes) {
        if (!_memoryBackpressureActive) {
            LOG_INFO("TextureLoadingQueue", "Memory backpressure active (heap %zu MiB); limiting concurrency to 1",
                     heapBytes / (1024 * 1024));
        }
        _memoryBackpressureActive = true;
        return 1;
    }

    if (_memoryBackpressureActive) {
        LOG_INFO("TextureLoadingQueue", "Memory backpressure released (heap %zu MiB)", heapBytes / (1024 * 1024));
    }
    _memoryBackpressureActive = false;
#endif

    return _maxConcurrentLoads;
}

bool TextureLoadingQueue::QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture,
                                           std::function<void(bool)> callback, TextureLoadCategory category) {
    if (_pendingPaths.count(path)) {
        return false;
    }
    _pendingPaths.insert(path);
    TextureLoadJob job{path, id, texture, std::move(callback), category, 0, std::chrono::steady_clock::time_point{}};
    job.epoch = _epoch;
    _queue.push(std::move(job));
    _totalQueued++;
    _categoryQueued.at(static_cast<size_t>(category))++;
    ProcessQueue();
    return true;
}

void TextureLoadingQueue::SetMaxConcurrentLoads(int maxConcurrentLoads) {
    _maxConcurrentLoads = std::max(0, maxConcurrentLoads);
    LOG_INFO("TextureLoadingQueue", "Concurrency limit set to %d", _maxConcurrentLoads);

    if (_maxConcurrentLoads == 0) {
        _cancelledPaths.insert(_pendingPaths.begin(), _pendingPaths.end());
        while (!_queue.empty()) {
            TextureLoadJob job = _queue.front();
            _queue.pop();
            _cancelledPaths.erase(job.path);
            _pendingPaths.erase(job.path);
            _totalCancelled++;
            if (job.callback) job.callback(false);
        }
        return;
    }

    ProcessQueue();
}

void TextureLoadingQueue::HandleDownloadResult(TextureLoadJob job, bool success) {
    if (job.epoch != _epoch) {
        return; // CancelAll: the texture and the callback's owner are gone
    }
    if (_cancelledPaths.erase(job.path)) {
        _pendingPaths.erase(job.path);
        _totalCancelled++;
        if (job.callback) job.callback(false);
        return;
    }

    if (success && job.targetTexture) {
        // Decode in a job, upload under the frame budget (TextureImage2D::ReloadTextureAsync);
        // the load stays "active" until the texture is swapped in, so the concurrency limit
        // also bounds how many decoded payloads wait in memory.
        ++_activeLoads;
        TextureImage2D* target = job.targetTexture;
        const std::string path = job.path;
        target->ReloadTextureAsync(
            path,
            [this, job](TextureImage2D::AsyncResult result) mutable {
                --_activeLoads;
                HandleTextureApplied(std::move(job), result);
                ProcessQueue();
            },
            [this, path, epoch = job.epoch] { return epoch == _epoch && _cancelledPaths.count(path) == 0; });
        return;
    }

    RetryOrFail(std::move(job));
}

void TextureLoadingQueue::HandleTextureApplied(TextureLoadJob job, TextureImage2D::AsyncResult result) {
    if (job.epoch != _epoch) {
        return; // CancelAll
    }
    switch (result) {
        case TextureImage2D::AsyncResult::Uploaded:
            LOG_DEBUG("TextureLoadingQueue", "Successfully loaded: %s", job.textureId.c_str());
            _totalCompleted++;
            _categoryCompleted.at(static_cast<size_t>(job.category))++;
            _pendingPaths.erase(job.path);
            if (job.callback) job.callback(true);
            return;
        case TextureImage2D::AsyncResult::Superseded:
            // Cancelled while it decoded, or a newer reload of the same texture won.
            _cancelledPaths.erase(job.path);
            _pendingPaths.erase(job.path);
            _totalCancelled++;
            if (job.callback) job.callback(false);
            return;
        case TextureImage2D::AsyncResult::Failed:
            LOG_WARN("TextureLoadingQueue", "Failed to load texture %s", job.textureId.c_str());
            RetryOrFail(std::move(job));
            return;
    }
}

void TextureLoadingQueue::RetryOrFail(TextureLoadJob job) {
    LOG_WARN("TextureLoadingQueue", "Failed to download %s (will keep previous texture)", job.textureId.c_str());

    if (job.retries < job.maxRetries) {
        job.retries++;
        const auto delay = std::chrono::milliseconds(250 * (1 << std::min(job.retries - 1, 3)));
        job.nextAttemptTime = std::chrono::steady_clock::now() + delay;
        LOG_INFO("TextureLoadingQueue", "Queued retry %d/%d for %s", job.retries, job.maxRetries,
                 job.textureId.c_str());
        _queue.push(std::move(job));
        return;
    }
    _totalFailed++;
    if (job.callback) job.callback(false);
    _pendingPaths.erase(job.path);
}

void TextureLoadingQueue::ProcessQueue() {
    const int effectiveMax = GetEffectiveMaxConcurrentLoads();
    if (effectiveMax <= 0 || _queue.empty()) {
        return;
    }

    size_t deferredScans = 0;
    const size_t initialQueueSize = _queue.size();

    while (_activeLoads < effectiveMax && !_queue.empty()) {
        TextureLoadJob job = _queue.front();
        _queue.pop();

        if (job.nextAttemptTime > std::chrono::steady_clock::now()) {
            _queue.push(job);
            ++deferredScans;
            if (deferredScans >= initialQueueSize) {
                break;
            }
            continue;
        }

        deferredScans = 0;

        if (_cancelledPaths.erase(job.path)) {
            _pendingPaths.erase(job.path);
            _totalCancelled++;
            if (job.callback) job.callback(false);
            continue;
        }

        ++_activeLoads;
        _currentPath = job.path;

        LOG_DEBUG("TextureLoadingQueue", "[%s] Loading texture: %s from %s (active %d/%d)", CategoryName(job.category),
                  job.textureId.c_str(), job.path.c_str(), _activeLoads, effectiveMax);

        WebResourceFetcher::DownloadFile(job.path, job.path, [this, job](bool success) mutable {
            --_activeLoads;
            HandleDownloadResult(job, success);
            ProcessQueue();
        });
    }
}

int TextureLoadingQueue::GetCategoryQueued(TextureLoadCategory category) const {
    return _categoryQueued.at(static_cast<size_t>(category));
}

int TextureLoadingQueue::GetCategoryCompleted(TextureLoadCategory category) const {
    return _categoryCompleted.at(static_cast<size_t>(category));
}

void TextureLoadingQueue::CancelAll() {
    ++_epoch;
    _totalCancelled += static_cast<int>(_queue.size());
    while (!_queue.empty()) {
        _queue.pop();
    }
    _pendingPaths.clear();
    _cancelledPaths.clear();
    _currentPath.clear();
    LOG_INFO("TextureLoadingQueue", "Cancelled every pending texture load (scene teardown)");
}

void TextureLoadingQueue::CancelLoad(const std::string& path) {
    if (_pendingPaths.count(path)) {
        _cancelledPaths.insert(path);
        LOG_DEBUG("TextureLoadingQueue", "Cancel requested for %s", path.c_str());
    }
}

#ifdef SOLARSYSTEM_BUILD_TESTS
void TextureLoadingQueue::ResetForTests() {
    while (!_queue.empty()) {
        _queue.pop();
    }
    _activeLoads = 0;
    _maxConcurrentLoads = 2;
    _memoryBackpressureActive = false;
    _currentPath.clear();
    _pendingPaths.clear();
    _cancelledPaths.clear();
    _totalQueued = 0;
    _totalCompleted = 0;
    _totalFailed = 0;
    _totalCancelled = 0;
    _categoryQueued = {};
    _categoryCompleted = {};
}
#endif
