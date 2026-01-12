#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

class WebResourceFetcher {
public:
    static void Fetch(const std::string& path) {
#ifdef __EMSCRIPTEN__
        // 1. Check if file already exists in MEMFS (e.g. preloaded)
        if (std::filesystem::exists(path)) {
            return;
        }

        std::cout << "[WebResourceFetcher] Fetching: " << path << " ..." << std::endl;

        // 2. Prepare for synchronous download
        void* buffer = nullptr;
        int numBytes = 0;
        int error = 0;

        // emscripten_wget_data is synchronous (blocks execution)
        // arg1: url, arg2: buffer, arg3: size, arg4: error
        emscripten_wget_data(path.c_str(), &buffer, &numBytes, &error);

        if (error || !buffer || numBytes == 0) {
            std::cerr << "[WebResourceFetcher] FATAL: Failed to download " << path << std::endl;
            if (buffer) free(buffer);
            return;
        }

        // 3. Ensure directory structure exists in MEMFS
        std::filesystem::path fsPath(path);
        if (fsPath.has_parent_path()) {
            std::filesystem::create_directories(fsPath.parent_path());
        }

        // 4. Write memory buffer to MEMFS file
        std::ofstream outfile(path, std::ios::binary);
        outfile.write(static_cast<char*>(buffer), numBytes);
        outfile.close();

        // 5. Free the Emscripten buffer
        free(buffer);

        std::cout << "[WebResourceFetcher] Downloaded and written to MEMFS: " << path << std::endl;
#else
        // Desktop behavior: Do nothing, file should be on disk
        return;
#endif
    }
};
