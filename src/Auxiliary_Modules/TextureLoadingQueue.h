#pragma once

#include <string>
#include <queue>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>
#include <chrono>

class TextureImage2D;

struct TextureLoadJob {
    std::string path;
    std::string textureId;
    TextureImage2D* targetTexture;
    std::function<void(bool)> callback;
    int retries = 0;
    std::chrono::steady_clock::time_point nextAttemptTime{};
    const int maxRetries = 3;
};

class TextureLoadingQueue {
public:
    static TextureLoadingQueue& GetInstance();

    void QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture, std::function<void(bool)> callback);
    void ProcessQueue();
    int GetQueuedCount() const { return static_cast<int>(_queue.size()) + (_isLoading ? 1 : 0); }
    bool IsLoading() const { return _isLoading; }
    const std::string& GetCurrentLoadingPath() const { return _currentPath; }

    // Cancel a pending or in-flight load (deprioritize when camera moves away).
    // For in-flight async downloads, suppresses the texture reload on completion.
    void CancelLoad(const std::string& path);

    // Cumulative stats for JS progress reporting
    int GetTotalQueued()    const { return _totalQueued; }
    int GetTotalCompleted() const { return _totalCompleted; }
    int GetTotalFailed()    const { return _totalFailed; }

private:
    TextureLoadingQueue() = default;
    ~TextureLoadingQueue() = default;
    TextureLoadingQueue(const TextureLoadingQueue&) = delete;
    TextureLoadingQueue& operator=(const TextureLoadingQueue&) = delete;

    std::queue<TextureLoadJob> _queue;
    bool _isLoading = false;
    std::string _currentPath;

    // Dedup + cancel support for resilience against repeated LOD checks and camera movement.
    std::unordered_set<std::string> _pendingPaths;
    std::unordered_set<std::string> _cancelledPaths;

    int _totalQueued    = 0;
    int _totalCompleted = 0;
    int _totalFailed    = 0;
};
