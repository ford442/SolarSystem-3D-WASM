#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <functional>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

class WebResourceFetcher {
public:
    // Callback-based download into MEMFS (emscripten_async_wget2 on web, no-op stub
    // natively).  This is the *only* download path: it never blocks the Wasm stack,
    // so the build does not need ASYNCIFY/JSPI.  Anything a constructor needs must
    // already have been pulled in through here (Application::LoadCoreResources, the
    // staged planet manifests, or TextureLoadingQueue) before the constructor runs.
    static void DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback);

    // Returns true if the file is present in the Emscripten MEMFS (or native filesystem).
    // Useful for LOD decisions: only attempt high-res download if we want to try fetching it.
    static bool ResourceExists(const std::string& virtualPath);

    // Non-blocking residency assertion used at the read sites that used to call the
    // blocking Fetch().  Returns ResourceExists(virtualPath) and logs once per missing
    // path so a staged download that never landed shows up in the console instead of
    // silently degrading to a fallback mesh/texture.
    static bool RequireResident(const std::string& virtualPath, const char* context);

#ifdef SOLARSYSTEM_BUILD_TESTS
    using TestDownloadHandler = std::function<void(const std::string& url, const std::string& virtualPath,
                                                   std::function<void(bool)> callback)>;
    static void SetTestDownloadHandler(TestDownloadHandler handler);
    static void ClearTestDownloadHandler();
#endif
};
