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
    static void DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback);
    static void Fetch(const std::string& path);

    // Returns true if the file is present in the Emscripten MEMFS (or native filesystem).
    // Useful for LOD decisions: only attempt high-res download if we want to try fetching it.
    static bool ResourceExists(const std::string& virtualPath);
};
