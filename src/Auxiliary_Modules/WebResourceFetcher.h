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
};
