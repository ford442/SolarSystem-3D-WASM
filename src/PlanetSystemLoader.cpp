#include "Application.h"
#include "assets/PlanetManifestLoader.h"
#include "assets/WebResourceFetcher.h"
#include "Solar_System/OrbitLayout.h"
#include "QualitySettings.h"
#include "core/Log.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <string>

void Application::LoadPlanetSystemManifests() {
#ifdef __EMSCRIPTEN__
    constexpr const char* kManifestPath = "resource/planet_manifest.json";
    // Preloaded into the .data file via --preload-file (see CMakeLists.txt).
    WebResourceFetcher::RequireResident(kManifestPath, "LoadPlanetSystemManifests");

    int manifestVersion = 0;
    std::string error;
    const glm::vec3 sunPos = _sun->GetPosition();
    auto manifests = PlanetManifestLoader::LoadManifests(
        kManifestPath,
        sunPos,
        [this](const std::string& initTag) { return MakePlanetInitFunc(initTag); },
        manifestVersion,
        error);

    if (manifests.empty()) {
        LOG_ERROR("StagedLoading", "Failed to load planet manifest: %s", error.c_str());
        return;
    }

    _planetSystemManifests = std::move(manifests);
    RefreshPlanetProxyPositions();
    LOG_INFO("StagedLoading", "Loaded manifest v%d with %zu planet systems deferred until camera approaches.",
             manifestVersion, _planetSystemManifests.size());
    ActivateNearestPlanetSystems(
        GetQualitySettings(_simState.qualityPreset, _simState.isMobileWeb).eagerPlanetSystems);
#endif
}

void Application::ActivateNearestPlanetSystems(int count) {
#ifdef __EMSCRIPTEN__
    // From the default pose no system is inside its activation radius, so without this the
    // first view is an empty sky labelled "(approach to load)".
    const glm::vec3 camPos = CameraRenderPosition();
    std::vector<PlanetSystemManifest*> byDistance;
    for (auto& manifest : _planetSystemManifests) {
        byDistance.push_back(&manifest);
    }
    std::sort(byDistance.begin(), byDistance.end(), [&camPos](const auto* a, const auto* b) {
        return glm::length(camPos - a->proxyPosition) < glm::length(camPos - b->proxyPosition);
    });
    const size_t n = std::min(byDistance.size(), static_cast<size_t>(std::max(count, 0)));
    for (size_t i = 0; i < n; ++i) {
        StartPlanetSystemDownload(*byDistance[i], "nearest to the start pose");
    }
#else
    (void)count;
#endif
}

void Application::ActivatePlanetSystem(OrbitLayout::Body body) {
#ifdef __EMSCRIPTEN__
    for (auto& manifest : _planetSystemManifests) {
        if (OrbitLayout::BodyFromName(manifest.name) == body) {
            StartPlanetSystemDownload(manifest, "focused");
            return;
        }
    }
#else
    (void)body;
#endif
}

void Application::StartPlanetSystemDownload(PlanetSystemManifest& manifest, const char* reason) {
    if (manifest.state != PlanetSystemManifest::State::NOT_LOADED) {
        return;
    }
    LOG_INFO("StagedLoading", "%s — starting download for %s", reason, manifest.name.c_str());
    manifest.state = PlanetSystemManifest::State::DOWNLOADING;
    manifest.totalDownloads = static_cast<int>(manifest.assetPaths.size());
    manifest.pendingDownloads = manifest.totalDownloads;

    // Download required assets — decrement pendingDownloads on completion (success or failure)
    for (const auto& path : manifest.assetPaths) {
        WebResourceFetcher::DownloadWithFallback({path}, path,
            [&manifest, path](WebResourceFetcher::FetchOutcome outcome) {
                manifest.pendingDownloads--;
                if (outcome == WebResourceFetcher::FetchOutcome::Failed) {
                    LOG_ERROR("StagedLoading", "Required asset %s failed for %s", path.c_str(),
                              manifest.name.c_str());
                }
            });
    }

    // Download optional assets (moons, rings, clouds) — fire-and-forget.
    // These do NOT block init; failure is expected when textures are not yet deployed.
    for (const auto& path : manifest.optionalAssetPaths) {
        WebResourceFetcher::DownloadWithFallback({path}, path,
            [name = manifest.name](WebResourceFetcher::FetchOutcome outcome) {
                if (outcome == WebResourceFetcher::FetchOutcome::Failed) {
                    LOG_INFO("StagedLoading", "Optional asset unavailable for %s — fallback texture will be used.",
                             name.c_str());
                }
            });
    }
}

std::function<void()> Application::MakePlanetInitFunc(const std::string& initTag) {
    if (!BodyCatalog::FindPrimaryByInitTag(initTag.c_str())) {
        return {};
    }
    return [this, initTag] { InitCatalogSystem(*_sphereModel, initTag); };
}
void Application::UpdatePlanetSystemLoading() {
#ifdef __EMSCRIPTEN__
    const glm::vec3 camPos = CameraRenderPosition();
    for (auto& manifest : _planetSystemManifests) {
        if (manifest.state == PlanetSystemManifest::State::READY) continue;

        if (manifest.state == PlanetSystemManifest::State::NOT_LOADED) {
            float dist = glm::length(camPos - manifest.proxyPosition);
            if (dist < manifest.activationRadius) {
                StartPlanetSystemDownload(manifest, "Camera approached");
            }
        }

        if (manifest.state == PlanetSystemManifest::State::DOWNLOADING) {
            if (manifest.pendingDownloads <= 0) {
                LOG_INFO("StagedLoading", "Required assets ready for %s — initializing system.",
                         manifest.name.c_str());
                manifest.initFunc();
                manifest.state = PlanetSystemManifest::State::READY;
            }
        }
    }
#endif
}

void Application::RefreshPlanetProxyPositions() {
#ifdef __EMSCRIPTEN__
    if (_planetSystemManifests.empty()) {
        return;
    }
    for (auto& manifest : _planetSystemManifests) {
        const OrbitLayout::Body body = OrbitLayout::BodyFromName(manifest.name);
        manifest.proxyPosition = OrbitLayout::GetRenderPosition(body);
    }
#endif
}

