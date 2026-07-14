#pragma once

#include <functional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Staged-loading manifest for a planet system (WASM only).
struct PlanetSystemManifest {
    std::string name;
    glm::vec3 proxyPosition;
    float activationRadius = 0.f;
    std::vector<std::string> assetPaths;
    std::vector<std::string> optionalAssetPaths;
    std::vector<std::string> optionalHighResAssetPaths;
    std::function<void()> initFunc;

    enum class State { NOT_LOADED, DOWNLOADING, READY } state = State::NOT_LOADED;
    int pendingDownloads{0};
    int totalDownloads{0};
};
