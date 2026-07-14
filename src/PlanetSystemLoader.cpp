#include "Application.h"
#include "Auxiliary_Modules/PlanetManifestLoader.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"
#include "Solar_System/OrbitLayout.h"
#include <deque>
#include <functional>
#include <iostream>
#include <string>

void Application::LoadPlanetSystemManifests() {
#ifdef __EMSCRIPTEN__
    constexpr const char* kManifestPath = "resource/planet_manifest.json";
    WebResourceFetcher::Fetch(kManifestPath);

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
#endif
}

std::function<void()> Application::MakePlanetInitFunc(const std::string& initTag) {
    if (initTag == "Mercury") return [this] { InitMercury(*_sphereModel); };
    if (initTag == "Venus") return [this] { InitVenus(*_sphereModel); };
    if (initTag == "EarthSystem") return [this] { InitEarthSystem(*_sphereModel); };
    if (initTag == "MarsSystem") return [this] { InitMarsSystem(*_sphereModel); };
    if (initTag == "JupiterSystem") return [this] { InitJupiterSystem(*_sphereModel); };
    if (initTag == "SaturnSystem") return [this] { InitSaturnSystem(*_sphereModel); };
    if (initTag == "UranusSystem") return [this] { InitUranusSystem(*_sphereModel); };
    if (initTag == "NeptuneSystem") return [this] { InitNeptuneSystem(*_sphereModel); };
    if (initTag == "PlutoSystem") return [this] { InitPlutoSystem(*_sphereModel); };
    return {};
}
void Application::UpdatePlanetSystemLoading() {
#ifdef __EMSCRIPTEN__
    const glm::vec3 camPos = camera.GetPosition();
    for (auto& manifest : _planetSystemManifests) {
        if (manifest.state == PlanetSystemManifest::State::READY) continue;

        if (manifest.state == PlanetSystemManifest::State::NOT_LOADED) {
            float dist = glm::length(camPos - manifest.proxyPosition);
            if (dist < manifest.activationRadius) {
                std::cout << "[StagedLoading] Camera within " << dist
                          << " units — starting download for " << manifest.name << std::endl;
                manifest.state = PlanetSystemManifest::State::DOWNLOADING;
                manifest.totalDownloads = static_cast<int>(manifest.assetPaths.size());
                manifest.pendingDownloads = manifest.totalDownloads;

                // Download required assets — decrement pendingDownloads on completion (success or failure)
                for (const auto& path : manifest.assetPaths) {
                    WebResourceFetcher::DownloadFile(path, path, [&manifest](bool success) {
                        manifest.pendingDownloads--;
                        if (!success) {
                            std::cerr << "[StagedLoading] Required asset failed for "
                                      << manifest.name << std::endl;
                        }
                    });
                }

                // Download optional assets (moons, rings, clouds) — fire-and-forget.
                // These do NOT block init; failure is expected when textures are not yet deployed.
                for (const auto& path : manifest.optionalAssetPaths) {
                    WebResourceFetcher::DownloadFile(path, path, [name = manifest.name](bool success) {
                        if (!success) {
                            std::cout << "[StagedLoading] Optional asset unavailable for "
                                      << name << " — fallback texture will be used." << std::endl;
                        }
                    });
                }
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
    const glm::vec3 sunPos = _sun ? _sun->GetPosition() : glm::vec3(0.0f);
    for (auto& manifest : _planetSystemManifests) {
        const OrbitLayout::Body body = OrbitLayout::BodyFromName(manifest.name);
        manifest.proxyPosition = sunPos + OrbitLayout::GetOffset(body);
    }
#endif
}

void Application::RenderPlanetProxyMarkers() const {
#ifdef __EMSCRIPTEN__
    if (_planetSystemManifests.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _cameraProjection);
    _mainTextShader->SetMat4("view", _cameraView);
    _mainTextShader->SetBool("is3D", true);

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
        _mainTextShader->SetVec3("particleCenterWorldSpace", manifest.proxyPosition);
        _textRenderer->Render(*_mainTextShader, chars, 0.0, 0.0, 0.075, glm::vec3(0.5f, 0.7f, 1.0f));
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
#endif
}
