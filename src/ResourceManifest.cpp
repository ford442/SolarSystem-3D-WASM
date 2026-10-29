#include "Application.h"
#include "ResourceManifest.h"
#include "QualitySettings.h"
#include "assets/WebResourceFetcher.h"
#include "Solar_System/TexturePaths.h"
#include <array>
#include <chrono>
#include <fstream>
#include "core/Log.h"
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
        // Tried (into the same virtualPath) when url fails; empty for none.
        std::string fallbackUrl;
    };

    std::vector<CoreResource> coreResources = {
        {"resource/models/sphere.obj", "resource/models/sphere.obj"},
        {"resource/models/phobos.obj", "resource/models/phobos.obj"},
        {"resource/models/deimos.obj", "resource/models/deimos.obj"},
        {"resource/models/saturn_ring.obj", "resource/models/saturn_ring.obj"},
        {"resource/models/uranus_ring.obj", "resource/models/uranus_ring.obj"},
        // Resolved through the active texture pack so the staged file matches what the
        // Sun/lens-flare constructors later read (see TextureFormats::VariantPath).
        {TexturePaths::Resolve("resource/textures_low/Star_Spectrum_Low.dds"),
         TexturePaths::Resolve("resource/textures_low/Star_Spectrum_Low.dds")},
        {TexturePaths::Resolve("resource/textures_low/flares_bright_Low.dds"),
         TexturePaths::Resolve("resource/textures_low/flares_bright_Low.dds")},
    };

    const auto skyBoxFaces = GetSkyBoxFaces();
    for (size_t i = 0; i < skyBoxFaces.size(); ++i) {
        // Use the quality-resolved path as both the fetch URL and the virtual
        // path.  On WASM GetTexturePath() returns the low-res path, so we must
        // fetch from resource/textures_low/Main_SkyBox/ (not the high-res
        // resource/textures/Main_SkyBox/ which 404s when only placeholder files
        // are deployed).  On native both paths are the high-res path anyway.
        // Deployments have shipped only resource/textures/Main_SkyBox/ (the low-res
        // folder 404s), so retry the high-res face rather than fall back to black.
        const std::string highFace = kHighSkyBoxFaces[i];
        coreResources.push_back({skyBoxFaces[i], skyBoxFaces[i],
                                 highFace != skyBoxFaces[i] ? highFace : std::string()});
    }

    _totalResources = static_cast<int>(coreResources.size());
    _resourcesPending = _totalResources;
    _resourcesFailed = 0;

    LOG_INFO("Loading", "Loading %d core resources...", _totalResources);
    UpdateLoadingProgress();

    for (const auto& res : coreResources) {
        std::vector<std::string> urls{res.url};
        if (!res.fallbackUrl.empty()) {
            urls.push_back(res.fallbackUrl);
        }
        // A face the deployment lacks keeps its preloaded placeholder (KeptResident); only a
        // resource with no copy anywhere counts as failed.
        WebResourceFetcher::DownloadWithFallback(std::move(urls), res.virtualPath,
            [this, virtualPath = res.virtualPath](WebResourceFetcher::FetchOutcome outcome) {
                _resourcesPending--;
                if (outcome == WebResourceFetcher::FetchOutcome::Failed) {
                    _resourcesFailed++;
                    LOG_ERROR("Loading", "Failed to download required core resource: %s", virtualPath.c_str());
                }
                UpdateLoadingProgress();
            });
    }
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
