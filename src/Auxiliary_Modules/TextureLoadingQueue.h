#pragma once

#include <string>
#include <queue>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>
#include <chrono>
#include <array>

class TextureImage2D;

enum class TextureLoadCategory {
    Planet,
    Satellite,
    Ring,
    Clouds,
    Count
};

struct TextureLoadJob {
    std::string path;
    std::string textureId;
    TextureImage2D* targetTexture;
    std::function<void(bool)> callback;
    TextureLoadCategory category = TextureLoadCategory::Planet;
    int retries = 0;
    std::chrono::steady_clock::time_point nextAttemptTime{};
    const int maxRetries = 3;
};

class TextureLoadingQueue {
public:
    static TextureLoadingQueue& GetInstance();

    bool QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture,
                          std::function<void(bool)> callback,
                          TextureLoadCategory category = TextureLoadCategory::Planet);
    void ProcessQueue();
    void SetMaxConcurrentLoads(int maxConcurrentLoads);
    int GetMaxConcurrentLoads() const { return _maxConcurrentLoads; }
    int GetActiveLoadCount() const { return _activeLoads; }
    int GetQueuedCount() const { return static_cast<int>(_queue.size()) + _activeLoads; }
    bool IsLoading() const { return _activeLoads > 0; }
    const std::string& GetCurrentLoadingPath() const { return _currentPath; }

    // Cancel a pending or in-flight load (deprioritize when camera moves away).
    // For in-flight async downloads, suppresses the texture reload on completion.
    void CancelLoad(const std::string& path);

    // Cumulative stats for JS progress reporting
    int GetTotalQueued()    const { return _totalQueued; }
    int GetTotalCompleted() const { return _totalCompleted; }
    int GetTotalFailed()    const { return _totalFailed; }
    int GetTotalCancelled() const { return _totalCancelled; }
    int GetTotalProcessed() const { return _totalCompleted + _totalFailed + _totalCancelled; }
    int GetCategoryQueued(TextureLoadCategory category) const;
    int GetCategoryCompleted(TextureLoadCategory category) const;

private:
    TextureLoadingQueue() = default;
    ~TextureLoadingQueue() = default;
    TextureLoadingQueue(const TextureLoadingQueue&) = delete;
    TextureLoadingQueue& operator=(const TextureLoadingQueue&) = delete;

    std::queue<TextureLoadJob> _queue;
    int _activeLoads = 0;
    int _maxConcurrentLoads = 1;
    std::string _currentPath;

    // Dedup + cancel support for resilience against repeated LOD checks and camera movement.
    std::unordered_set<std::string> _pendingPaths;
    std::unordered_set<std::string> _cancelledPaths;

    int _totalQueued    = 0;
    int _totalCompleted = 0;
    int _totalFailed    = 0;
    int _totalCancelled = 0;
    std::array<int, static_cast<size_t>(TextureLoadCategory::Count)> _categoryQueued{};
    std::array<int, static_cast<size_t>(TextureLoadCategory::Count)> _categoryCompleted{};
};
