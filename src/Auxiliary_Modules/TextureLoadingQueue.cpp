#include "TextureLoadingQueue.h"
#include "TextureImage2D.h"
#include "WebResourceFetcher.h"
#include <iostream>

TextureLoadingQueue& TextureLoadingQueue::GetInstance() {
    static TextureLoadingQueue instance;
    return instance;
}

void TextureLoadingQueue::QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture, std::function<void(bool)> callback) {
    _queue.push({path, id, texture, callback, 0});
}

void TextureLoadingQueue::ProcessQueue() {
    if (_isLoading || _queue.empty()) {
        return;
    }

    TextureLoadJob job = _queue.front();
    _queue.pop();
    _isLoading = true;
    _currentPath = job.path;

    std::cout << "[TextureLoadingQueue] Loading texture: " << job.textureId << " from " << job.path << std::endl;

    WebResourceFetcher::DownloadFile(job.path, job.path, [this, job](bool success) mutable {
        _isLoading = false;

        if (success) {
            try {
                if (job.targetTexture) {
                    job.targetTexture->ReloadTexture(job.path);
                    std::cout << "[TextureLoadingQueue] Successfully loaded: " << job.textureId << std::endl;
                    if (job.callback) job.callback(true);
                }
            } catch (const std::exception& e) {
                std::cerr << "[TextureLoadingQueue] Failed to load texture " << job.textureId << ": " << e.what() << std::endl;
                if (job.retries < job.maxRetries) {
                    job.retries++;
                    _queue.push(job);
                    std::cout << "[TextureLoadingQueue] Queued retry " << job.retries << "/" << job.maxRetries << " for " << job.textureId << std::endl;
                } else {
                    if (job.callback) job.callback(false);
                }
            }
        } else {
            std::cerr << "[TextureLoadingQueue] Failed to download " << job.textureId << std::endl;
            if (job.retries < job.maxRetries) {
                job.retries++;
                _queue.push(job);
            } else {
                if (job.callback) job.callback(false);
            }
        }
    });
}
