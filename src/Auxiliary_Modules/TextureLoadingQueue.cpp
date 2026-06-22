#include "TextureLoadingQueue.h"
#include "TextureImage2D.h"
#include "WebResourceFetcher.h"
#include <iostream>
#include <chrono>
#include <cmath>

TextureLoadingQueue& TextureLoadingQueue::GetInstance() {
    static TextureLoadingQueue instance;
    return instance;
}

void TextureLoadingQueue::QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture, std::function<void(bool)> callback) {
    if (_pendingPaths.count(path)) {
        // Duplicate request while already queued or downloading — ignore to prevent
        // redundant network fetches and reloads.
        return;
    }
    _pendingPaths.insert(path);
    _queue.push({path, id, texture, callback, 0, std::chrono::steady_clock::time_point{}});
    _totalQueued++;
}

void TextureLoadingQueue::ProcessQueue() {
    if (_isLoading || _queue.empty()) {
        return;
    }

    TextureLoadJob job = _queue.front();

    // Backoff check: if this job has a future retry time, leave it and wait.
    if (job.nextAttemptTime > std::chrono::steady_clock::now()) {
        return;
    }

    _queue.pop();

    // If cancelled while queued (e.g. camera moved away), drop it without fetching.
    if (_cancelledPaths.erase(job.path)) {
        _pendingPaths.erase(job.path);
        if (job.callback) job.callback(false);
        return;
    }

    _isLoading = true;
    _currentPath = job.path;

    std::cout << "[TextureLoadingQueue] Loading texture: " << job.textureId << " from " << job.path << std::endl;

    WebResourceFetcher::DownloadFile(job.path, job.path, [this, job](bool success) mutable {
        _isLoading = false;

        // If cancelled while in-flight, suppress apply/reload and do not count as completed.
        if (_cancelledPaths.erase(job.path)) {
            _pendingPaths.erase(job.path);
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

void TextureLoadingQueue::CancelLoad(const std::string& path) {
    if (_pendingPaths.count(path)) {
        _cancelledPaths.insert(path);
        // Will be dropped on next ProcessQueue() if not yet started, or suppressed on completion.
        std::cout << "[TextureLoadingQueue] Cancel requested for " << path << std::endl;
    }
}
