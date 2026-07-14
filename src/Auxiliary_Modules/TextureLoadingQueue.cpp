#include "TextureLoadingQueue.h"
#include "TextureImage2D.h"
#include "WebResourceFetcher.h"
#include <algorithm>
#include <iostream>
#include <chrono>
#include <cmath>

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
}

bool TextureLoadingQueue::QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture,
                                           std::function<void(bool)> callback, TextureLoadCategory category) {
    if (_pendingPaths.count(path)) {
        // Duplicate request while already queued or downloading — ignore to prevent
        // redundant network fetches and reloads.
        return false;
    }
    _pendingPaths.insert(path);
    _queue.push({path, id, texture, callback, category, 0, std::chrono::steady_clock::time_point{}});
    _totalQueued++;
    _categoryQueued.at(static_cast<size_t>(category))++;
    return true;
}

void TextureLoadingQueue::SetMaxConcurrentLoads(int maxConcurrentLoads) {
    _maxConcurrentLoads = std::max(0, maxConcurrentLoads);
    std::cout << "[TextureLoadingQueue] Concurrency limit set to " << _maxConcurrentLoads << std::endl;

    if (_maxConcurrentLoads == 0) {
        // Low quality is a hard streaming stop. Mark active requests so their
        // eventual callbacks cannot apply textures, and discard queued work now.
        _cancelledPaths.insert(_pendingPaths.begin(), _pendingPaths.end());
        while (!_queue.empty()) {
            TextureLoadJob job = _queue.front();
            _queue.pop();
            _cancelledPaths.erase(job.path);
            _pendingPaths.erase(job.path);
            _totalCancelled++;
            if (job.callback) job.callback(false);
        }
    }
}

void TextureLoadingQueue::ProcessQueue() {
    while (_activeLoads < _maxConcurrentLoads && !_queue.empty()) {
        TextureLoadJob job = _queue.front();

        // Preserve FIFO retry ordering: a backed-off front job waits until its
        // next attempt instead of being overtaken indefinitely by newer work.
        if (job.nextAttemptTime > std::chrono::steady_clock::now()) {
            return;
        }

        _queue.pop();

        // If cancelled while queued (e.g. camera moved away), drop it without fetching.
        if (_cancelledPaths.erase(job.path)) {
            _pendingPaths.erase(job.path);
            _totalCancelled++;
            if (job.callback) job.callback(false);
            continue;
        }

        ++_activeLoads;
        _currentPath = job.path;

        std::cout << "[TextureLoadingQueue][" << CategoryName(job.category) << "] Loading texture: " << job.textureId << " from " << job.path
                  << " (active " << _activeLoads << "/" << _maxConcurrentLoads << ")" << std::endl;

        WebResourceFetcher::DownloadFile(job.path, job.path, [this, job](bool success) mutable {
            --_activeLoads;

            // If cancelled while in-flight, suppress apply/reload and do not count as completed.
            if (_cancelledPaths.erase(job.path)) {
                _pendingPaths.erase(job.path);
                _totalCancelled++;
                if (job.callback) job.callback(false);
                return;
            }

            bool didRetry = false;
            if (success) {
                try {
                    if (job.targetTexture) {
                        job.targetTexture->ReloadTexture(job.path);
                        std::cout << "[TextureLoadingQueue] Successfully loaded: " << job.textureId << std::endl;
                        _totalCompleted++;
                        _categoryCompleted.at(static_cast<size_t>(job.category))++;
                        if (job.callback) job.callback(true);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[TextureLoadingQueue] Failed to load texture " << job.textureId << ": " << e.what() << std::endl;
                    if (job.retries < job.maxRetries) {
                        job.retries++;
                        auto delay = std::chrono::milliseconds(250 * (1 << std::min(job.retries - 1, 3)));
                        job.nextAttemptTime = std::chrono::steady_clock::now() + delay;
                        _queue.push(job);
                        std::cout << "[TextureLoadingQueue] Queued retry " << job.retries << "/" << job.maxRetries << " for " << job.textureId << std::endl;
                        didRetry = true;
                    } else {
                        _totalFailed++;
                        if (job.callback) job.callback(false);
                        _pendingPaths.erase(job.path);
                    }
                }
            } else {
                std::cerr << "[TextureLoadingQueue] Failed to download " << job.textureId << " (will keep previous texture)" << std::endl;
                if (job.retries < job.maxRetries) {
                    job.retries++;
                    auto delay = std::chrono::milliseconds(250 * (1 << std::min(job.retries - 1, 3)));
                    job.nextAttemptTime = std::chrono::steady_clock::now() + delay;
                    _queue.push(job);
                    std::cout << "[TextureLoadingQueue] Queued retry " << job.retries << "/" << job.maxRetries << " for " << job.textureId << std::endl;
                    didRetry = true;
                } else {
                    _totalFailed++;
                    if (job.callback) job.callback(false);
                    _pendingPaths.erase(job.path);
                }
            }

            if (!didRetry) {
                // Final success (no throw) or final failure already handled erase; ensure for success path.
                if (success) {
                    _pendingPaths.erase(job.path);
                }
            }
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
        // Will be dropped on next ProcessQueue() if not yet started, or suppressed on completion.
        std::cout << "[TextureLoadingQueue] Cancel requested for " << path << std::endl;
    }
}
