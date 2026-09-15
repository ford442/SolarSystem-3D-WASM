// Non-body scene overlays drawn on top of the planets: magnetic field-line ribbons
// (with their optional bloom pass), orbit path rings and the asteroid belt, plus the
// toggles that gate them.
#include "Application.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include <algorithm>
#include <iostream>
#include <string>

using namespace std;

void Application::SetOrbitLinesEnabled(bool enabled) {
    _orbitLinesEnabled = enabled;
}

bool Application::GetOrbitLinesEnabled() const {
    return _orbitLinesEnabled;
}

void Application::SetMagneticFieldsEnabled(bool enabled) {
    _magneticFieldsEnabled = enabled;
    if (enabled) {
        EnsureMagneticFieldsBuilt();
    }
}

bool Application::GetMagneticFieldsEnabled() const {
    return _magneticFieldsEnabled;
}

void Application::ForEachEnabledMagneticField(
    const std::function<void(const SpaceObject& object, const MagneticFieldParams& params)>& fn) const {
    if (!fn) {
        return;
    }
    if (_sun && _sun->HasMagneticField()) {
        fn(*_sun, _sun->GetMagneticField());
    }
    for (const auto& component : _renderableSceneComponents) {
        if (component.planet && component.planet->HasMagneticField()) {
            fn(*component.planet, component.planet->GetMagneticField());
        }
        for (const auto& satellite : component.satellites) {
            if (satellite && satellite->HasMagneticField()) {
                fn(*satellite, satellite->GetMagneticField());
            }
        }
    }
}

void Application::EnsureMagneticFieldsBuilt() {
    if (!_magneticFieldRenderer) {
        return;
    }
    if (_magneticFieldsBuilt && _magneticFieldsQuality == gSimState->qualityPreset) {
        return;
    }

    _magneticFieldRenderer->Clear();
    static constexpr OrbitLayout::Body kFieldBodies[] = {
        OrbitLayout::Body::Sun,
        OrbitLayout::Body::Mercury,
        OrbitLayout::Body::Earth,
        OrbitLayout::Body::Jupiter,
        OrbitLayout::Body::Saturn,
        OrbitLayout::Body::Uranus,
        OrbitLayout::Body::Neptune,
    };
    for (const auto body : kFieldBodies) {
        const MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(body, gSimState->qualityPreset);
        MagneticFieldLineMesh mesh;
        mesh.Upload(MagneticFieldTracer::Trace(params));
        _magneticFieldRenderer->AddBody(body, std::move(mesh), params);
    }
    _magneticFieldsBuilt = true;
    _magneticFieldsQuality = gSimState->qualityPreset;
    std::cout << "[MagneticField] Built field-line ribbons for quality preset " << gSimState->qualityPreset << std::endl;
}

void Application::RenderMagneticFields() {
    if (!_magneticFieldsEnabled || !_magneticFieldRenderer || _magneticFieldRenderer->Empty()) {
        return;
    }

    const float zCoef = static_cast<float>(2.0 / glm::log2(_camera.GetFar() + 1.0));
    const float now = static_cast<float>(glfwGetTime());
    const glm::vec3 camPos = _camera.GetPosition();

    auto drawBodies = [&](float ribbonWidthScale, float opacityScale) {
        auto drawBody = [&](OrbitLayout::Body body, const MagneticFieldParams& params, const glm::vec3& position,
                            const glm::mat4& rotation, float radius) {
            if (!params.enabled || radius < 1.0e-4f) {
                return;
            }
            glm::mat4 model(1.0f);
            model = glm::translate(model, position);
            model *= rotation;
            model = glm::rotate(model, glm::radians(params.dipoleTiltDeg), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::scale(model, glm::vec3(radius));
            _magneticFieldRenderer->Draw(_cameraProjection, _cameraView, model, params, body, camPos, zCoef, now,
                                         ribbonWidthScale, opacityScale);
        };

        if (_sun) {
            const MagneticFieldParams sunParams =
                MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Sun, gSimState->qualityPreset);
            // Mesh is scaled 0.5 × sphere radius (~2) ≈ 1 unit; inflate so the torus is readable.
            const float sunRadius = 28.0f;
            drawBody(OrbitLayout::Body::Sun, sunParams, _sun->GetPosition(), glm::mat4(1.0f), sunRadius);
        }

        for (const auto& component : _renderableSceneComponents) {
            if (!component.planet) {
                continue;
            }
            const std::wstring& wideName = component.planet->GetEngName();
            const std::string name(wideName.begin(), wideName.end());
            const OrbitLayout::Body body = OrbitLayout::BodyFromName(name);
            if (body == OrbitLayout::Body::Sun) {
                continue;
            }
            const MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(body, gSimState->qualityPreset);
            if (!params.enabled) {
                continue;
            }
            const float radius = std::max(component.planet->GetRadius(), 0.5f);
            drawBody(body, params, component.planet->GetPosition(), component.planet->GetRotationMatrix(), radius);
        }
    };

    auto bindRibbonState = []() {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
    };

    bindRibbonState();
    drawBodies(1.0f, 1.0f);

    const auto quality = GetQualitySettings(gSimState->qualityPreset, gSimState->isMobileWeb);
    if (_magneticFieldBloom && quality.enableMagneticBloom) {
        _magneticFieldBloom->Resize(_displayWidth, _displayHeight);
        if (_magneticFieldBloom->IsEnabled()) {
            _magneticFieldBloom->BeginCapture();
            bindRibbonState();
            drawBodies(2.0f, 1.35f);
            const float intensity = gSimState->isMobileWeb ? 0.48f : (quality.magneticBloomPasses >= 2 ? 0.82f : 0.62f);
            _magneticFieldBloom->BlurAndComposite(quality.magneticBloomPasses, intensity);
        }
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

void Application::RenderOrbitPaths() const {
    if (!_orbitLinesEnabled || !_orbitPathRenderer || !_sun) {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    const glm::vec3 sunPos = _sun->GetPosition();
    const float orbitAlpha = _magneticFieldsEnabled ? 0.08f : 0.35f;
    const glm::vec4 color(0.45f, 0.55f, 0.75f, orbitAlpha);

    for (int i = 1; i < OrbitLayout::kBodyCount; ++i) {
        const auto body = static_cast<OrbitLayout::Body>(i);
        const float radius = OrbitLayout::GetOrbitRadius(body);
        if (radius < 0.001f) {
            continue;
        }

        glm::mat4 model(1.0f);
        model = glm::translate(model, sunPos);
        model = glm::rotate(model, glm::radians(OrbitLayout::GetInclinationDegrees(body)), glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::scale(model, glm::vec3(radius));
        _orbitPathRenderer->Draw(_cameraProjection, _cameraView, model, color);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Application::LoadMissionCatalog() {
    std::string error;
    if (!MissionCatalog::LoadFromFile("resource/missions/catalog.json", _missionCatalog, error)) {
        std::cout << "[Missions] " << error << std::endl;
        _missionCatalog = {};
        return;
    }
    std::cout << "[Missions] Loaded " << _missionCatalog.missions.size()
              << " trajectory path(s) from catalog.json" << std::endl;
}

void Application::LoadMissions() {
    if (_missionCatalog.missions.empty()) {
        LoadMissionCatalog();
    }
    RebuildMissionPaths();
}

void Application::RebuildMissionPaths() {
    _missionPathMeshes.clear();
    _missionPathMeshes.reserve(_missionCatalog.missions.size());
    for (const auto& mission : _missionCatalog.missions) {
        auto renderer = std::make_unique<MissionPathRenderer>();
        const auto auSamples = MissionCatalog::DownsampledAu(mission, gSimState->qualityPreset);
        std::vector<glm::vec3> scene;
        scene.reserve(auSamples.size());
        const glm::vec3 sunPos = _sun ? _sun->GetPosition() : glm::vec3(0.0f);
        for (const auto& au : auSamples) {
            scene.push_back(sunPos + OrbitLayout::HelioAuToScene(au));
        }
        renderer->Upload(scene);
        _missionPathMeshes.push_back(std::move(renderer));
    }
}

void Application::RenderMissionPaths() const {
    if (_missionPathMeshes.empty()) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    const float zCoef = static_cast<float>(2.0 / glm::log2(_camera.GetFar() + 1.0));
    const glm::vec3 camPos = _camera.GetPosition();
    for (size_t i = 0; i < _missionPathMeshes.size(); ++i) {
        if (!_missionPathMeshes[i] || _missionPathMeshes[i]->Empty()) {
            continue;
        }
        const auto& mission = _missionCatalog.missions[i];
        const bool focused = static_cast<int>(i) == _focusedMissionIndex;
        _missionPathMeshes[i]->Draw(_cameraProjection, _cameraView, camPos, zCoef, mission.color,
                                    focused ? 0.85f : 0.45f, focused);
        glm::vec3 probe(0.0f);
        if (SampleMissionScenePosition(static_cast<int>(i), probe)) {
            _missionPathMeshes[i]->DrawProbe(_cameraProjection, _cameraView, camPos,
                                             _camera.GetRightVector(), _camera.GetUpVector(), zCoef,
                                             probe, mission.color);
        }
    }

    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

void Application::RenderXrPointers() const {
#ifdef __EMSCRIPTEN__
    if (!_xr.active || !_xrPointerRenderer || gSimState->qualityPreset <= 0) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    const float zCoef = static_cast<float>(2.0 / glm::log2(_camera.GetFar() + 1.0));
    _xrPointerRenderer->Draw(_cameraProjection, _cameraView, _camera.GetPosition(), zCoef);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
#else
    (void)0;
#endif
}

void Application::RenderAsteroidField() {
    if (!_asteroidField || !_sun) {
        return;
    }

    _asteroidField->Update(gSimState->simDeltaSeconds);

    static const float zCoef = static_cast<float>(2.0 / glm::log2(_camera.GetFar() + 1.0));
    _asteroidField->Render(_cameraProjection, _cameraView,
                           _sun->GetPosition(), _camera.GetPosition(),
                           _camera.GetRightVector(), _camera.GetUpVector(),
                           zCoef);
}
