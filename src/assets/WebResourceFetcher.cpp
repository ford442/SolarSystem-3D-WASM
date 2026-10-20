#include "WebResourceFetcher.h"
#include "../core/Log.h"
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>

namespace {
    // Every caller waiting on an in-flight download, keyed by virtualPath. A duplicate
    // request joins the list instead of starting a second transfer (or being told it
    // failed); CompleteTransfer fires all of them with the one result.
    std::unordered_map<std::string, std::vector<std::function<void(bool)>>> s_inFlight;

    // Platform transfer: emscripten_async_wget2 on web, the test seam or a no-op stub
    // natively. Must eventually call CompleteTransfer(virtualPath, ok) exactly once.
    void StartTransfer(const std::string& url, const std::string& virtualPath);

    void CompleteTransfer(const std::string& virtualPath, bool success) {
        auto it = s_inFlight.find(virtualPath);
        if (it == s_inFlight.end()) {
            return;
        }
        // Detach before firing: a callback may request the same path again (e.g. a
        // fallback URL into the same virtualPath) and must start a fresh transfer.
        auto callbacks = std::move(it->second);
        s_inFlight.erase(it);
        for (auto& callback : callbacks) {
            if (callback) callback(success);
        }
    }
}

// Shared by both builds. The result is the transfer's own: a file already in MEMFS (the
// --preload-file placeholder tier) is still fetched, because the deployed copy is what
// replaces the placeholder. DownloadWithFallback is where a resident copy stands in.
void WebResourceFetcher::DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback) {
    auto& waiters = s_inFlight[virtualPath];
    waiters.push_back(std::move(callback));
    if (waiters.size() > 1) {
        return; // Joined the transfer already under way for this path.
    }
    StartTransfer(url, virtualPath);
}

#ifdef __EMSCRIPTEN__

namespace {
    constexpr const char* kDefaultRuntimeAssetBase = "/solar-system/";

    struct DownloadContext {
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
            typeof window !== 'undefined' && typeof window['__solarSystemAssetBase'] === 'string' && window['__solarSystemAssetBase'].length > 0
                ? window['__solarSystemAssetBase']
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
void OnLoad2(unsigned int /*handle*/, void* arg, const char* /*file*/) {
    auto* context = static_cast<DownloadContext*>(arg);
    LOG_DEBUG("WebResourceFetcher", "Successfully downloaded: %s -> %s", context->resolvedUrl.c_str(),
              context->virtualPath.c_str());
    const std::string virtualPath = context->virtualPath;
    delete context;
    CompleteTransfer(virtualPath, true);
}

void OnError2(unsigned int /*handle*/, void* arg, int status) {
    auto* context = static_cast<DownloadContext*>(arg);
    LOG_WARN("WebResourceFetcher", "Failed to download %s -> %s. Status: %d", context->resolvedUrl.c_str(),
             context->virtualPath.c_str(), status);
    const std::string virtualPath = context->virtualPath;
    delete context;
    CompleteTransfer(virtualPath, false);
}

// Matching the signature emscripten_async_wget2 expects: void (*)(unsigned int, void *, int)
void OnProgress2(unsigned int /*handle*/, void* /*arg*/, int /*bytesLoaded*/) {
    // Progress tracking - bytesLoaded is the number of bytes loaded so far
    // Note: We could expose this for more granular progress tracking
    // For now, we rely on the completion callbacks for progress
}

namespace {
void StartTransfer(const std::string& url, const std::string& virtualPath) {
    EnsureDirectoryExists(virtualPath);
    const std::string resolvedUrl = ResolveResourceUrl(url);
    LOG_DEBUG("WebResourceFetcher", "Starting download: %s to %s", resolvedUrl.c_str(), virtualPath.c_str());

    auto* context = new DownloadContext{resolvedUrl, virtualPath};

    // emscripten_async_wget2(url, file, requesttype, param, userdata, onload, onerror, onprogress)
    emscripten_async_wget2(resolvedUrl.c_str(), virtualPath.c_str(), "GET", nullptr, context, OnLoad2, OnError2, OnProgress2);
}
} // namespace

bool WebResourceFetcher::ResourceExists(const std::string& virtualPath) {
    return std::filesystem::exists(virtualPath);
}

#else

#ifdef SOLARSYSTEM_BUILD_TESTS
namespace {
WebResourceFetcher::TestDownloadHandler g_testDownloadHandler;
}

// Both also forget in-flight transfers: a test that never completes one would otherwise
// make the next test's request for that path join a transfer nobody will finish.
void WebResourceFetcher::SetTestDownloadHandler(TestDownloadHandler handler) {
    g_testDownloadHandler = std::move(handler);
    s_inFlight.clear();
}

void WebResourceFetcher::ClearTestDownloadHandler() {
    g_testDownloadHandler = nullptr;
    s_inFlight.clear();
}
#endif

namespace {
void StartTransfer(const std::string& url, const std::string& virtualPath) {
#ifdef SOLARSYSTEM_BUILD_TESTS
    if (g_testDownloadHandler) {
        g_testDownloadHandler(url, virtualPath, [virtualPath](bool success) {
            CompleteTransfer(virtualPath, success);
        });
        return;
    }
#endif
    LOG_DEBUG("WebResourceFetcher", "Downloading (native stub): %s", url.c_str());
    CompleteTransfer(virtualPath, true);
}
} // namespace

bool WebResourceFetcher::ResourceExists(const std::string& virtualPath) {
    return std::filesystem::exists(virtualPath);
}

#endif

namespace {
void TryUrlsFrom(std::shared_ptr<const std::vector<std::string>> urls, size_t index, const std::string& virtualPath,
                 std::function<void(WebResourceFetcher::FetchOutcome)> callback) {
    using Outcome = WebResourceFetcher::FetchOutcome;
    if (index >= urls->size()) {
        // emscripten_async_wget2 only replaces the file on a 2xx, so a failed fetch leaves
        // the preloaded copy intact.
        const bool resident = WebResourceFetcher::ResourceExists(virtualPath);
        if (resident) {
            LOG_INFO("WebResourceFetcher", "No deployed copy of %s; keeping the preloaded one.", virtualPath.c_str());
        }
        callback(resident ? Outcome::KeptResident : Outcome::Failed);
        return;
    }
    if (index > 0) {
        LOG_INFO("WebResourceFetcher", "%s unavailable; trying %s", (*urls)[index - 1].c_str(),
                 (*urls)[index].c_str());
    }
    WebResourceFetcher::DownloadFile((*urls)[index], virtualPath,
        [urls, index, virtualPath, callback = std::move(callback)](bool success) mutable {
            if (success) {
                callback(Outcome::Downloaded);
                return;
            }
            TryUrlsFrom(urls, index + 1, virtualPath, std::move(callback));
        });
}
} // namespace

void WebResourceFetcher::DownloadWithFallback(std::vector<std::string> urls, const std::string& virtualPath,
                                              std::function<void(FetchOutcome)> callback) {
    TryUrlsFrom(std::make_shared<const std::vector<std::string>>(std::move(urls)), 0, virtualPath,
                std::move(callback));
}

// Shared by both builds: purely a filesystem probe, so it never blocks the Wasm stack.
bool WebResourceFetcher::RequireResident(const std::string& virtualPath, const char* context) {
    if (ResourceExists(virtualPath)) {
        return true;
    }

    // Warn once per path: these fire from render-adjacent code, and a per-frame
    // reload attempt would otherwise spam the console with the same line.
    static std::unordered_set<std::string> s_warnedPaths;
    if (s_warnedPaths.insert(virtualPath).second) {
        LOG_ERROR("WebResourceFetcher",
                  "%s: '%s' is not resident. It must be downloaded via DownloadFile() before use "
                  "(core resources, a staged planet manifest, or TextureLoadingQueue); falling back.",
                  context ? context : "?", virtualPath.c_str());
    }
    return false;
}
