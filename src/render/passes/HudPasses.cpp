// Text and screen-space passes after the scene (docs/ARCHITECTURE.md §7.5): staging labels at
// systems not loaded yet, body distances, the hint list, the texture-upgrade line and the
// star-system jump's fade. Observe's HUD is in ObservePasses.cpp.
#include "Renderer.h"
#include "FrameContext.h"
#include "PlanetSystemManifest.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include "assets/TextureLoadingQueue.h"
#include "astro/Ephemeris.h"
#include "astro/SkyEvents.h"
#include "gfx/GlPlatform.h"
#include "gfx/Shader.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include "render/TextRenderer.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#ifdef __EMSCRIPTEN__
#include "JsBridge.h"
#endif

namespace {

// Text over the frame: no depth test, straight alpha; and back to the scene's defaults after.
const rhi::PipelineState kTextOver = rhi::PipelineState().SetDepthTest(false).SetBlend(true).SetBlendFunc(
    rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha);
const rhi::PipelineState kTextDone = rhi::PipelineState().SetBlend(false).SetDepthTest(true);

} // namespace

void Renderer::ProxyLabels(rhi::CommandList& cmd) {
    RenderPlanetProxyMarkers(cmd, View(0));
}

void Renderer::Distances(rhi::CommandList& cmd) {
    RenderPlanetSatelliteStarDistances(cmd, View(0));
}

void Renderer::Hints(rhi::CommandList& cmd) {
    RenderHints(cmd, View(0));
}

void Renderer::LoadingProgress(rhi::CommandList& cmd) {
    RenderTextureLoadingProgress(cmd, View(0));
}

void Renderer::RenderPlanetProxyMarkers(rhi::CommandList& cmd, const FrameContext& ctx) const {
    // Web staged loading: a label at the orbital position of every system not READY yet.
    if (!_overlayToggles.stagingLabels || !ctx.scene.stagedSystems || ctx.scene.stagedSystems->empty()) return;

    cmd.SetPipelineState(kTextOver);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", ctx.view.projection);
    _mainTextShader->SetMat4("view", ctx.view.view);
    _mainTextShader->SetBool("is3D", true);

    for (const auto& manifest : *ctx.scene.stagedSystems) {
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

    cmd.SetPipelineState(kTextDone);
}

void Renderer::RenderPlanetSatelliteStarDistances(rhi::CommandList& cmd, const FrameContext& ctx) const {
    cmd.SetPipelineState(kTextOver);

    if (_overlayToggles.planetStarDistances) RenderSpaceObjectDistance(ctx, ctx.scene.sun);

    for (const auto& renderableComponentPS : (*ctx.scene.components)) {
        if (_overlayToggles.planetStarDistances) {
            RenderSpaceObjectDistance(ctx, renderableComponentPS.planet.get());
        }

        if (_overlayToggles.satelliteDistances) {
            for (const auto& satellite : renderableComponentPS.satellites) {
                RenderSpaceObjectDistance(ctx, satellite.get());
            }
        }
    }

    cmd.SetPipelineState(kTextDone);
}

void Renderer::RenderSpaceObjectDistance(const FrameContext& ctx, const SpaceObject* spaceObject) const {
    // Reuse pre-allocated container to eliminate per-frame heap allocation
    _distanceInfoCache.clear();
    _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetEngName().begin(),
                              spaceObject->GetEngName().end());

    if (!spaceObject->GetOtherLangName().empty()) {
        _distanceInfoCache.push_back(L'[');
        _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetOtherLangName().begin(),
                                  spaceObject->GetOtherLangName().end());
        _distanceInfoCache.push_back(L']');
        _distanceInfoCache.push_back(L' ');
    }

    std::wstring distance(
        std::to_wstring(static_cast<uint16_t>(glm::distance(ctx.view.headPosition, spaceObject->GetPosition()))));
    _distanceInfoCache.insert(_distanceInfoCache.end(), std::make_move_iterator(distance.begin()),
                              std::make_move_iterator(distance.end()));

    _mainTextShader->Use();
    _mainTextShader->SetVec3("particleCenterWorldSpace", spaceObject->GetPosition());
    _mainTextShader->SetBool("is3D", true);
    _textRenderer->Render(*_mainTextShader, _distanceInfoCache, 0.0, 0.0, 0.075, glm::vec3(0.98431, 0.80784, 0.69412));
}

void Renderer::RenderHints(rhi::CommandList& cmd, const FrameContext& ctx) const {
    static const std::string gpuHintString = std::string(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    static constexpr glm::vec3 textColor = glm::vec3(0.98431, 0.80784, 0.69412);

    cmd.SetPipelineState(kTextOver);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _hudProjection);
    _mainTextShader->SetBool("is3D", false);

    // Reuse pre-allocated containers to eliminate per-frame heap allocations
    _fpsHintCache.clear();
    _fpsHintCache.emplace_back(L"FPS: ");
    _fpsHintCache.emplace_back(std::to_wstring(ctx.hud->fps));

    _gpuHintCache.clear();
    _gpuHintCache.emplace_back(std::wstring(gpuHintString.begin(), gpuHintString.end()));

#ifndef __EMSCRIPTEN__ // web: volume and track live in the settings panel (web/src/music.ts)
    _soundVolumeHintCache.clear();
    std::stringstream soundVolumeStream;
    soundVolumeStream << std::fixed << std::setprecision(0) << ctx.hud->musicVolumePercent;
    std::string soundVolume = soundVolumeStream.str();
    _soundVolumeHintCache.emplace_back(
        std::wstring(L"Sound volume(PgUp/PgDown): ").append(soundVolume.begin(), soundVolume.end()).append(L" %"));

    _tmpStringCache.clear();
    _tmpStringCache.emplace_back(std::wstring(ctx.hud->musicTrack.begin(), ctx.hud->musicTrack.end()));
#endif

    std::deque<std::wstring> timeRunHint;
    timeRunHint.emplace_back(L"Time (P:pause, +/-:scale, .:step): ");
    std::wstringstream wss;
    wss << (ctx.sim.timePaused ? L"paused" : L"run") << L" x" << static_cast<int>(ctx.sim.timeScale);
    int year = 0;
    int month = 0;
    int day = 0;
    Ephemeris::YmdFromJulianDate(OrbitLayout::GetJulianDate(), year, month, day);
    wss << L" | " << year << L'-' << std::setfill(L'0') << std::setw(2) << month << L'-' << std::setw(2) << day;
    timeRunHint.emplace_back(wss.str());

    // Sky event driven by the ephemeris, not a baked animation — see SkyEvents.h.
    std::deque<std::wstring> conjunctionHint;
    if (const SkyEvents::SkyEvent* next = ctx.scene.nextSkyEvent; next && next->valid) {
        const std::string line = SkyEvents::FormatEvent(*next);
        conjunctionHint.emplace_back(line.begin(), line.end()); // ASCII only, see SkyEvents
    }

    std::deque<std::wstring> planetStarHint;
    planetStarHint.emplace_back(L"Planet/Star distances(Z): ");
    planetStarHint.emplace_back((_overlayToggles.planetStarDistances) ? L"On" : L"Off");

    std::deque<std::wstring> satelliteHint;
    satelliteHint.emplace_back(L"Satellite distances(X): ");
    satelliteHint.emplace_back((_overlayToggles.satelliteDistances) ? L"On" : L"Off");

#ifdef __EMSCRIPTEN__
    // Indicate low-res start + high-res streaming (visual in streaming-progress + on-screen when active)
    std::deque<std::wstring> textureHint;
    textureHint.emplace_back(L"Web: low-res start; high-res streams when close (see HUD)");
#endif

    std::deque<std::wstring> cameraSpeedHint;
    cameraSpeedHint.emplace_back(L"Camera speed(1/2): ");
    cameraSpeedHint.emplace_back(std::to_wstring(ctx.hud->cameraSpeed));

    std::deque<std::wstring> smoothCameraHint;
    smoothCameraHint.emplace_back(L"Smooth camera(Arrows)");

    std::deque<std::wstring> smoothZoomHint;
    smoothZoomHint.emplace_back(L"Smooth zoom(V/B)");

    std::deque<std::wstring> movementHint;
    movementHint.emplace_back(L"Move up/down(SPACE/C)");

    std::deque<std::wstring> speedBostHint;
    speedBostHint.emplace_back(L"Speed boost(SHIFT)");

    std::deque<std::wstring> starExposureHint;
    starExposureHint.emplace_back(L"Star Exposure(3/4): ");
    starExposureHint.emplace_back(std::to_wstring(_starTuning.exposure));

    std::deque<std::wstring> starGammaHint;
    starGammaHint.emplace_back(L"Star Gamma(5/6): ");
    starGammaHint.emplace_back(std::to_wstring(_starTuning.gamma));

    std::deque<std::wstring> starTemperatureHint;
    std::stringstream starTemperatureStream;
    starTemperatureStream << std::fixed << std::setprecision(0) << _starTuning.temperatureKelvin;
    std::string starTemperatureStr = starTemperatureStream.str();
    starTemperatureHint.emplace_back(std::wstring(L"Star Temperature(7/8): ")
                                         .append(std::make_move_iterator(starTemperatureStr.begin()),
                                                 std::make_move_iterator(starTemperatureStr.end())));
    std::deque<std::wstring> vertSyncHint;
    vertSyncHint.emplace_back(L"Vert Sync(F1): ");
    vertSyncHint.emplace_back((ctx.hud->vsync) ? L"On" : L"Off");

    std::deque<std::wstring> textHints;
    textHints.emplace_back(L"Text hints(TAB)");

    std::deque<std::wstring> magneticHint;
    magneticHint.emplace_back(L"Magnetic fields(M): ");
    magneticHint.emplace_back(_magneticFieldsEnabled ? L"On" : L"Off");

#ifndef __EMSCRIPTEN__
    _textRenderer->ReverseRender(*_mainTextShader, _tmpStringCache, 0.99 * ctx.display.x, 0.95 * ctx.display.y, 0.35,
                                 textColor);
#endif
    if (!conjunctionHint.empty()) {
        _textRenderer->ReverseRender(*_mainTextShader, conjunctionHint, 0.99 * ctx.display.x, 0.925 * ctx.display.y,
                                     0.35, textColor);
    }
    _textRenderer->Render(*_mainTextShader, _fpsHintCache, 0.01 * ctx.display.x, 0.95 * ctx.display.y, 0.35,
                          ctx.hud->fpsColor);
    _textRenderer->Render(*_mainTextShader, _gpuHintCache, 0.01 * ctx.display.x, 0.925 * ctx.display.y, 0.35,
                          textColor);
#ifndef __EMSCRIPTEN__
    _textRenderer->Render(*_mainTextShader, _soundVolumeHintCache, 0.01 * ctx.display.x, 0.9 * ctx.display.y, 0.35,
                          textColor);
#endif
    _textRenderer->Render(*_mainTextShader, timeRunHint, 0.01 * ctx.display.x, 0.875 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, planetStarHint, 0.01 * ctx.display.x, 0.85 * ctx.display.y, 0.35,
                          textColor);
    _textRenderer->Render(*_mainTextShader, satelliteHint, 0.01 * ctx.display.x, 0.825 * ctx.display.y, 0.35,
                          textColor);
#ifdef __EMSCRIPTEN__
    _textRenderer->Render(*_mainTextShader, textureHint, 0.01 * ctx.display.x, 0.80 * ctx.display.y, 0.30,
                          glm::vec3(0.6f, 0.8f, 1.0f));
#endif
    _textRenderer->Render(*_mainTextShader, cameraSpeedHint, 0.01 * ctx.display.x, 0.8 * ctx.display.y, 0.35,
                          textColor);
    _textRenderer->Render(*_mainTextShader, smoothCameraHint, 0.01 * ctx.display.x, 0.775 * ctx.display.y, 0.35,
                          textColor);
    _textRenderer->Render(*_mainTextShader, smoothZoomHint, 0.01 * ctx.display.x, 0.75 * ctx.display.y, 0.35,
                          textColor);
    _textRenderer->Render(*_mainTextShader, movementHint, 0.01 * ctx.display.x, 0.725 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, speedBostHint, 0.01 * ctx.display.x, 0.7 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starExposureHint, 0.01 * ctx.display.x, 0.675 * ctx.display.y, 0.35,
                          textColor);
    _textRenderer->Render(*_mainTextShader, starGammaHint, 0.01 * ctx.display.x, 0.65 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starTemperatureHint, 0.01 * ctx.display.x, 0.625 * ctx.display.y, 0.35,
                          textColor);
    _textRenderer->Render(*_mainTextShader, vertSyncHint, 0.01 * ctx.display.x, 0.6 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, textHints, 0.01 * ctx.display.x, 0.575 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, magneticHint, 0.01 * ctx.display.x, 0.55 * ctx.display.y, 0.35, textColor);

    cmd.SetPipelineState(kTextDone);
}

void Renderer::RenderTextureLoadingProgress(rhi::CommandList& cmd, const FrameContext& ctx) const {
    auto& loadingQueue = TextureLoadingQueue::GetInstance();
    const int queued = loadingQueue.GetQueuedCount();
    const int completed = loadingQueue.GetTotalProcessed();
    const int total = loadingQueue.GetTotalQueued();
    const int active = loadingQueue.GetActiveLoadCount();
    const std::string& currentPath = loadingQueue.GetCurrentLoadingPath();
    // 0=generic, 1=mid, 2=high — mirrors streaming UI labels in progressOverlay.ts
    int tierCode = 0;
    if (currentPath.find("textures_mid/") != std::string::npos) {
        tierCode = 1;
    } else if (currentPath.find("textures/") != std::string::npos &&
               currentPath.find("textures_low/") == std::string::npos) {
        tierCode = 2;
    }

#ifdef __EMSCRIPTEN__
    NotifyStreamingProgress(completed, total, active, tierCode);
#endif

    if (queued == 0) {
        return;
    }

    cmd.SetPipelineState(kTextOver);

    static constexpr glm::vec3 textColor = glm::vec3(0.3f, 0.8f, 1.0f);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _hudProjection);
    _mainTextShader->SetBool("is3D", false);

    // Hint string with progress counters, e.g. "Mid-res upgrade (2/5)". Rebuilt only when a
    // counter changes, but drawn every frame: the frame is cleared, so a hint drawn one frame
    // in ten (as the old throttle did) flickers.
    const std::array<int, 4> counters = {completed, total, active, tierCode};
    if (_loadingHintCache.empty() || counters != _loadingHintCounters) {
        _loadingHintCounters = counters;
        const wchar_t* tierLabel = L"Texture upgrade";
        if (tierCode == 1) {
            tierLabel = L"Mid-res upgrade";
        } else if (tierCode == 2) {
            tierLabel = L"High-res upgrade";
        }
        std::wstring hint =
            std::wstring(tierLabel) + L" (" + std::to_wstring(completed) + L"/" + std::to_wstring(total) + L")";
        if (active > 0) {
            hint += L" [" + std::to_wstring(active) + L" active]";
        }
        _loadingHintCache.clear();
        _loadingHintCache.emplace_back(std::move(hint));
    }
    _textRenderer->Render(*_mainTextShader, _loadingHintCache, 0.5f * ctx.display.x - 150, 0.1f * ctx.display.y, 0.25,
                          textColor);

    cmd.SetPipelineState(kTextDone);
}

void Renderer::ScreenFade(rhi::CommandList& cmd) {
    // Declared only while a jump fades (HudState::fadeAmount > 0) and its program is built.
    const FrameContext& ctx = View(0);
    cmd.BindFramebuffer(ctx.defaultFramebuffer);
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetDepthWrite(false).SetBlend(true).SetBlendFunc(
        rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha));
    _screenFadeShader->Use();
    _screenFadeShader->SetVec4("fadeColor", glm::vec4(ctx.hud->fadeColor, std::min(ctx.hud->fadeAmount, 1.0f)));
    cmd.DrawFullscreenTriangle(_screenFadeVao.get());
    cmd.SetPipelineState(rhi::PipelineState().SetBlend(false).SetDepthWrite(true).SetDepthTest(true));
}
