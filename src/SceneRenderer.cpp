#include "Application.h"
#include "JsBridge.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include <deque>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

using namespace std;

void Application::ProcessSceneComponentsRendering() {
    const float timeScale = gSimState->timePaused ? 0.0f : gSimState->timeScale;

    for (auto& component : _renderableSceneComponents) {
        // Place bodies once per frame (shadow + color passes only render).
        component.planet->AdjustToParent(timeScale);
        for (const auto& satellite : component.satellites) {
            satellite->AdjustToParent(timeScale);
        }
        if (component.clouds) {
            component.clouds->AdjustToParent(timeScale);
        }
        for (const auto& renderableAtmosphere : component.atmospheres) {
            renderableAtmosphere.atmosphere->AdjustToParent();
        }
        if (component.planetaryRing) {
            component.planetaryRing->AdjustToParent();
        }

        // Shadow frustum tracks the planet's current heliocentric position.
        const float extent = component.planet->GetRadius() * 3.0f;
        const glm::mat4 lightProjection = glm::ortho(-extent, extent, -extent, extent, _camera.GetNear(), _camera.GetFar());
        const glm::mat4 lightView = glm::lookAt(_sun->GetPosition(), component.planet->GetPosition(), glm::vec3(0.0f, 1.0f, 0.0f));
        component.lightSpaceMatrix = lightProjection * lightView;

        ShadowMapPass(component);
        RenderPass(component);
    }
}

void Application::ShadowMapPass(const RenderableSceneComponent& component) {
#ifdef __EMSCRIPTEN__
    // XR presents into XRWebGLLayer's framebuffer (bound by JS). The normal
    // shadow pass rebinds FBO 0 on exit, which would break stereo output — skip
    // self-shadow maps in VR for now (lighting reads a cleared map as fully lit).
    if (_xr.active) {
        return;
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, _shadowMapFBO->GetFBO());
    glClear(GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _shadowMapFBO->GetShadowMapWidth(), _shadowMapFBO->GetShadowMapHeight());

    // A cleared depth texture contains 1.0 everywhere, which the existing
    // lighting shaders interpret as fully lit. Keep the texture bound but skip
    // all shadow geometry when shadows are disabled.
    if (gSimState->shadowQuality == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }

    _shadowMapShader->Use();
    _shadowMapShader->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);

    component.planet->SetShader(*_shadowMapShader);
    component.planet->Render();

    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*_shadowMapShader);
        satellite->Render();
    }

    RenderPlanetaryRing(*_shadowMapShader, component.planetaryRing.get(), component.lightSpaceMatrix);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Application::RenderPass(const RenderableSceneComponent& component) {
#ifdef __EMSCRIPTEN__
    if (_xr.active && _xr.currentEye >= 0 && _xr.currentEye < 2) {
        const auto& eye = _xr.eyes[_xr.currentEye];
        glViewport(eye.viewportX, eye.viewportY, eye.viewportWidth, eye.viewportHeight);
    } else
#endif
    {
        glViewport(0, 0, _displayWidth, _displayHeight);
    }
    _mainPlanetShader->Use();

    ConfigureMainPlanetShader(component);

    component.planet->SetShader(*_mainPlanetShader);
    component.planet->Render();

    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*_mainPlanetShader);
        satellite->Render();
    }

    if (_nearestPlanetIndex >= 0
        && static_cast<size_t>(_nearestPlanetIndex) < _renderableSceneComponents.size()
        && component.planet == _renderableSceneComponents[static_cast<size_t>(_nearestPlanetIndex)].planet)
        ProcessStarRendering();

    RenderAtmospheres(component.atmospheres, component.lightSpaceMatrix, component.planetaryRing.get());
    RenderClouds(component.clouds.get(), component.lightSpaceMatrix);
    RenderPlanetaryRing(*_mainRingShader, component.planetaryRing.get(), component.lightSpaceMatrix);
}

void Application::RenderAtmospheres(const std::vector<RenderableAtmosphere>& renderableAtmospheres, const glm::mat4& lightSpaceMatrix, const PlanetaryRing* ring) const {
    if (!renderableAtmospheres.empty()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        _mainAtmosphereShader->Use();
        _mainAtmosphereShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);

        for (const auto& renderableAtmosphere : renderableAtmospheres) {
            _mainAtmosphereShader->SetVec3("camPosition", _camera.GetPosition() - renderableAtmosphere.atmosphere->GetPosition());
            _mainAtmosphereShader->SetVec3("lightPos", _sun->GetPosition() - renderableAtmosphere.atmosphere->GetPosition());
            _mainAtmosphereShader->SetVec3("mieTint", renderableAtmosphere.atmosphere->GetMieTint());
            _mainAtmosphereShader->SetFloat("SCALE_H_FACTOR", renderableAtmosphere.hScaleFactor);
            _mainAtmosphereShader->SetFloat("SCALE_L_FACTOR", 1.0f);
            _mainAtmosphereShader->SetFloat("earthSizeCoefficient", renderableAtmosphere.parentEarthSizeCoefficient);
            _mainAtmosphereShader->SetBool("isUseToneMapping", renderableAtmosphere.isUseToneMapping);
            _mainAtmosphereShader->SetBool("isNearbyPlanetaryRing", ring != nullptr);

            if (ring) {
                _mainAtmosphereShader->SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
                _mainAtmosphereShader->SetFloat("ringParentPlanetRadiusSquared", ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
                _mainAtmosphereShader->SetBool("isUseSphereIntersect", ring->GetParent() != renderableAtmosphere.atmosphere->GetParent());

                _mainAtmosphereShader->SetVec3("ringCenter", ring->GetPosition());
                _mainAtmosphereShader->SetVec3("ringNormal", ring->GetRingNormal());
                _mainAtmosphereShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
                _mainAtmosphereShader->SetInt("ringDiffuse", 9);
                glBindTextureUnit(9, ring->GetRingTexture());
            }

            if (CalculateSpaceObjectDistance(renderableAtmosphere.atmosphere.get()) <= renderableAtmosphere.atmosphere->GetAtmosphereOuterBoundary())
                glFrontFace(GL_CW);

            renderableAtmosphere.atmosphere->Render();

            glFrontFace(GL_CCW);
        }

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Application::RenderClouds(Clouds* renderableClouds, const glm::mat4& lightSpaceMatrix) const {
    if (renderableClouds) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
        glDisable(GL_CULL_FACE);

        _mainCloudsShader->Use();
        _mainCloudsShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
        renderableClouds->Render();

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Application::RenderPlanetaryRing(const Shader& shader, PlanetaryRing* planetaryRing, const glm::mat4& lightSpaceMatrix) const {
    if (planetaryRing) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        shader.Use();
        shader.SetMat4("lightSpaceMatrix", lightSpaceMatrix);
        planetaryRing->SetShader(shader);
        planetaryRing->Render();

        glDisable(GL_BLEND);
    }
}

void Application::ProcessStarRendering() {
#ifdef __EMSCRIPTEN__
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    _sun->SetVisibility(1.0f);
#else
    glDepthMask(GL_FALSE);
    glBeginQuery(GL_SAMPLES_PASSED, _sun->GetStarOcclusionValue(0));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEndQuery(GL_SAMPLES_PASSED);

    glBeginQuery(GL_SAMPLES_PASSED, _sun->GetStarOcclusionValue(1));
    RenderStar();
    glEndQuery(GL_SAMPLES_PASSED);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    UpdateOcclusionQuery();
#endif
}

void Application::UpdateOcclusionQuery() {
    GLuint totalSamples = 0;
    GLuint visibleSamples = 0;
    glGetQueryObjectuiv(_sun->GetStarOcclusionValue(0), GL_QUERY_RESULT, &totalSamples);
    glGetQueryObjectuiv(_sun->GetStarOcclusionValue(1), GL_QUERY_RESULT, &visibleSamples);

    if (totalSamples == 0) {
        _sun->SetVisibility(0.0f);
        return;
    }

    _sun->SetVisibility(static_cast<float>(visibleSamples) / static_cast<float>(totalSamples));
}

void Application::RenderStarCorona() const {
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    _mainCoronaStarShader->Use();
    _sun->SetShader(*_mainCoronaStarShader);
    _sun->TakeStarSystemCenter();
    _sun->Render();
    _sun->SetShader(*_mainStarShader);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Application::RenderStar() const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    _mainStarShader->Use();
    _sun->TakeStarSystemCenter();
    _sun->Render();

    glDisable(GL_BLEND);
}

void Application::RenderStarEffects() const {
    const PlanetaryRing* nearestPlanetaryRing = nullptr;
    if (!_renderableSceneComponents.empty()
        && _nearestPlanetIndex >= 0
        && static_cast<size_t>(_nearestPlanetIndex) < _renderableSceneComponents.size()) {
        nearestPlanetaryRing = _renderableSceneComponents[static_cast<size_t>(_nearestPlanetIndex)].planetaryRing.get();
    }

    optional<RingCameraInfo> ringCameraInfo;
    if (nearestPlanetaryRing) {
        ringCameraInfo = {_camera.GetPosition(), nearestPlanetaryRing->GetPosition(), nearestPlanetaryRing->GetRingNormal(),
                          glm::vec2(nearestPlanetaryRing->GetInnerRadius(), nearestPlanetaryRing->GetOuterRadius()),
                          nearestPlanetaryRing->GetRingTexture()};
    }

    if (_hdrEnabled && _hdr && _hdr->IsEnabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, _hdr->GetHdrFBO());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        _sun->RenderGlow(_cameraProjection, _cameraView, _camera.GetFrontVector() - _camera.GetRightVector(), _camera.GetAspect(),
                         CalculateSpaceObjectDistance(_sun.get()), ringCameraInfo, _starTemperatureInKelvin);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        _sun->RenderGlow(_cameraProjection, _cameraView, _camera.GetFrontVector() - _camera.GetRightVector(), _camera.GetAspect(),
                         CalculateSpaceObjectDistance(_sun.get()), ringCameraInfo, _starTemperatureInKelvin);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    if (_hdrEnabled && _hdr && _hdr->IsEnabled()) {
        glBlendFunc(GL_ONE, GL_ONE);
        _hdr->Render(_starExposure, _starGamma);
    }

    glBlendFunc(GL_ONE, GL_ONE);
    float intensity = glm::min(_sun->GetCurrentGlowSize() * _sun->GetVisibility(), 1.0f);
    _lensFlare->Render(_cameraProjection, _cameraView, _sun->GetPosition(), glm::vec3(1.0), _camera.GetAspect(), 0.1, intensity, ringCameraInfo);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Application::RenderPlanetSatelliteStarDistances() const {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (_isRenderPlanetStarDistances)
        RenderSpaceObjectDistance(_sun.get());

    for(const auto& renderableComponentPS : _renderableSceneComponents) {
        if (_isRenderPlanetStarDistances) {
            RenderSpaceObjectDistance(renderableComponentPS.planet.get());
        }

        if (_isRenderSatelliteDistances) {
            for(const auto& satellite : renderableComponentPS.satellites) {
                RenderSpaceObjectDistance(satellite.get());
            }
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Application::RenderSpaceObjectDistance(const SpaceObject* spaceObject) const {
    // Reuse pre-allocated container to eliminate per-frame heap allocation
    _distanceInfoCache.clear();
    _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetEngName().begin(), spaceObject->GetEngName().end());

    if (!spaceObject->GetOtherLangName().empty()) {
        _distanceInfoCache.push_back(L'[');
        _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetOtherLangName().begin(), spaceObject->GetOtherLangName().end());
        _distanceInfoCache.push_back(L']');
        _distanceInfoCache.push_back(L' ');
    }

    wstring distance(to_wstring(static_cast<uint16_t>(CalculateSpaceObjectDistance(spaceObject))));
    _distanceInfoCache.insert(_distanceInfoCache.end(), make_move_iterator(distance.begin()), make_move_iterator(distance.end()));

    _mainTextShader->Use();
    _mainTextShader->SetVec3("particleCenterWorldSpace", spaceObject->GetPosition());
    _mainTextShader->SetBool("is3D", true);
    _textRenderer->Render(*_mainTextShader, _distanceInfoCache, 0.0, 0.0, 0.075, glm::vec3(0.98431, 0.80784, 0.69412));
}

void Application::RenderHints() const {
    static const glm::mat4 textProjection = glm::ortho(0.0f, static_cast<float>(_displayWidth), 0.0f, static_cast<float>(_displayHeight));
    static const string gpuHintString = string(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    static constexpr glm::vec3 textColor = glm::vec3(0.98431, 0.80784, 0.69412);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", textProjection);
    _mainTextShader->SetBool("is3D", false);

    // Reuse pre-allocated containers to eliminate per-frame heap allocations
    _fpsHintCache.clear();
    _fpsHintCache.emplace_back(L"FPS: ");
    _fpsHintCache.emplace_back(to_wstring(_fpsHandler.GetCurrentFps()));

    _gpuHintCache.clear();
    _gpuHintCache.emplace_back(wstring(gpuHintString.begin(), gpuHintString.end()));

    _soundVolumeHintCache.clear();
    stringstream soundVolumeStream;
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    soundVolumeStream << fixed << setprecision(0) << (_musicMuted ? 0.0f : _musicVolume) * 100.0;
#else
    soundVolumeStream << fixed << setprecision(0) << _soundEngine->getSoundVolume() * 100.0;
#endif
    string soundVolume = soundVolumeStream.str();
    _soundVolumeHintCache.emplace_back(wstring(L"Sound volume(PgUp/PgDown): ").append(soundVolume.begin(), soundVolume.end()).append(L" %"));

    _tmpStringCache.clear();
    _tmpStringCache.emplace_back(wstring(_currentMusicTrack.begin(), _currentMusicTrack.end()));

    deque<wstring> timeRunHint;
    timeRunHint.emplace_back(L"Time (P:pause, +/-:scale, .:step): ");
    std::wstringstream wss;
    wss << (gSimState->timePaused ? L"paused" : L"run") << L" x" << static_cast<int>(gSimState->timeScale);
    int year = 0;
    int month = 0;
    int day = 0;
    Ephemeris::YmdFromJulianDate(OrbitLayout::GetJulianDate(), year, month, day);
    wss << L" | " << year << L'-' << std::setfill(L'0') << std::setw(2) << month
        << L'-' << std::setw(2) << day;
    timeRunHint.emplace_back(wss.str());

    deque<wstring> planetStarHint;
    planetStarHint.emplace_back(L"Planet/Star distances(Z): ");
    planetStarHint.emplace_back((_isRenderPlanetStarDistances) ? L"On" : L"Off");

    deque<wstring> satelliteHint;
    satelliteHint.emplace_back(L"Satellite distances(X): ");
    satelliteHint.emplace_back((_isRenderSatelliteDistances) ? L"On" : L"Off");

#ifdef __EMSCRIPTEN__
    // Indicate low-res start + high-res streaming (visual in streaming-progress + on-screen when active)
    deque<wstring> textureHint;
    textureHint.emplace_back(L"Web: low-res start; high-res streams when close (see HUD)");
#endif

    deque<wstring> cameraSpeedHint;
    cameraSpeedHint.emplace_back(L"Camera speed(1/2): ");
    cameraSpeedHint.emplace_back(to_wstring(_camera.GetMovementSpeed()));

    deque<wstring> smoothCameraHint;
    smoothCameraHint.emplace_back(L"Smooth camera(Arrows)");

    deque<wstring> smoothZoomHint;
    smoothZoomHint.emplace_back(L"Smooth zoom(V/B)");

    deque<wstring> movementHint;
    movementHint.emplace_back(L"Move up/down(SPACE/C)");

    deque<wstring> speedBostHint;
    speedBostHint.emplace_back(L"Speed boost(SHIFT)");

    deque<wstring> starExposureHint;
    starExposureHint.emplace_back(L"Star Exposure(3/4): ");
    starExposureHint.emplace_back(to_wstring(_starExposure));

    deque<wstring> starGammaHint;
    starGammaHint.emplace_back(L"Star Gamma(5/6): ");
    starGammaHint.emplace_back(to_wstring(_starGamma));

    deque<wstring> starTemperatureHint;
    stringstream  starTemperatureStream;
    starTemperatureStream << fixed << setprecision(0) << _starTemperatureInKelvin;
    string starTemperatureStr = starTemperatureStream.str();
    starTemperatureHint.emplace_back(wstring(L"Star Temperature(7/8): ").append(make_move_iterator(starTemperatureStr.begin()),
                                                                                make_move_iterator(starTemperatureStr.end())));
    deque<wstring> vertSyncHint;
    vertSyncHint.emplace_back(L"Vert Sync(F1): ");
    vertSyncHint.emplace_back((_isVertSyncEnabled) ? L"On" : L"Off");

    deque<wstring> textHints;
    textHints.emplace_back(L"Text hints(TAB)");

    deque<wstring> magneticHint;
    magneticHint.emplace_back(L"Magnetic fields(M): ");
    magneticHint.emplace_back(_magneticFieldsEnabled ? L"On" : L"Off");

    _textRenderer->ReverseRender(*_mainTextShader, _tmpStringCache, 0.99 * _displayWidth, 0.95 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, _fpsHintCache, 0.01 * _displayWidth, 0.95 * _displayHeight, 0.35, CurrentFpsColor());
    _textRenderer->Render(*_mainTextShader, _gpuHintCache, 0.01 * _displayWidth, 0.925 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, _soundVolumeHintCache, 0.01 * _displayWidth, 0.9 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, timeRunHint, 0.01 * _displayWidth, 0.875 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, planetStarHint, 0.01 * _displayWidth, 0.85 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, satelliteHint, 0.01 * _displayWidth, 0.825 * _displayHeight, 0.35, textColor);
#ifdef __EMSCRIPTEN__
    _textRenderer->Render(*_mainTextShader, textureHint, 0.01 * _displayWidth, 0.80 * _displayHeight, 0.30, glm::vec3(0.6f, 0.8f, 1.0f));
#endif
    _textRenderer->Render(*_mainTextShader, cameraSpeedHint, 0.01 * _displayWidth, 0.8 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, smoothCameraHint, 0.01 * _displayWidth, 0.775 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, smoothZoomHint, 0.01 * _displayWidth, 0.75 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, movementHint, 0.01 * _displayWidth, 0.725 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, speedBostHint, 0.01 * _displayWidth, 0.7 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starExposureHint, 0.01 * _displayWidth, 0.675 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starGammaHint, 0.01 * _displayWidth, 0.65 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starTemperatureHint, 0.01 * _displayWidth, 0.625 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, vertSyncHint, 0.01 * _displayWidth, 0.6 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, textHints, 0.01 * _displayWidth, 0.575 * _displayHeight, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, magneticHint, 0.01 * _displayWidth, 0.55 * _displayHeight, 0.35, textColor);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Application::RenderTextureLoadingProgress() const {
    auto& queue = TextureLoadingQueue::GetInstance();
    const int queued    = queue.GetQueuedCount();
    const int completed = queue.GetTotalProcessed();
    const int total     = queue.GetTotalQueued();
    const int active    = queue.GetActiveLoadCount();
    const std::string& currentPath = queue.GetCurrentLoadingPath();
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

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    static const glm::mat4 textProjection = glm::ortho(0.0f, static_cast<float>(_displayWidth), 0.0f, static_cast<float>(_displayHeight));
    static constexpr glm::vec3 textColor = glm::vec3(0.3f, 0.8f, 1.0f);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", textProjection);
    _mainTextShader->SetBool("is3D", false);

    // Build hint string with progress counters, e.g. "Mid-res upgrade (2/5)"
    // Throttled render (via static) to avoid per-frame spam; visual is brief.
    static int lastCompleted = -1;
    static int lastTotal = -1;
    static int lastActive = -1;
    static int lastTier = -1;
    static int frameCounter = 0;
    frameCounter = (frameCounter + 1) % 10;
    if (frameCounter == 0 || completed != lastCompleted || total != lastTotal || active != lastActive ||
        tierCode != lastTier) {
        lastCompleted = completed;
        lastTotal = total;
        lastActive = active;
        lastTier = tierCode;
        const wchar_t* tierLabel = L"Texture upgrade";
        if (tierCode == 1) {
            tierLabel = L"Mid-res upgrade";
        } else if (tierCode == 2) {
            tierLabel = L"High-res upgrade";
        }
        std::wstring hint = std::wstring(tierLabel) + L" (" + std::to_wstring(completed) + L"/" +
                            std::to_wstring(total) + L")";
        if (active > 0) {
            hint += L" [" + std::to_wstring(active) + L" active]";
        }
        deque<wstring> loadingHint;
        loadingHint.emplace_back(hint);
        _textRenderer->Render(*_mainTextShader, loadingHint, 0.5f * _displayWidth - 150, 0.1f * _displayHeight, 0.25, textColor);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Application::ConfigureMainShaders() {
    static const double zCoef = 2.0 / glm::log2(_camera.GetFar() + 1.0);

#ifdef __EMSCRIPTEN__
    if (_xr.active && _xr.currentEye >= 0 && _xr.currentEye < _xr.eyeCount) {
        const auto& eye = _xr.eyes[_xr.currentEye];
        _cameraProjection = eye.projection;
        _cameraView = eye.view;
    } else
#endif
    {
        _cameraProjection = _camera.GetProjectionMatrix();
        _cameraView = _camera.GetViewMatrix();
    }

    // Skybox uses the active view's rotation; projection matches the eye frustum in XR.
    const glm::mat4 skyBoxProjection =
#ifdef __EMSCRIPTEN__
        (_xr.active) ? _cameraProjection :
#endif
        glm::perspective(glm::radians(45.0f), _camera.GetAspect(), _camera.GetNear(), _camera.GetFar());

    _mainSkyBoxShader->Use();
    _mainSkyBoxShader->SetMat4("view", glm::mat4(glm::mat3(_cameraView)));
    _mainSkyBoxShader->SetMat4("projection", skyBoxProjection);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _cameraProjection);
    _mainTextShader->SetMat4("view", _cameraView);
    _mainTextShader->SetInt("text", 0);

    _mainStarShader->Use();
    _mainStarShader->SetMat4("projection", _cameraProjection);
    _mainStarShader->SetMat4("view", _cameraView);
    _mainStarShader->SetVec3("centerDir", glm::normalize(_camera.GetPosition() - _sun->GetPosition()));
    _mainStarShader->SetVec3("shiftStarColor", _sun->GetShiftColor());
    _mainStarShader->SetVec3("colorMult", glm::vec3(0.96862745, 0.58039215, 0.235294117) * _sun->GetShiftColor());
    _mainStarShader->SetFloat("sunTemperatureInKelvin", _sun->GetStarTemperatureInKelvin());
    _mainStarShader->SetFloat("starRadiusInKilometers", _sun->GetStarRadius());
    _mainStarShader->SetFloat("zCoef", zCoef);
    _mainStarShader->SetFloat("uColorMap", _sun->GetTemperatureColorUCoordinate());
    _mainStarShader->SetBool("isVisible", _sun->GetVisibility() == 1.0);
    _mainStarShader->SetInt("colorMap", 0);
    glBindTextureUnit(0, _sun->GetStarSpectrumTexture());

    _mainCoronaStarShader->Use();
    _mainCoronaStarShader->SetMat4("projection", _cameraProjection);
    _mainCoronaStarShader->SetMat4("view", _cameraView);
    _mainCoronaStarShader->SetVec3("center", _sun->GetPosition());
    _mainCoronaStarShader->SetVec3("cameraRight", _camera.GetRightVector());
    _mainCoronaStarShader->SetVec3("cameraUp", _camera.GetUpVector());
    _mainCoronaStarShader->SetVec3("starShiftColor", _sun->GetShiftColor());
    _mainCoronaStarShader->SetFloat("zCoef", zCoef);
    _mainCoronaStarShader->SetFloat("maxSize", 7.1);
    _mainCoronaStarShader->SetFloat("starRadius", _sun->GetStarRadius());
    _mainCoronaStarShader->SetFloat("deltaTime", glfwGetTime() * 0.002);

    const float surfaceDim = _magneticFieldsEnabled ? 0.28f : 1.0f;
    const float atmosphereDim = _magneticFieldsEnabled ? 0.40f : 1.0f;
    const float ringDim = _magneticFieldsEnabled ? 0.35f : 1.0f;

    _mainPlanetShader->Use();
    _mainPlanetShader->SetMat4("projection", _cameraProjection);
    _mainPlanetShader->SetMat4("view", _cameraView);
    _mainPlanetShader->SetVec3("viewPos", _camera.GetPosition());
    _mainPlanetShader->SetVec3("lightPos", _sun->GetPosition());
    _mainPlanetShader->SetVec3("starGlowTint", _sun->GetGlowTintMult());
    _mainPlanetShader->SetFloat("farPlane", _camera.GetFar());
    _mainPlanetShader->SetFloat("zCoef", zCoef);
    _mainPlanetShader->SetFloat("bias", 0.0005);
    _mainPlanetShader->SetFloat("uSurfaceDim", surfaceDim);
    _mainPlanetShader->SetInt("shadowMap", 6);
    glBindTextureUnit(6, _shadowMapFBO->GetShadowMap());

    _mainAtmosphereShader->Use();
    _mainAtmosphereShader->SetMat4("projection", _cameraProjection);
    _mainAtmosphereShader->SetMat4("view", _cameraView);
    _mainAtmosphereShader->SetFloat("farPlane", _camera.GetFar());
    _mainAtmosphereShader->SetFloat("zCoef", zCoef);
    _mainAtmosphereShader->SetFloat("bias", 0.001);
    _mainAtmosphereShader->SetFloat("uSurfaceDim", atmosphereDim);
    _mainAtmosphereShader->SetInt("shadowMap", 11);
    glBindTextureUnit(11, _shadowMapFBO->GetShadowMap());

    _mainCloudsShader->Use();
    _mainCloudsShader->SetMat4("projection", _cameraProjection);
    _mainCloudsShader->SetMat4("view", _cameraView);
    _mainCloudsShader->SetVec3("viewPos", _camera.GetPosition());
    _mainCloudsShader->SetVec3("lightPos", _sun->GetPosition());
    _mainCloudsShader->SetFloat("farPlane", _camera.GetFar());
    _mainCloudsShader->SetFloat("zCoef", zCoef);
    _mainCloudsShader->SetFloat("bias", 0.001);
    _mainCloudsShader->SetFloat("uSurfaceDim", surfaceDim);
    _mainCloudsShader->SetInt("shadowMap", 8);
    glBindTextureUnit(8, _shadowMapFBO->GetShadowMap());

    _mainRingShader->Use();
    _mainRingShader->SetMat4("projection", _cameraProjection);
    _mainRingShader->SetMat4("view", _cameraView);
    _mainRingShader->SetVec3("lightPos", _sun->GetPosition());
    _mainRingShader->SetVec3("camPos", _camera.GetPosition());
    _mainRingShader->SetVec3("starGlowTint", _sun->GetGlowTintMult());
    _mainRingShader->SetFloat("zCoef", zCoef);
    _mainRingShader->SetFloat("bias", 0.001);
    _mainRingShader->SetFloat("uSurfaceDim", ringDim);
    _mainRingShader->SetInt("shadowMap", 5);
    glBindTextureUnit(5, _shadowMapFBO->GetShadowMap());
}
void Application::ConfigureMainPlanetShader(const RenderableSceneComponent& renderableComponent) {
    _mainPlanetShader->SetMat4("lightSpaceMatrix", renderableComponent.lightSpaceMatrix);
    _mainPlanetShader->SetBool("isNearbyPlanetaryRing", renderableComponent.planetaryRing != nullptr);

    if (renderableComponent.planetaryRing) {
        _mainPlanetShader->SetVec3("ringCenter", renderableComponent.planetaryRing->GetPosition());
        _mainPlanetShader->SetVec3("ringNormal", renderableComponent.planetaryRing->GetRingNormal());
        _mainPlanetShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(renderableComponent.planetaryRing->GetInnerRadius(), renderableComponent.planetaryRing->GetOuterRadius()));
        _mainPlanetShader->SetInt("ringDiffuse", 7);
        glBindTextureUnit(7, renderableComponent.planetaryRing->GetRingTexture());
    }
}
