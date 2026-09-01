#include "Application.h"
#include "ResourceManifest.h"
#include "QualitySettings.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

using namespace std;

namespace {
constexpr std::array<const char*, 6> kLowSkyBoxFaces = {
    "resource/textures_low/Main_SkyBox/PositiveX.dds",
    "resource/textures_low/Main_SkyBox/NegativeX.dds",
    "resource/textures_low/Main_SkyBox/PositiveY.dds",
    "resource/textures_low/Main_SkyBox/NegativeY.dds",
    "resource/textures_low/Main_SkyBox/PositiveZ.dds",
    "resource/textures_low/Main_SkyBox/NegativeZ.dds"
};

constexpr std::array<const char*, 6> kHighSkyBoxFaces = {
    "resource/textures/Main_SkyBox/PositiveX.dds",
    "resource/textures/Main_SkyBox/NegativeX.dds",
    "resource/textures/Main_SkyBox/PositiveY.dds",
    "resource/textures/Main_SkyBox/NegativeY.dds",
    "resource/textures/Main_SkyBox/PositiveZ.dds",
    "resource/textures/Main_SkyBox/NegativeZ.dds"
};
} // namespace

std::vector<std::string> GetSkyBoxFaces() {
    std::vector<std::string> faces;
    faces.reserve(kLowSkyBoxFaces.size());
    for (size_t i = 0; i < kLowSkyBoxFaces.size(); ++i) {
        faces.push_back(GetTexturePath(kLowSkyBoxFaces[i], kHighSkyBoxFaces[i]));
    }
    return faces;
}

const std::vector<std::string>& GetBackgroundSongPaths() {
    static const std::vector<std::string> kBackgroundSongPaths = {
        "resource/sounds/Stellardrone - Galaxies.mp3",
        "resource/sounds/Stellardrone - Mars.mp3",
        "resource/sounds/Stellardrone - Billions And Billions.mp3",
        "resource/sounds/Stellardrone - Gravitation (Remix).mp3",
        "resource/sounds/Stellardrone - The Edge of Forever.mp3"
    };
    return kBackgroundSongPaths;
}

void Application::LoadCoreResources() {
    struct CoreResource {
        std::string url;
        std::string virtualPath;
    };

    std::vector<CoreResource> coreResources = {
        {"resource/models/sphere.obj", "resource/models/sphere.obj"},
        {"resource/models/phobos.obj", "resource/models/phobos.obj"},
        {"resource/models/deimos.obj", "resource/models/deimos.obj"},
        {"resource/models/saturn_ring.obj", "resource/models/saturn_ring.obj"},
        {"resource/models/uranus_ring.obj", "resource/models/uranus_ring.obj"},
        {"resource/textures_low/Star_Spectrum_Low.dds", "resource/textures_low/Star_Spectrum_Low.dds"},
        {"resource/textures_low/flares_bright_Low.dds", "resource/textures_low/flares_bright_Low.dds"},
    };

    const auto skyBoxFaces = GetSkyBoxFaces();
    for (size_t i = 0; i < skyBoxFaces.size(); ++i) {
        // Use the quality-resolved path as both the fetch URL and the virtual
        // path.  On WASM GetTexturePath() returns the low-res path, so we must
        // fetch from resource/textures_low/Main_SkyBox/ (not the high-res
        // resource/textures/Main_SkyBox/ which 404s when only placeholder files
        // are deployed).  On native both paths are the high-res path anyway.
        coreResources.push_back({skyBoxFaces[i], skyBoxFaces[i]});
    }

    _totalResources = static_cast<int>(coreResources.size());
    _resourcesPending = _totalResources;

    std::cout << "Loading " << _totalResources << " core resources..." << std::endl;
    UpdateLoadingProgress();

    for (const auto& res : coreResources) {
        WebResourceFetcher::DownloadFile(res.url, res.virtualPath, [this, virtualPath = res.virtualPath](bool success) {
            _resourcesPending--;
            if (!success) {
                std::cerr << "[Loading] Failed to download required core resource: " << virtualPath << std::endl;
            }
            UpdateLoadingProgress();
        });
    }
}

void Application::LoadOptionalSounds() {
#ifdef __EMSCRIPTEN__
    const auto& songPaths = GetBackgroundSongPaths();
    std::cout << "[Audio] Starting optional background music download ("
              << songPaths.size() << " tracks)..." << std::endl;

    for (const auto& path : songPaths) {
        WebResourceFetcher::DownloadFile(path, path, [this, path](bool success) {
            if (success && WebResourceFetcher::ResourceExists(path)) {
                _availableSongPaths.insert(path);
                std::cout << "[Audio] Loaded track: " << path << std::endl;
                return;
            }

            std::cerr << "[Audio] Missing track (skipped): " << path << std::endl;
        });
    }
#endif
}

void Application::InitSongList() {
    _backgroundSongPaths = GetBackgroundSongPaths();

    default_random_engine randEngine(static_cast<uint32_t>(chrono::high_resolution_clock::now().time_since_epoch().count()));
    shuffle(_backgroundSongPaths.begin(), _backgroundSongPaths.end(), randEngine);

#if defined(SOLARSYSTEM_USE_SDL_MIXER) && !defined(__EMSCRIPTEN__)
    for (const auto& path : _backgroundSongPaths) {
        ifstream songFile(path);
        if (songFile.good()) {
            _availableSongPaths.insert(path);
        }
    }
#endif
}
