// Shadow/color passes for planets, atmospheres, rings, clouds and text overlays, plus the
// GPU-resource setup they need. Renderer:: methods; see Renderer.h for what it owns versus
// what it reaches into Application for (camera, scene components, sun, display size, ...).
#include "Application.h"
#include "JsBridge.h"
#include "QualitySettings.h"
#include "ResourceManifest.h"
#include "Solar_System/EclipseCaster.h"
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

void Renderer::Init() {
    Application& app = _app;
    const auto qualitySettings = GetQualitySettings(
        gSimState->shadowQuality == 0 ? gSimState->qualityPreset : gSimState->shadowQuality - 1, gSimState->isMobileWeb);
    shadowMapFBO = make_unique<ShadowMapFBO>(qualitySettings.shadowResolution, qualitySettings.shadowResolution);
    hdrEnabled = qualitySettings.enableHdr;
    hdrShader = make_unique<Shader>("resource/shaders/passThrough.vs", "resource/shaders/hdr.fs");
    hdr = make_unique<HDR>(*hdrShader, app._displayWidth, app._displayHeight, hdrEnabled);
    // HDR turns itself off when the GPU cannot give it a complete float FBO; follow it,
    // otherwise the composite path would keep sampling an empty buffer.
    hdrEnabled = hdr->IsEnabled();
    LogQualityTier(qualitySettings, hdrEnabled, gSimState->shadowQuality);

    const vector<string> skyBoxFaces = GetSkyBoxFaces();

    skyBox = make_unique<SkyBox>(skyBoxFaces);
    mainTextShader = make_unique<Shader>("resource/shaders/text.vs", "resource/shaders/text.fs");
    textRenderer = make_unique<TextRenderer>(app._ft, "resource/fonts/Arial.ttf");
    FT_Done_FreeType(app._ft);
    shadowMapShader = make_unique<Shader>("resource/shaders/shadowMap.vs", "resource/shaders/shadowMap.fs");
    mainSkyBoxShader = make_unique<Shader>("resource/shaders/skyBox.vs", "resource/shaders/skyBox.fs");
    mainStarShader = make_unique<Shader>("resource/shaders/star.vs", "resource/shaders/star.fs");
    mainCoronaStarShader = make_unique<Shader>("resource/shaders/starCorona.vs", "resource/shaders/starCorona.fs");
    mainPlanetShader = make_unique<Shader>("resource/shaders/planetLighting.vs", "resource/shaders/planetLighting.fs");
    mainAtmosphereShader = make_unique<Shader>("resource/shaders/atmosphere.vs", "resource/shaders/atmosphere.fs");
    mainCloudsShader = make_unique<Shader>("resource/shaders/planetLighting.vs", "resource/shaders/cloudsLighting.fs");
    mainRingShader = make_unique<Shader>("resource/shaders/planetaryRingLighting.vs", "resource/shaders/planetaryRingLighting.fs");
    lensFlareShader = make_unique<Shader>("resource/shaders/lensFlare.vs", "resource/shaders/lensFlare.fs");
    lensFlare = make_unique<LensFlare>(*lensFlareShader, TextureImage2D("resource/textures_low/flares_bright_Low.dds"),
            FlaresInfo {4,
            {
                FlareSprite{false, 1.0, 7.0, 0},
                FlareSprite{false, 1.35, 0.3, 1},
                FlareSprite{false, 1.5, 0.4, 4},
                FlareSprite{false, 1.7, 0.6, 5},
                FlareSprite{false, 1.9, 1.2, 6},
                FlareSprite{false, 2.1, 0.4, 2},
                FlareSprite{false, 2.25, 0.2, 3},
                FlareSprite{false, 2.75, 2.0, 7}
            }});
    orbitPathRenderer = make_unique<OrbitPathRenderer>();
    xrPointerRenderer = make_unique<XrPointerRenderer>();
    magneticFieldRenderer = make_unique<MagneticFieldLineRenderer>();
    {
        const auto fieldQuality = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);
        magneticFieldBloom = make_unique<MagneticFieldBloom>(app._displayWidth, app._displayHeight,
                                                              fieldQuality.enableMagneticBloom);
    }
}

void Renderer::ProcessSceneComponentsRendering() {
    const float timeScale = gSimState->timePaused ? 0.0f : gSimState->timeScale;

    for (auto& component : _app._renderableSceneComponents) {
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
        const glm::mat4 lightProjection = glm::ortho(-extent, extent, -extent, extent, _app._camera.GetNear(), _app._camera.GetFar());
        const glm::mat4 lightView = glm::lookAt(_app._sun->GetPosition(), component.planet->GetPosition(), glm::vec3(0.0f, 1.0f, 0.0f));
        component.lightSpaceMatrix = lightProjection * lightView;

        ShadowMapPass(component);
        RenderPass(component);
    }
}

void Renderer::ShadowMapPass(const RenderableSceneComponent& component) {
    // Under WebXR the frame composites into the XRWebGLLayer framebuffer, not FBO 0, so
    // every exit below restores DefaultFramebuffer() instead of hardcoding 0. Getting
    // that wrong is what used to force the whole pass to be skipped in VR, leaving the
    // lighting shaders reading a cleared depth map as "fully lit".
#ifdef __EMSCRIPTEN__
    if (_app._xr.active && _app._xr.baseLayerFramebuffer == 0) {
        // No layer framebuffer to return to (JS could not register it). Fall back to the
        // old behaviour rather than composite the eyes into FBO 0; RenderXrStereoFrame
        // logs the reason once per session.
        return;
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFBO->GetFBO());
    glClear(GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, shadowMapFBO->GetShadowMapWidth(), shadowMapFBO->GetShadowMapHeight());

    // A cleared depth texture contains 1.0 everywhere, which the existing
    // lighting shaders interpret as fully lit. Keep the texture bound but skip
    // all shadow geometry when shadows are disabled.
    if (gSimState->shadowQuality == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, _app.DefaultFramebuffer());
        return;
    }

    shadowMapShader->Use();
    shadowMapShader->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);

    component.planet->SetShader(*shadowMapShader);
    component.planet->Render();

    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*shadowMapShader);
        satellite->Render();
    }

    RenderPlanetaryRing(*shadowMapShader, component.planetaryRing.get(), component.lightSpaceMatrix);
    glBindFramebuffer(GL_FRAMEBUFFER, _app.DefaultFramebuffer());
}

void Renderer::RenderPass(const RenderableSceneComponent& component) {
#ifdef __EMSCRIPTEN__
    if (_app._xr.active && _app._xr.currentEye >= 0 && _app._xr.currentEye < 2) {
        const auto& eye = _app._xr.eyes[_app._xr.currentEye];
        glViewport(eye.viewportX, eye.viewportY, eye.viewportWidth, eye.viewportHeight);
    } else
#endif
    {
        glViewport(0, 0, _app._displayWidth, _app._displayHeight);
    }
    mainPlanetShader->Use();

    ConfigureMainPlanetShader(component);

    component.planet->SetShader(*mainPlanetShader);
    component.planet->Render();

    // The moons share mainPlanetShader with their primary, so the umbra decal has to come
    // back off before they draw — otherwise the caster would shadow its own night side.
    mainPlanetShader->SetBool("hasEclipseCaster", false);
    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*mainPlanetShader);
        satellite->Render();
    }

    if (_app._nearestPlanetIndex >= 0
        && static_cast<size_t>(_app._nearestPlanetIndex) < _app._renderableSceneComponents.size()
        && component.planet == _app._renderableSceneComponents[static_cast<size_t>(_app._nearestPlanetIndex)].planet)
        ProcessStarRendering();

    RenderAtmospheres(component.atmospheres, component.lightSpaceMatrix, component.planetaryRing.get());
    RenderClouds(component.clouds.get(), component.lightSpaceMatrix);
    RenderPlanetaryRing(*mainRingShader, component.planetaryRing.get(), component.lightSpaceMatrix);
}

void Renderer::RenderAtmospheres(const std::vector<RenderableAtmosphere>& renderableAtmospheres, const glm::mat4& lightSpaceMatrix, const PlanetaryRing* ring) const {
    if (!renderableAtmospheres.empty()) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);

        mainAtmosphereShader->Use();
        mainAtmosphereShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);

        for (const auto& renderableAtmosphere : renderableAtmospheres) {
            mainAtmosphereShader->SetVec3("camPosition", _app._camera.GetPosition() - renderableAtmosphere.atmosphere->GetPosition());
            mainAtmosphereShader->SetVec3("lightPos", _app._sun->GetPosition() - renderableAtmosphere.atmosphere->GetPosition());
            mainAtmosphereShader->SetVec3("mieTint", renderableAtmosphere.atmosphere->GetMieTint());
            mainAtmosphereShader->SetFloat("SCALE_H_FACTOR", renderableAtmosphere.hScaleFactor);
            mainAtmosphereShader->SetFloat("SCALE_L_FACTOR", 1.0f);
            mainAtmosphereShader->SetFloat("earthSizeCoefficient", renderableAtmosphere.parentEarthSizeCoefficient);
            mainAtmosphereShader->SetBool("isUseToneMapping", renderableAtmosphere.isUseToneMapping);
            mainAtmosphereShader->SetBool("isNearbyPlanetaryRing", ring != nullptr);

            if (ring) {
                mainAtmosphereShader->SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
                mainAtmosphereShader->SetFloat("ringParentPlanetRadiusSquared", ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
                mainAtmosphereShader->SetBool("isUseSphereIntersect", ring->GetParent() != renderableAtmosphere.atmosphere->GetParent());

                mainAtmosphereShader->SetVec3("ringCenter", ring->GetPosition());
                mainAtmosphereShader->SetVec3("ringNormal", ring->GetRingNormal());
                mainAtmosphereShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
                mainAtmosphereShader->SetInt("ringDiffuse", 9);
                glBindTextureUnit(9, ring->GetRingTexture());
            }

            if (_app.CalculateSpaceObjectDistance(renderableAtmosphere.atmosphere.get()) <= renderableAtmosphere.atmosphere->GetAtmosphereOuterBoundary())
                glFrontFace(GL_CW);

            renderableAtmosphere.atmosphere->Render();

            glFrontFace(GL_CCW);
        }

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Renderer::RenderClouds(Clouds* renderableClouds, const glm::mat4& lightSpaceMatrix) const {
    if (renderableClouds) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
        glDisable(GL_CULL_FACE);

        mainCloudsShader->Use();
        mainCloudsShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
        renderableClouds->Render();

        glEnable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
}

void Renderer::RenderPlanetaryRing(const Shader& shader, PlanetaryRing* planetaryRing, const glm::mat4& lightSpaceMatrix) const {
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

void Renderer::ProcessStarRendering() {
#ifdef __EMSCRIPTEN__
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    _app._sun->SetVisibility(1.0f);
#else
    glDepthMask(GL_FALSE);
    glBeginQuery(GL_SAMPLES_PASSED, _app._sun->GetStarOcclusionValue(0));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar();
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEndQuery(GL_SAMPLES_PASSED);

    glBeginQuery(GL_SAMPLES_PASSED, _app._sun->GetStarOcclusionValue(1));
    RenderStar();
    glEndQuery(GL_SAMPLES_PASSED);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    UpdateOcclusionQuery();
#endif
}

void Renderer::UpdateOcclusionQuery() {
    GLuint totalSamples = 0;
    GLuint visibleSamples = 0;
    glGetQueryObjectuiv(_app._sun->GetStarOcclusionValue(0), GL_QUERY_RESULT, &totalSamples);
    glGetQueryObjectuiv(_app._sun->GetStarOcclusionValue(1), GL_QUERY_RESULT, &visibleSamples);

    if (totalSamples == 0) {
        _app._sun->SetVisibility(0.0f);
        return;
    }

    _app._sun->SetVisibility(static_cast<float>(visibleSamples) / static_cast<float>(totalSamples));
}

void Renderer::RenderStarCorona() const {
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    mainCoronaStarShader->Use();
    _app._sun->SetShader(*mainCoronaStarShader);
    _app._sun->TakeStarSystemCenter();
    _app._sun->Render();
    _app._sun->SetShader(*mainStarShader);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::RenderStar() const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    mainStarShader->Use();
    _app._sun->TakeStarSystemCenter();
    _app._sun->Render();

    glDisable(GL_BLEND);
}

void Renderer::RenderStarEffects() const {
    const PlanetaryRing* nearestPlanetaryRing = nullptr;
    if (!_app._renderableSceneComponents.empty()
        && _app._nearestPlanetIndex >= 0
        && static_cast<size_t>(_app._nearestPlanetIndex) < _app._renderableSceneComponents.size()) {
        nearestPlanetaryRing = _app._renderableSceneComponents[static_cast<size_t>(_app._nearestPlanetIndex)].planetaryRing.get();
    }

    optional<RingCameraInfo> ringCameraInfo;
    if (nearestPlanetaryRing) {
        ringCameraInfo = {_app._camera.GetPosition(), nearestPlanetaryRing->GetPosition(), nearestPlanetaryRing->GetRingNormal(),
                          glm::vec2(nearestPlanetaryRing->GetInnerRadius(), nearestPlanetaryRing->GetOuterRadius()),
                          nearestPlanetaryRing->GetRingTexture()};
    }

    if (hdrEnabled && hdr && hdr->IsEnabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, hdr->GetHdrFBO());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        _app._sun->RenderGlow(cameraProjection, cameraView, _app._camera.GetFrontVector() - _app._camera.GetRightVector(), _app._camera.GetAspect(),
                         _app.CalculateSpaceObjectDistance(_app._sun.get()), ringCameraInfo, starTemperatureInKelvin);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, _app.DefaultFramebuffer());
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        _app._sun->RenderGlow(cameraProjection, cameraView, _app._camera.GetFrontVector() - _app._camera.GetRightVector(), _app._camera.GetAspect(),
                         _app.CalculateSpaceObjectDistance(_app._sun.get()), ringCameraInfo, starTemperatureInKelvin);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, _app.DefaultFramebuffer());
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    if (hdrEnabled && hdr && hdr->IsEnabled()) {
        glBlendFunc(GL_ONE, GL_ONE);
        hdr->Render(starExposure, starGamma);
    }

    glBlendFunc(GL_ONE, GL_ONE);
    float intensity = glm::min(_app._sun->GetCurrentGlowSize() * _app._sun->GetVisibility(), 1.0f);
    lensFlare->Render(cameraProjection, cameraView, _app._sun->GetPosition(), glm::vec3(1.0), _app._camera.GetAspect(), 0.1, intensity, ringCameraInfo);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderPlanetSatelliteStarDistances() const {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (isRenderPlanetStarDistances)
        RenderSpaceObjectDistance(_app._sun.get());

    for(const auto& renderableComponentPS : _app._renderableSceneComponents) {
        if (isRenderPlanetStarDistances) {
            RenderSpaceObjectDistance(renderableComponentPS.planet.get());
        }

        if (isRenderSatelliteDistances) {
            for(const auto& satellite : renderableComponentPS.satellites) {
                RenderSpaceObjectDistance(satellite.get());
            }
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderSpaceObjectDistance(const SpaceObject* spaceObject) const {
    // Reuse pre-allocated container to eliminate per-frame heap allocation
    distanceInfoCache.clear();
    distanceInfoCache.insert(distanceInfoCache.end(), spaceObject->GetEngName().begin(), spaceObject->GetEngName().end());

    if (!spaceObject->GetOtherLangName().empty()) {
        distanceInfoCache.push_back(L'[');
        distanceInfoCache.insert(distanceInfoCache.end(), spaceObject->GetOtherLangName().begin(), spaceObject->GetOtherLangName().end());
        distanceInfoCache.push_back(L']');
        distanceInfoCache.push_back(L' ');
    }

    wstring distance(to_wstring(static_cast<uint16_t>(_app.CalculateSpaceObjectDistance(spaceObject))));
    distanceInfoCache.insert(distanceInfoCache.end(), make_move_iterator(distance.begin()), make_move_iterator(distance.end()));

    mainTextShader->Use();
    mainTextShader->SetVec3("particleCenterWorldSpace", spaceObject->GetPosition());
    mainTextShader->SetBool("is3D", true);
    textRenderer->Render(*mainTextShader, distanceInfoCache, 0.0, 0.0, 0.075, glm::vec3(0.98431, 0.80784, 0.69412));
}

void Renderer::RenderHints() const {
    static const glm::mat4 textProjection = glm::ortho(0.0f, static_cast<float>(_app._displayWidth), 0.0f, static_cast<float>(_app._displayHeight));
    static const string gpuHintString = string(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    static constexpr glm::vec3 textColor = glm::vec3(0.98431, 0.80784, 0.69412);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mainTextShader->Use();
    mainTextShader->SetMat4("projection", textProjection);
    mainTextShader->SetBool("is3D", false);

    // Reuse pre-allocated containers to eliminate per-frame heap allocations
    fpsHintCache.clear();
    fpsHintCache.emplace_back(L"FPS: ");
    fpsHintCache.emplace_back(to_wstring(_app._fpsHandler.GetCurrentFps()));

    gpuHintCache.clear();
    gpuHintCache.emplace_back(wstring(gpuHintString.begin(), gpuHintString.end()));

    soundVolumeHintCache.clear();
    stringstream soundVolumeStream;
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    soundVolumeStream << fixed << setprecision(0) << (_app._musicMuted ? 0.0f : _app._musicVolume) * 100.0;
#else
    soundVolumeStream << fixed << setprecision(0) << _app._soundEngine->getSoundVolume() * 100.0;
#endif
    string soundVolume = soundVolumeStream.str();
    soundVolumeHintCache.emplace_back(wstring(L"Sound volume(PgUp/PgDown): ").append(soundVolume.begin(), soundVolume.end()).append(L" %"));

    tmpStringCache.clear();
    tmpStringCache.emplace_back(wstring(_app._currentMusicTrack.begin(), _app._currentMusicTrack.end()));

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

    // Sky event driven by the ephemeris, not a baked animation — see SkyEvents.h.
    deque<wstring> conjunctionHint;
    if (const SkyEvents::SkyEvent& next = _app.GetNextSkyEvent(); next.valid) {
        const std::string line = SkyEvents::FormatEvent(next);
        conjunctionHint.emplace_back(line.begin(), line.end()); // ASCII only, see SkyEvents
    }

    deque<wstring> planetStarHint;
    planetStarHint.emplace_back(L"Planet/Star distances(Z): ");
    planetStarHint.emplace_back((isRenderPlanetStarDistances) ? L"On" : L"Off");

    deque<wstring> satelliteHint;
    satelliteHint.emplace_back(L"Satellite distances(X): ");
    satelliteHint.emplace_back((isRenderSatelliteDistances) ? L"On" : L"Off");

#ifdef __EMSCRIPTEN__
    // Indicate low-res start + high-res streaming (visual in streaming-progress + on-screen when active)
    deque<wstring> textureHint;
    textureHint.emplace_back(L"Web: low-res start; high-res streams when close (see HUD)");
#endif

    deque<wstring> cameraSpeedHint;
    cameraSpeedHint.emplace_back(L"Camera speed(1/2): ");
    cameraSpeedHint.emplace_back(to_wstring(_app._camera.GetMovementSpeed()));

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
    starExposureHint.emplace_back(to_wstring(starExposure));

    deque<wstring> starGammaHint;
    starGammaHint.emplace_back(L"Star Gamma(5/6): ");
    starGammaHint.emplace_back(to_wstring(starGamma));

    deque<wstring> starTemperatureHint;
    stringstream  starTemperatureStream;
    starTemperatureStream << fixed << setprecision(0) << starTemperatureInKelvin;
    string starTemperatureStr = starTemperatureStream.str();
    starTemperatureHint.emplace_back(wstring(L"Star Temperature(7/8): ").append(make_move_iterator(starTemperatureStr.begin()),
                                                                                make_move_iterator(starTemperatureStr.end())));
    deque<wstring> vertSyncHint;
    vertSyncHint.emplace_back(L"Vert Sync(F1): ");
    vertSyncHint.emplace_back((_app._isVertSyncEnabled) ? L"On" : L"Off");

    deque<wstring> textHints;
    textHints.emplace_back(L"Text hints(TAB)");

    deque<wstring> magneticHint;
    magneticHint.emplace_back(L"Magnetic fields(M): ");
    magneticHint.emplace_back(magneticFieldsEnabled ? L"On" : L"Off");

    textRenderer->ReverseRender(*mainTextShader, tmpStringCache, 0.99 * _app._displayWidth, 0.95 * _app._displayHeight, 0.35, textColor);
    if (!conjunctionHint.empty()) {
        textRenderer->ReverseRender(*mainTextShader, conjunctionHint, 0.99 * _app._displayWidth, 0.925 * _app._displayHeight, 0.35, textColor);
    }
    textRenderer->Render(*mainTextShader, fpsHintCache, 0.01 * _app._displayWidth, 0.95 * _app._displayHeight, 0.35, _app.CurrentFpsColor());
    textRenderer->Render(*mainTextShader, gpuHintCache, 0.01 * _app._displayWidth, 0.925 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, soundVolumeHintCache, 0.01 * _app._displayWidth, 0.9 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, timeRunHint, 0.01 * _app._displayWidth, 0.875 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, planetStarHint, 0.01 * _app._displayWidth, 0.85 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, satelliteHint, 0.01 * _app._displayWidth, 0.825 * _app._displayHeight, 0.35, textColor);
#ifdef __EMSCRIPTEN__
    textRenderer->Render(*mainTextShader, textureHint, 0.01 * _app._displayWidth, 0.80 * _app._displayHeight, 0.30, glm::vec3(0.6f, 0.8f, 1.0f));
#endif
    textRenderer->Render(*mainTextShader, cameraSpeedHint, 0.01 * _app._displayWidth, 0.8 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, smoothCameraHint, 0.01 * _app._displayWidth, 0.775 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, smoothZoomHint, 0.01 * _app._displayWidth, 0.75 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, movementHint, 0.01 * _app._displayWidth, 0.725 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, speedBostHint, 0.01 * _app._displayWidth, 0.7 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, starExposureHint, 0.01 * _app._displayWidth, 0.675 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, starGammaHint, 0.01 * _app._displayWidth, 0.65 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, starTemperatureHint, 0.01 * _app._displayWidth, 0.625 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, vertSyncHint, 0.01 * _app._displayWidth, 0.6 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, textHints, 0.01 * _app._displayWidth, 0.575 * _app._displayHeight, 0.35, textColor);
    textRenderer->Render(*mainTextShader, magneticHint, 0.01 * _app._displayWidth, 0.55 * _app._displayHeight, 0.35, textColor);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderTextureLoadingProgress() const {
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

    static const glm::mat4 textProjection = glm::ortho(0.0f, static_cast<float>(_app._displayWidth), 0.0f, static_cast<float>(_app._displayHeight));
    static constexpr glm::vec3 textColor = glm::vec3(0.3f, 0.8f, 1.0f);

    mainTextShader->Use();
    mainTextShader->SetMat4("projection", textProjection);
    mainTextShader->SetBool("is3D", false);

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
        textRenderer->Render(*mainTextShader, loadingHint, 0.5f * _app._displayWidth - 150, 0.1f * _app._displayHeight, 0.25, textColor);
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::ConfigureMainShaders() {
    static const double zCoef = 2.0 / glm::log2(_app._camera.GetFar() + 1.0);

#ifdef __EMSCRIPTEN__
    if (_app._xr.active && _app._xr.currentEye >= 0 && _app._xr.currentEye < _app._xr.eyeCount) {
        const auto& eye = _app._xr.eyes[_app._xr.currentEye];
        cameraProjection = eye.projection;
        cameraView = eye.view;
    } else
#endif
    {
        cameraProjection = _app._camera.GetProjectionMatrix();
        cameraView = _app._camera.GetViewMatrix();
    }

    // Skybox uses the active view's rotation; projection matches the eye frustum in XR.
    const glm::mat4 skyBoxProjection =
#ifdef __EMSCRIPTEN__
        (_app._xr.active) ? cameraProjection :
#endif
        glm::perspective(glm::radians(45.0f), _app._camera.GetAspect(), _app._camera.GetNear(), _app._camera.GetFar());

    mainSkyBoxShader->Use();
    mainSkyBoxShader->SetMat4("view", glm::mat4(glm::mat3(cameraView)));
    mainSkyBoxShader->SetMat4("projection", skyBoxProjection);

    mainTextShader->Use();
    mainTextShader->SetMat4("projection", cameraProjection);
    mainTextShader->SetMat4("view", cameraView);
    mainTextShader->SetInt("text", 0);

    mainStarShader->Use();
    mainStarShader->SetMat4("projection", cameraProjection);
    mainStarShader->SetMat4("view", cameraView);
    mainStarShader->SetVec3("centerDir", glm::normalize(_app._camera.GetPosition() - _app._sun->GetPosition()));
    mainStarShader->SetVec3("shiftStarColor", _app._sun->GetShiftColor());
    mainStarShader->SetVec3("colorMult", glm::vec3(0.96862745, 0.58039215, 0.235294117) * _app._sun->GetShiftColor());
    mainStarShader->SetFloat("sunTemperatureInKelvin", _app._sun->GetStarTemperatureInKelvin());
    mainStarShader->SetFloat("starRadiusInKilometers", _app._sun->GetStarRadius());
    mainStarShader->SetFloat("zCoef", zCoef);
    mainStarShader->SetFloat("uColorMap", _app._sun->GetTemperatureColorUCoordinate());
    mainStarShader->SetBool("isVisible", _app._sun->GetVisibility() == 1.0);
    mainStarShader->SetInt("colorMap", 0);
    glBindTextureUnit(0, _app._sun->GetStarSpectrumTexture());

    mainCoronaStarShader->Use();
    mainCoronaStarShader->SetMat4("projection", cameraProjection);
    mainCoronaStarShader->SetMat4("view", cameraView);
    mainCoronaStarShader->SetVec3("center", _app._sun->GetPosition());
    mainCoronaStarShader->SetVec3("cameraRight", _app._camera.GetRightVector());
    mainCoronaStarShader->SetVec3("cameraUp", _app._camera.GetUpVector());
    mainCoronaStarShader->SetVec3("starShiftColor", _app._sun->GetShiftColor());
    mainCoronaStarShader->SetFloat("zCoef", zCoef);
    mainCoronaStarShader->SetFloat("maxSize", 7.1);
    mainCoronaStarShader->SetFloat("starRadius", _app._sun->GetStarRadius());
    mainCoronaStarShader->SetFloat("deltaTime", glfwGetTime() * 0.002);

    const float surfaceDim = magneticFieldsEnabled ? 0.28f : 1.0f;
    const float atmosphereDim = magneticFieldsEnabled ? 0.40f : 1.0f;
    const float ringDim = magneticFieldsEnabled ? 0.35f : 1.0f;

    mainPlanetShader->Use();
    mainPlanetShader->SetMat4("projection", cameraProjection);
    mainPlanetShader->SetMat4("view", cameraView);
    mainPlanetShader->SetVec3("viewPos", _app._camera.GetPosition());
    mainPlanetShader->SetVec3("lightPos", _app._sun->GetPosition());
    mainPlanetShader->SetVec3("starGlowTint", _app._sun->GetGlowTintMult());
    mainPlanetShader->SetFloat("farPlane", _app._camera.GetFar());
    mainPlanetShader->SetFloat("zCoef", zCoef);
    mainPlanetShader->SetFloat("bias", 0.0005);
    mainPlanetShader->SetFloat("uSurfaceDim", surfaceDim);
    mainPlanetShader->SetInt("shadowMap", 6);
    glBindTextureUnit(6, shadowMapFBO->GetShadowMap());

    mainAtmosphereShader->Use();
    mainAtmosphereShader->SetMat4("projection", cameraProjection);
    mainAtmosphereShader->SetMat4("view", cameraView);
    mainAtmosphereShader->SetFloat("farPlane", _app._camera.GetFar());
    mainAtmosphereShader->SetFloat("zCoef", zCoef);
    mainAtmosphereShader->SetFloat("bias", 0.001);
    mainAtmosphereShader->SetFloat("uSurfaceDim", atmosphereDim);
    mainAtmosphereShader->SetInt("shadowMap", 11);
    glBindTextureUnit(11, shadowMapFBO->GetShadowMap());

    mainCloudsShader->Use();
    mainCloudsShader->SetMat4("projection", cameraProjection);
    mainCloudsShader->SetMat4("view", cameraView);
    mainCloudsShader->SetVec3("viewPos", _app._camera.GetPosition());
    mainCloudsShader->SetVec3("lightPos", _app._sun->GetPosition());
    mainCloudsShader->SetFloat("farPlane", _app._camera.GetFar());
    mainCloudsShader->SetFloat("zCoef", zCoef);
    mainCloudsShader->SetFloat("bias", 0.001);
    mainCloudsShader->SetFloat("uSurfaceDim", surfaceDim);
    mainCloudsShader->SetInt("shadowMap", 8);
    glBindTextureUnit(8, shadowMapFBO->GetShadowMap());

    mainRingShader->Use();
    mainRingShader->SetMat4("projection", cameraProjection);
    mainRingShader->SetMat4("view", cameraView);
    mainRingShader->SetVec3("lightPos", _app._sun->GetPosition());
    mainRingShader->SetVec3("camPos", _app._camera.GetPosition());
    mainRingShader->SetVec3("starGlowTint", _app._sun->GetGlowTintMult());
    mainRingShader->SetFloat("zCoef", zCoef);
    mainRingShader->SetFloat("bias", 0.001);
    mainRingShader->SetFloat("uSurfaceDim", ringDim);
    mainRingShader->SetInt("shadowMap", 5);
    glBindTextureUnit(5, shadowMapFBO->GetShadowMap());
}
void Renderer::ConfigureEclipseUmbra(const RenderableSceneComponent& renderableComponent) {
    // Real physical constants, because the eclipse *decision* has to be made at true scale.
    // The scene draws body radii, moon orbits, and planet orbits at three different
    // exaggerations; at art scale the Moon sits 12.5 Earth-radii away instead of 60, so its
    // 5.1 deg of inclination cannot clear Earth's disc and a shadow would land on nearly
    // every new moon. Deciding in kilometres and only *drawing* at art scale is what keeps
    // eclipses as rare as they are. See docs/ARCHITECTURE.md § 11.
    constexpr float kEarthRadiusKm = 6371.0f;
    constexpr float kSunRadiusKm = 695700.0f;
    constexpr float kAuKm = 149597870.7f;

    const std::shared_ptr<Planet>& planet = renderableComponent.planet;
    if (!planet || !_app._sun || renderableComponent.satellites.empty()) {
        mainPlanetShader->SetBool("hasEclipseCaster", false);
        return;
    }

    const glm::vec3 planetPosition = planet->GetPosition();
    const glm::vec3 sunPosition = _app._sun->GetPosition();
    const float planetRadiusKm = planet->GetEarthSizeCoefficient() * kEarthRadiusKm;

    // Pick the moon whose shadow axis passes closest to the planet's centre. Only one
    // caster is uploaded: two simultaneous umbrae on the same body are rare enough that
    // paying for an array every frame would be the wrong trade.
    const Satellite* caster = nullptr;
    float casterUnitsPerKm = 0.0f;
    float bestMiss = 0.0f;
    for (const std::shared_ptr<Satellite>& satellite : renderableComponent.satellites) {
        // A circular art orbit lies exactly in its parent's equatorial plane, so it would
        // eclipse every revolution. Only ephemeris-placed moons are allowed to cast.
        if (!satellite || !satellite->IsEphemerisPlaced()) {
            continue;
        }
        const float unitsPerKm = satellite->OrbitSceneUnitsPerKm();
        if (unitsPerKm <= 0.0f) {
            continue;
        }

        const float miss =
            EclipseCaster::ShadowAxisMissDistance(planetPosition, sunPosition, satellite->GetPosition());
        if (miss < 0.0f) {
            continue;
        }

        // True radii expressed in this moon's orbital scale — the frame `miss` is already in.
        const float moonRadiusKm = satellite->GetEarthSizeCoefficient() * kEarthRadiusKm;
        const float limit = (planetRadiusKm + moonRadiusKm) * unitsPerKm;
        if (miss < limit && (!caster || miss < bestMiss)) {
            caster = satellite.get();
            casterUnitsPerKm = unitsPerKm;
            bestMiss = miss;
        }
    }

    if (!caster) {
        mainPlanetShader->SetBool("hasEclipseCaster", false);
        return;
    }

    // Draw radius: the moon's real radius in its own orbital scale, not the exaggerated
    // radius it is rendered at. That keeps the shadow spot's size sane relative to the
    // planet's disc instead of blanketing a quarter of it.
    const float casterRadius = caster->GetEarthSizeCoefficient() * kEarthRadiusKm * casterUnitsPerKm;

    // The shader only uses the star radius through its angular size at the caster, so pass
    // the radius that reproduces the Sun's *true* angular size at the scene's light
    // distance. Using the drawn Sun radius would widen the penumbra by the full art-scale
    // mismatch between the moon orbit and the planet orbit.
    const float lightDistance = glm::length(sunPosition - planetPosition);
    const float starRadius = lightDistance * (kSunRadiusKm / kAuKm);

    // Low drops the penumbra entirely: a zero-radius star gives a hard-edged disc, which is
    // one smoothstep and no extra bandwidth.
    const bool softPenumbra = gSimState->qualityPreset >= 2;

    mainPlanetShader->SetBool("hasEclipseCaster", true);
    mainPlanetShader->SetVec3("eclipseCasterCenter", caster->GetPosition());
    mainPlanetShader->SetFloat("eclipseCasterRadius", casterRadius);
    mainPlanetShader->SetFloat("eclipseStarRadius", softPenumbra ? starRadius : 0.0f);
}

void Renderer::ConfigureMainPlanetShader(const RenderableSceneComponent& renderableComponent) {
    mainPlanetShader->SetMat4("lightSpaceMatrix", renderableComponent.lightSpaceMatrix);
    mainPlanetShader->SetBool("isNearbyPlanetaryRing", renderableComponent.planetaryRing != nullptr);
    ConfigureEclipseUmbra(renderableComponent);

    if (renderableComponent.planetaryRing) {
        mainPlanetShader->SetVec3("ringCenter", renderableComponent.planetaryRing->GetPosition());
        mainPlanetShader->SetVec3("ringNormal", renderableComponent.planetaryRing->GetRingNormal());
        mainPlanetShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(renderableComponent.planetaryRing->GetInnerRadius(), renderableComponent.planetaryRing->GetOuterRadius()));
        mainPlanetShader->SetInt("ringDiffuse", 7);
        glBindTextureUnit(7, renderableComponent.planetaryRing->GetRingTexture());
    }
}
