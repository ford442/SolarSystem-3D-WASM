#include "Application.h"
#include "Auxiliary_Modules/PlanetManifestLoader.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"
#include "Solar_System/OrbitLayout.h"
#include "QualitySettings.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <iostream>
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
        std::cerr << "[StagedLoading] Failed to load planet manifest: " << error << std::endl;
        return;
    }

    _planetSystemManifests = std::move(manifests);
    RefreshPlanetProxyPositions();
    std::cout << "[StagedLoading] Loaded manifest v" << manifestVersion
              << " with " << _planetSystemManifests.size()
              << " planet systems deferred until camera approaches." << std::endl;
    ActivateNearestPlanetSystems(
        GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb).eagerPlanetSystems);
#endif
}

void Application::ActivateNearestPlanetSystems(int count) {
#ifdef __EMSCRIPTEN__
    // From the default pose no system is inside its activation radius, so without this the
    // first view is an empty sky labelled "(approach to load)".
    const glm::vec3 camPos = _camera.GetPosition();
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
    std::cout << "[StagedLoading] " << reason << " — starting download for " << manifest.name << std::endl;
    manifest.state = PlanetSystemManifest::State::DOWNLOADING;
    manifest.totalDownloads = static_cast<int>(manifest.assetPaths.size());
    manifest.pendingDownloads = manifest.totalDownloads;

    // Download required assets — decrement pendingDownloads on completion (success or failure)
    for (const auto& path : manifest.assetPaths) {
        WebResourceFetcher::DownloadWithFallback({path}, path,
            [&manifest, path](WebResourceFetcher::FetchOutcome outcome) {
                manifest.pendingDownloads--;
                if (outcome == WebResourceFetcher::FetchOutcome::Failed) {
                    std::cerr << "[StagedLoading] Required asset " << path << " failed for "
                              << manifest.name << std::endl;
                }
            });
    }

    // Download optional assets (moons, rings, clouds) — fire-and-forget.
    // These do NOT block init; failure is expected when textures are not yet deployed.
    for (const auto& path : manifest.optionalAssetPaths) {
        WebResourceFetcher::DownloadWithFallback({path}, path,
            [name = manifest.name](WebResourceFetcher::FetchOutcome outcome) {
                if (outcome == WebResourceFetcher::FetchOutcome::Failed) {
                    std::cout << "[StagedLoading] Optional asset unavailable for "
                              << name << " — fallback texture will be used." << std::endl;
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
    const glm::vec3 camPos = _camera.GetPosition();
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
                std::cout << "[StagedLoading] Required assets ready for " << manifest.name
                          << " — initializing system." << std::endl;
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

void Application::RenderPlanetProxyMarkers() const {
#ifdef __EMSCRIPTEN__
    if (_planetSystemManifests.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _renderer.mainTextShader->Use();
    _renderer.mainTextShader->SetMat4("projection", _renderer.cameraProjection);
    _renderer.mainTextShader->SetMat4("view", _renderer.cameraView);
    _renderer.mainTextShader->SetBool("is3D", true);

    for (const auto& manifest : _planetSystemManifests) {
        if (manifest.state == PlanetSystemManifest::State::READY) continue;

        std::wstring label(manifest.name.begin(), manifest.name.end());
        if (manifest.state == PlanetSystemManifest::State::NOT_LOADED) {
            label += L" (approach to load)";
        } else if (manifest.state == PlanetSystemManifest::State::DOWNLOADING) {
            int done = manifest.totalDownloads - manifest.pendingDownloads;
            int pct = (manifest.totalDownloads > 0) ? (done * 100 / manifest.totalDownloads) : 0;
            label += L" [downloading " + std::to_wstring(pct) + L"%]";
        }

        std::deque<wchar_t> chars(label.begin(), label.end());
        _renderer.mainTextShader->SetVec3("particleCenterWorldSpace", manifest.proxyPosition);
        _renderer.textRenderer->Render(*_renderer.mainTextShader, chars, 0.0, 0.0, 0.075, glm::vec3(0.5f, 0.7f, 1.0f));
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#endif
}
