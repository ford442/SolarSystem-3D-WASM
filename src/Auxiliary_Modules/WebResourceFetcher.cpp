#include "WebResourceFetcher.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#ifdef __EMSCRIPTEN__

// Helper to ensure directory exists
void EnsureDirectoryExists(const std::string& path) {
    std::string currentPath;
    size_t pos = 0;
    while((pos = path.find('/', pos)) != std::string::npos) {
        currentPath = path.substr(0, pos);
        if (!currentPath.empty()) {
             // 0777 is permission (rwxrwxrwx)
            mkdir(currentPath.c_str(), 0777);
        }
        pos++;
    }
}

// Callback wrappers for emscripten_async_wget2
void OnLoad2(unsigned int handle, void* arg, const char* file) {
    auto* callback = static_cast<std::function<void(bool)>*>(arg);
    std::cout << "Successfully downloaded: " << file << std::endl;
    if (callback && *callback) {
        (*callback)(true);
    }
    delete callback;
}

void OnError2(unsigned int handle, void* arg, int status) {
    auto* callback = static_cast<std::function<void(bool)>*>(arg);
    std::cerr << "Failed to download. Status: " << status << std::endl;
    if (callback && *callback) {
        (*callback)(false);
    }
    delete callback;
}

// Matching the signature from the error message: void (*)(unsigned int, void *, int)
void OnProgress2(unsigned int handle, void* arg, int bytesLoaded) {
    // Progress tracking - bytesLoaded is the number of bytes loaded so far
    // Note: We could expose this for more granular progress tracking
    // For now, we rely on the completion callbacks for progress
}

void WebResourceFetcher::DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback) {
    EnsureDirectoryExists(virtualPath);
    std::cout << "Starting download: " << url << " to " << virtualPath << std::endl;

    // We allocate the callback on heap to pass it to the C callback.
    auto* cb = new std::function<void(bool)>(callback);

    // emscripten_async_wget2(url, file, requesttype, param, userdata, onload, onerror, onprogress)
    emscripten_async_wget2(url.c_str(), virtualPath.c_str(), "GET", nullptr, (void*)cb, OnLoad2, OnError2, OnProgress2);
}

#else

void WebResourceFetcher::DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback) {
    if (callback) callback(true);
}

#endif
