#include "WebResourceFetcher.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>

#ifdef __EMSCRIPTEN__

namespace {
    constexpr const char* kDefaultRuntimeAssetBase = "/solar-system/";

    struct DownloadContext {
        std::function<void(bool)> callback;
        std::string resolvedUrl;
        std::string virtualPath;
    };

    bool IsAbsoluteUrl(const std::string& url) {
        return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0;
    }

    std::string NormalizeBaseUrl(std::string baseUrl) {
        if (!baseUrl.empty() && baseUrl.back() != '/') {
            baseUrl.push_back('/');
        }
        return baseUrl;
    }

    std::string TrimLeadingSlash(std::string path) {
        while (!path.empty() && path.front() == '/') {
            path.erase(path.begin());
        }
        return path;
    }

    EM_JS(char*, GetRuntimeAssetBaseUrl, (const char* fallbackBasePtr), {
        const fallbackBase = UTF8ToString(fallbackBasePtr);
        const configuredBase =
            typeof window !== 'undefined' && typeof window.__solarSystemAssetBase === 'string' && window.__solarSystemAssetBase.length > 0
                ? window.__solarSystemAssetBase
                : fallbackBase;
        const length = lengthBytesUTF8(configuredBase) + 1;
        const buffer = _malloc(length);
        stringToUTF8(configuredBase, buffer, length);
        return buffer;
    });

    std::string ResolveResourceUrl(const std::string& path) {
        if (IsAbsoluteUrl(path)) {
            return path;
        }

        char* runtimeBase = GetRuntimeAssetBaseUrl(kDefaultRuntimeAssetBase);
        std::string baseUrl = runtimeBase ? runtimeBase : "";
        free(runtimeBase);

        if (baseUrl.empty()) {
            baseUrl = kDefaultRuntimeAssetBase;
        }

        return NormalizeBaseUrl(baseUrl) + TrimLeadingSlash(path);
    }
}

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
    auto* context = static_cast<DownloadContext*>(arg);
    std::cout << "Successfully downloaded: " << context->resolvedUrl << " -> " << context->virtualPath << std::endl;
    if (context->callback) {
        context->callback(true);
    }
    delete context;
}

void OnError2(unsigned int handle, void* arg, int status) {
    auto* context = static_cast<DownloadContext*>(arg);
    std::cerr << "Failed to download " << context->resolvedUrl << " -> " << context->virtualPath << ". Status: " << status << std::endl;
    if (context->callback) {
        context->callback(false);
    }
    delete context;
}

// Matching the signature from the error message: void (*)(unsigned int, void *, int)
void OnProgress2(unsigned int handle, void* arg, int bytesLoaded) {
    // Progress tracking - bytesLoaded is the number of bytes loaded so far
    // Note: We could expose this for more granular progress tracking
    // For now, we rely on the completion callbacks for progress
}

void WebResourceFetcher::DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback) {
    EnsureDirectoryExists(virtualPath);
    const std::string resolvedUrl = ResolveResourceUrl(url);
    std::cout << "Starting download: " << resolvedUrl << " to " << virtualPath << std::endl;

    auto* context = new DownloadContext{std::move(callback), resolvedUrl, virtualPath};

    // emscripten_async_wget2(url, file, requesttype, param, userdata, onload, onerror, onprogress)
    emscripten_async_wget2(resolvedUrl.c_str(), virtualPath.c_str(), "GET", nullptr, context, OnLoad2, OnError2, OnProgress2);
}

void WebResourceFetcher::Fetch(const std::string& path) {
    if (std::filesystem::exists(path)) {
        return;
    }

    const std::string resolvedUrl = ResolveResourceUrl(path);
    std::cout << "[WebResourceFetcher] Fetching: " << resolvedUrl << " ..." << std::endl;

    void* buffer = nullptr;
    int numBytes = 0;
    int error = 0;

    emscripten_wget_data(resolvedUrl.c_str(), &buffer, &numBytes, &error);

    if (error || !buffer || numBytes == 0) {
        std::cerr << "[WebResourceFetcher] FATAL: Failed to download " << resolvedUrl << " into " << path << std::endl;
        if (buffer) {
            free(buffer);
        }
        return;
    }

    std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::filesystem::create_directories(fsPath.parent_path());
    }

    std::ofstream outfile(path, std::ios::binary);
    outfile.write(static_cast<char*>(buffer), numBytes);
    outfile.close();

    free(buffer);

    std::cout << "[WebResourceFetcher] Downloaded and written to MEMFS: " << resolvedUrl << " -> " << path << std::endl;
}

#else

void WebResourceFetcher::DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback) {
    std::cout << "Downloading: " << url << " (size estimate: " << GetFileSize(url) << " bytes)" << std::endl;  // Add logging for debugging
    if (callback) callback(true);
}

void WebResourceFetcher::Fetch(const std::string& path) {
}

#endif
