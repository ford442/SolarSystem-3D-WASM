// Application's own share of each frame (docs/ARCHITECTURE.md §7.5): every body placed for the
// frame's date with its light box (UpdateScene), the views and the scene snapshot the passes
// read (BuildFrameSnapshot), frustum culling, and RenderFrame, which runs the modules
// (IAppModule.h) and hands the renderer its frame graph's inputs.
#include "Application.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "IAppModule.h"
#include "Solar_System/OrbitLayout.h"
#include "Solar_System/StarSystemCatalog.h"
#include "core/FrameProfile.h"
#include "core/Log.h"
#include "gfx/Frustum.h"
#include "gfx/ShadowFit.h"
#include <algorithm>
#include <cstddef>
#include <vector>

void Application::UpdateScene(double julianDate) {
    _renderer.CountSceneUpdate();
    RefreshPlanetProxyPositions();
    if (_sun) {
        _sun->TakeStarSystemCenter();
    }
    PlaceSceneComponents();

    for (auto& component : _renderableSceneComponents) {
        // Shadow frustum tracks the planet's current heliocentric position. Its depth range
        // is fitted to the component's own bodies — the only casters drawn into its map and
        // the only receivers that read it — instead of the camera's 0.001–20000, which
        // wasted precision and clipped everything past 20000 units from the Sun (Uranus and
        // beyond on realistic scale).
        const float radius = component.planet->GetRadius();
        const float fullExtent = radius * 3.0f;
        std::vector<BoundingSphere> bodies;
        component.ForEachObject([&](const SpaceObject& object) { bodies.push_back(object.GetBoundingSphere()); });
        const glm::vec3 toPlanet = component.planet->GetPosition() - _sun->GetPosition();
        const DepthRange depth = FitDepthRange(_sun->GetPosition(), glm::normalize(toPlanet), bodies,
                                               0.01f * fullExtent + 0.05f, _camera.GetNear());
        component.shadowDepthRange = glm::vec2(depth.nearPlane, depth.farPlane - depth.nearPlane);

        // Built in double: the eye is the Sun, tens of thousands of units from the render
        // origin on realistic scale, and a float lookAt folds that into the light-space x/y
        // the shadow lookup reads. In double those reduce to the planet's own small offset.
        const glm::dvec3 lightEye(_sun->GetPosition());
        const glm::dvec3 lightTarget(component.planet->GetPosition());
        const glm::mat4 lightView(glm::lookAt(lightEye, lightTarget, glm::dvec3(0.0, 1.0, 0.0)));

        const ShadowFit::Box box = FitShadowBox(component, ShadowFit::LightLookAt(lightEye, lightTarget), fullExtent);
        const float extent = static_cast<float>(box.halfExtent);
        const glm::mat4 lightProjection =
            glm::ortho(static_cast<float>(box.centerX) - extent, static_cast<float>(box.centerX) + extent,
                       static_cast<float>(box.centerY) - extent, static_cast<float>(box.centerY) + extent,
                       depth.nearPlane, depth.farPlane);
        component.lightSpaceMatrix = lightProjection * lightView;
    }

    // The belts and the probes are Sol's: another star system has neither.
    const bool sol = StarSystemCatalog::IsSol();
    if (_asteroidField && sol) {
        _asteroidField->Update(julianDate);
    }
    if (_kuiperField && sol) {
        _kuiperField->Update(julianDate);
    }
    UpdateLOD();
    UpdateSearchNearestPlanet();
    // Warm the lazily searched caches here so no pass is the one that pays for a search.
    if (sol) {
        GetNextSkyEvent();
    }
    if (_observe.active) {
        CurrentObserveSky();
    }
}

ShadowFit::Box Application::FitShadowBox(RenderableSceneComponent& component, const ShadowFit::LightBasis& light,
                                         float fullExtent) {
    const ShadowFit::Box full{0.0, 0.0, static_cast<double>(fullExtent)};
    // The cap is the camera's own frustum: an XR eye pair has two, so it keeps the full box.
    const double radius = component.planet->GetRadius();
    const glm::dvec3 center(component.planet->GetPosition());
    const glm::dvec3 eye(CameraRenderPosition());
    bool xrActive = false;
#ifdef __EMSCRIPTEN__
    xrActive = _xr.active;
#endif
    const bool shadows = _simState.shadowQuality > 0 && !xrActive && radius > 0.0;
    component.shadowVisibleCap =
        shadows && ShadowFit::UseVisibleCap(component.shadowVisibleCap, glm::distance(eye, center) / radius);

    ShadowFit::Box box = full;
    if (component.shadowVisibleCap) {
        ShadowFit::View view;
        view.eye = eye;
        view.front = glm::dvec3(_camera.GetFrontVector());
        view.right = glm::dvec3(_camera.GetRightVector());
        view.up = glm::dvec3(_camera.GetUpVector());
        view.tanHalfFovY = std::tan(glm::radians(static_cast<double>(_camera.GetZoom())) * 0.5);
        view.aspect = _camera.GetAspect();

        // Receivers the old box covered that stand clear of the sphere: the ring and any moon
        // in the frustum. (Shells hug the planet; the sphere stands for them.)
        const Frustum frustum(_camera.GetProjectionMatrix() * _camera.GetViewMatrix(glm::vec3(eye)));
        std::vector<BoundingSphere> extras;
        const auto addIfSeen = [&](const SpaceObject& object) {
            const BoundingSphere sphere = object.GetBoundingSphere();
            const glm::dvec2 offset = light.ToLight(glm::dvec3(sphere.center));
            if (frustum.Intersects(sphere) && glm::length(offset) - sphere.radius < fullExtent) {
                extras.push_back(sphere);
            }
        };
        for (const auto& satellite : component.satellites) {
            addIfSeen(*satellite);
        }
        if (component.planetaryRing) {
            addIfSeen(*component.planetaryRing);
        }

        const ShadowFit::Box cap = ShadowFit::FitVisibleCap(light, view, center, radius, extras);
        if (cap.halfExtent >= 0.0) {
            const ShadowFit::Box fitted = ShadowFit::Quantize(cap, ShadowFit::kMinCapRadii * radius);
            if (fitted.halfExtent < full.halfExtent) {
                box = fitted;
            }
        }
    }

    if (static_cast<float>(box.halfExtent) != component.shadowHalfExtent) {
        LOG_DEBUG("ShadowFit", "light box half-extent %.6g scene units (%s, full %.6g)", box.halfExtent,
                  component.shadowVisibleCap ? "visible cap" : "full", static_cast<double>(fullExtent));
        component.shadowHalfExtent = static_cast<float>(box.halfExtent);
    }
    return box;
}

void Application::PlaceBodies() {
    if (_sun) {
        _sun->TakeStarSystemCenter();
    }
    PlaceSceneComponents();
}

void Application::PlaceSceneComponents() {
    const bool physicalAtmosphere = _renderer.PhysicalAtmosphereAvailable();
    for (auto& component : _renderableSceneComponents) {
        component.planet->AdjustToParent();
        for (const auto& satellite : component.satellites) {
            satellite->AdjustToParent();
        }
        if (component.clouds) {
            component.clouds->AdjustToParent();
        }
        if (component.aurora) {
            component.aurora->AdjustToParent();
        }
        for (const auto& renderableAtmosphere : component.atmospheres) {
            // Before AdjustToParent: the two paths draw different shell sizes.
            renderableAtmosphere.atmosphere->SetPhysicalPathActive(physicalAtmosphere);
            renderableAtmosphere.atmosphere->AdjustToParent();
        }
        if (component.planetaryRing) {
            component.planetaryRing->AdjustToParent();
        }
    }
}

void Application::RenderFrame(int viewCount) {
    _profiler.Report(_renderer.Stats());
    _frameViewCount = viewCount;
    // Simulate once (an XR frame draws twice), then the modules, then the frame as a graph.
    _profiler.Time(FrameProfile::Stage::Update, [&] {
        _renderer.BeginFrame();
        UpdateScene(OrbitLayout::GetJulianDate());
        BuildFrameSnapshot(viewCount);
        CullScene(viewCount);
    });
    for (IAppModule* module : _modules) {
        _profiler.Time(module->Stage(), [&] {
            module->Update(_simState);
            module->Contribute(_frameScene);
        });
    }
    _profiler.Time(FrameProfile::Stage::SpaceWeather, [&] { _renderer.PrepareFrame(FrameContextFor(0)); });

    _frameContexts.clear();
    for (int view = 0; view < viewCount; ++view) {
        _frameContexts.push_back(FrameContextFor(view));
    }
    _renderer.DescribeFrame(FrameContextFor(0), viewCount, _frameFeatures);
    _frameFeatures.loadingProgress = !_frameFeatures.xr && viewCount > 0 && !_cinematics.Active();
    _renderer.RenderFrame(_frameContexts, _frameFeatures);
}

void Application::CullScene(int viewCount) {
    std::vector<Frustum> frusta;
    for (int view = 0; view < viewCount; ++view) {
        frusta.emplace_back(_frameViews[static_cast<size_t>(view)].projection *
                            _frameViews[static_cast<size_t>(view)].view);
    }
    const auto seen = [&](const SpaceObject& object) {
        const BoundingSphere sphere = object.GetBoundingSphere();
        return std::any_of(frusta.begin(), frusta.end(), [&](const Frustum& f) { return f.Intersects(sphere); });
    };

    _drawOrder.clear();
    int culled = 0;
    for (size_t i = 0; i < _renderableSceneComponents.size(); ++i) {
        RenderableSceneComponent& component = _renderableSceneComponents[i];
        bool any = false;
        component.ForEachObject([&](SpaceObject& object) {
            const bool visible = seen(object);
            object.SetVisible(visible);
            any = any || visible;
        });
        component.visible = any;
        culled += any ? 0 : 1;
        if (any || static_cast<int>(i) == _nearestPlanetIndex) {
            _drawOrder.push_back(i);
        }
    }
    _renderer.CountCulledComponents(culled);

    // Opaque surfaces front to back, so early depth rejects what is behind. Within a
    // component the order (planet, moons, shells, ring) stays as RenderPass has it.
    const glm::vec3 eye = CameraRenderPosition();
    const auto distance = [&](size_t index) {
        return glm::distance(eye, _renderableSceneComponents[index].planet->GetPosition());
    };
    std::stable_sort(_drawOrder.begin(), _drawOrder.end(),
                     [&](size_t a, size_t b) { return distance(a) < distance(b); });
}

void Application::BuildFrameSnapshot(int viewCount) {
    SceneSnapshot& scene = _frameScene;
    scene.sun = _sun.get();
    scene.components = &_renderableSceneComponents;
    scene.drawOrder = &_drawOrder; // CullScene fills it next
    scene.nearestComponent = _nearestPlanetIndex;
    const bool sol = StarSystemCatalog::IsSol();
    scene.asteroidField = sol ? _asteroidField.get() : nullptr;
    scene.kuiperField = sol ? _kuiperField.get() : nullptr;
    // Missions, terrain and space weather are the modules' (IAppModule::Contribute).
    scene.stagedSystems = &_planetSystemManifests;
    scene.observeActive = _observe.active;
    scene.observe = &_observe;
    scene.observeSky = _observe.active ? &CurrentObserveSky() : nullptr;
    scene.observeMoonTexture = _observe.active ? ObserveMoonTexture() : 0u;
    scene.observeEarthAtmosphere = _observe.active ? ObserveEarthAtmosphere() : nullptr;
    scene.nextSkyEvent = &GetNextSkyEvent();

    // The head: the mono camera, and the axes every eye shares.
    CameraView head;
    head.projection = _camera.GetProjectionMatrix();
    head.headPosition = CameraRenderPosition();
    head.view = _camera.GetViewMatrix(head.headPosition);
    head.renderOrigin = OrbitLayout::GetRenderOrigin();
    head.front = _camera.GetFrontVector();
    head.right = _camera.GetRightVector();
    head.up = _camera.GetUpVector();
    head.nearPlane = _camera.GetNear();
    head.farPlane = _camera.GetFar();
    head.aspect = _camera.GetAspect();
    head.zoomDeg = _camera.GetZoom();
    head.yawDeg = _camera.GetYaw();
    head.pitchDeg = _camera.GetPitch();
    head.logDepthC = _cameraController.LogDepthC();
    head.zCoef = static_cast<float>(2.0 / glm::log2(static_cast<double>(head.logDepthC) * _camera.GetFar() + 1.0));
    head.viewport = glm::ivec4(0, 0, _displayWidth, _displayHeight);

    const int views = std::clamp(viewCount, 1, static_cast<int>(_frameViews.size()));
    for (int i = 0; i < views; ++i) {
        CameraView& view = _frameViews[static_cast<size_t>(i)];
        view = head;
#ifdef __EMSCRIPTEN__
        if (_xr.active) {
            const XrEyeState& eye = _xr.eyes[i];
            view.projection = eye.projection;
            view.view = eye.view;
            view.viewport = glm::ivec4(eye.viewportX, eye.viewportY, eye.viewportWidth, eye.viewportHeight);
            view.eye = i;
        }
#endif
        view.eyePosition = glm::vec3(glm::inverse(view.view)[3]);
    }

    HudState& hud = _frameHud;
    hud.fps = _cinematics.FrozenHud() ? uint16_t{60} : _frameTimer.GetCurrentFps();
    hud.fpsColor = _cinematics.FrozenHud() ? glm::vec3(1.0f) : CurrentFpsColor();
    hud.vsync = _isVertSyncEnabled;
    hud.cameraSpeed = _camera.GetMovementSpeed();
#ifdef SOLARSYSTEM_USE_SDL_MIXER
    hud.musicVolumePercent = (_musicMuted ? 0.0f : _musicVolume) * 100.0f;
#elif !defined(__EMSCRIPTEN__)
    hud.musicVolumePercent = _soundEngine ? _soundEngine->getSoundVolume() * 100.0f : 0.0f;
#endif
    hud.musicTrack = _currentMusicTrack;
    hud.fadeColor = _screenFadeColor;
    hud.fadeAmount = _screenFadeAmount;
}

FrameContext Application::FrameContextFor(int view) const {
    const size_t index = static_cast<size_t>(std::clamp(view, 0, static_cast<int>(_frameViews.size()) - 1));
#ifdef __EMSCRIPTEN__
    const bool xr = _xr.active;
#else
    const bool xr = false;
#endif
    return FrameContext{_frameViews[index],   _frameScene, _simState, DisplaySize(), xr,
                        DefaultFramebuffer(), &_frameHud};
}
