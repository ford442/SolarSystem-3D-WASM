#include "WebResourceFetcher.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unordered_set>

#ifdef __EMSCRIPTEN__

namespace {
    constexpr const char* kDefaultRuntimeAssetBase = "/solar-system/";

    struct DownloadContext {
        std::function<void(bool)> callback;
        std::string resolvedUrl;
        std::string virtualPath;
    };

    // Guard against duplicate concurrent async downloads for the exact same virtual asset.
    std::unordered_set<std::string> s_activeDownloads;

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
    s_activeDownloads.erase(context->virtualPath);
    std::cout << "Successfully downloaded: " << context->resolvedUrl << " -> " << context->virtualPath << std::endl;
    if (context->callback) {
        context->callback(true);
    }
    delete context;
}

void OnError2(unsigned int handle, void* arg, int status) {
    auto* context = static_cast<DownloadContext*>(arg);
    s_activeDownloads.erase(context->virtualPath);
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
    if (s_activeDownloads.count(virtualPath)) {
        // Duplicate concurrent request for same asset; ignore to avoid redundant traffic.
        // The first one will deliver and invoke its callback.
        if (callback) callback(false); // signal as non-success for caller dedup, caller may ignore
        return;
    }
    s_activeDownloads.insert(virtualPath);

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
        std::cerr << "[WebResourceFetcher] Warning: Failed to download " << resolvedUrl
                  << " (fallback texture will be used if applicable)" << std::endl;
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

bool WebResourceFetcher::ResourceExists(const std::string& virtualPath) {
    return std::filesystem::exists(virtualPath);
}

#else

#ifdef SOLARSYSTEM_BUILD_TESTS
namespace {
WebResourceFetcher::TestDownloadHandler g_testDownloadHandler;
}

void WebResourceFetcher::SetTestDownloadHandler(TestDownloadHandler handler) {
    g_testDownloadHandler = std::move(handler);
}

void WebResourceFetcher::ClearTestDownloadHandler() {
    g_testDownloadHandler = nullptr;
}
#endif

void WebResourceFetcher::DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback) {
#ifdef SOLARSYSTEM_BUILD_TESTS
    if (g_testDownloadHandler) {
        g_testDownloadHandler(url, virtualPath, std::move(callback));
        return;
    }
#endif
    std::cout << "Downloading (native stub): " << url << std::endl;
    if (callback) callback(true);
}

void WebResourceFetcher::Fetch(const std::string& path) {
}

bool WebResourceFetcher::ResourceExists(const std::string& virtualPath) {
    return std::filesystem::exists(virtualPath);
}

#endif
