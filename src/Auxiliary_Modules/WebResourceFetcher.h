#ifndef SOLARSYSTEM_WEBRESOURCEFETCHER_H
#define SOLARSYSTEM_WEBRESOURCEFETCHER_H

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/wget.h>
#endif

#include <string>
#include <functional>
#include <iostream>

class WebResourceFetcher {
public:
    static void DownloadFile(const std::string& url, const std::string& virtualPath, std::function<void(bool)> callback);
};

#endif //SOLARSYSTEM_WEBRESOURCEFETCHER_H
