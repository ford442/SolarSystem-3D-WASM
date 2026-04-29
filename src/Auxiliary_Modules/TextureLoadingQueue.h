#pragma once

#include <string>
#include <queue>
#include <functional>
#include <memory>
#include <vector>

class TextureImage2D;

struct TextureLoadJob {
    std::string path;
    std::string textureId;
    TextureImage2D* targetTexture;
    std::function<void(bool)> callback;
    int retries = 0;
    const int maxRetries = 3;
};

class TextureLoadingQueue {
public:
    static TextureLoadingQueue& GetInstance();

    void QueueTextureLoad(const std::string& path, const std::string& id, TextureImage2D* texture, std::function<void(bool)> callback);
    void ProcessQueue();
    int GetQueuedCount() const { return _queue.size() + (_isLoading ? 1 : 0); }
    bool IsLoading() const { return _isLoading; }
    const std::string& GetCurrentLoadingPath() const { return _currentPath; }

private:
    TextureLoadingQueue() = default;
    ~TextureLoadingQueue() = default;
    TextureLoadingQueue(const TextureLoadingQueue&) = delete;
    TextureLoadingQueue& operator=(const TextureLoadingQueue&) = delete;

    std::queue<TextureLoadJob> _queue;
    bool _isLoading = false;
    std::string _currentPath;
};
