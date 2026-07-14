#include "TextureLoadingQueue.h"
#include "TextureImage2D.h"
#include "WebResourceFetcher.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cmath>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
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
    return static_cast<size_t>(EM_ASM_INT({ return HEAP8.length; }));
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
            std::cout << "[TextureLoadingQueue] Memory backpressure active (heap "
                      << (heapBytes / (1024 * 1024)) << " MiB); limiting concurrency to 1"
                      << std::endl;
        }
        _memoryBackpressureActive = true;
        return 1;
    }

    if (_memoryBackpressureActive) {
        std::cout << "[TextureLoadingQueue] Memory backpressure released (heap "
                  << (heapBytes / (1024 * 1024)) << " MiB)" << std::endl;
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
    _queue.push({path, id, texture, callback, category, 0, std::chrono::steady_clock::time_point{}});
    _totalQueued++;
    _categoryQueued.at(static_cast<size_t>(category))++;
    ProcessQueue();
    return true;
}

void TextureLoadingQueue::SetMaxConcurrentLoads(int maxConcurrentLoads) {
    _maxConcurrentLoads = std::max(0, maxConcurrentLoads);
    std::cout << "[TextureLoadingQueue] Concurrency limit set to " << _maxConcurrentLoads << std::endl;

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

void TextureLoadingQueue::FinishJob(TextureLoadJob job, bool success, bool didRetry) {
    if (!didRetry) {
        if (success) {
            _pendingPaths.erase(job.path);
        }
    }
}

void TextureLoadingQueue::HandleDownloadResult(TextureLoadJob job, bool success) {
    if (_cancelledPaths.erase(job.path)) {
        _pendingPaths.erase(job.path);
        _totalCancelled++;
        if (job.callback) job.callback(false);
        return;
    }

    if (success) {
        try {
            if (job.targetTexture) {
                job.targetTexture->ReloadTexture(job.path);
                std::cout << "[TextureLoadingQueue] Successfully loaded: " << job.textureId << std::endl;
                _totalCompleted++;
                _categoryCompleted.at(static_cast<size_t>(job.category))++;
                if (job.callback) job.callback(true);
                FinishJob(job, true, false);
                return;
            }
        } catch (const std::exception& e) {
            std::cerr << "[TextureLoadingQueue] Failed to load texture " << job.textureId << ": " << e.what() << std::endl;
            success = false;
        }
    }

    std::cerr << "[TextureLoadingQueue] Failed to download " << job.textureId
              << " (will keep previous texture)" << std::endl;

    bool didRetry = false;
    if (job.retries < job.maxRetries) {
        job.retries++;
        const auto delay = std::chrono::milliseconds(250 * (1 << std::min(job.retries - 1, 3)));
        job.nextAttemptTime = std::chrono::steady_clock::now() + delay;
        _queue.push(job);
        std::cout << "[TextureLoadingQueue] Queued retry " << job.retries << "/" << job.maxRetries
                  << " for " << job.textureId << std::endl;
        didRetry = true;
    } else {
        _totalFailed++;
        if (job.callback) job.callback(false);
        _pendingPaths.erase(job.path);
    }

    FinishJob(job, false, didRetry);
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

        std::cout << "[TextureLoadingQueue][" << CategoryName(job.category) << "] Loading texture: " << job.textureId
                  << " from " << job.path << " (active " << _activeLoads << "/" << effectiveMax << ")" << std::endl;

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

void TextureLoadingQueue::CancelLoad(const std::string& path) {
    if (_pendingPaths.count(path)) {
        _cancelledPaths.insert(path);
        std::cout << "[TextureLoadingQueue] Cancel requested for " << path << std::endl;
    }
}
