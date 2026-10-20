// Non-body scene overlays drawn on top of the planets: magnetic field-line ribbons
// (with their optional bloom pass), orbit path rings, mission trajectory paths, XR
// controller rays and the asteroid belt, plus the toggles that gate them. Renderer::
// methods; scene and camera state comes in through the FrameContext.
#include "Renderer.h"
#include "QualitySettings.h"
#include "Solar_System/AsteroidField.h"
#include "Solar_System/OrbitLayout.h"
#include "astro/MagneticFieldTracer.h"
#include "render/MagneticFieldBloom.h"
#include "render/MagneticFieldLineMesh.h"
#include "render/MagneticFieldLineRenderer.h"
#include "render/MissionPathRenderer.h"
#include "render/OrbitPathRenderer.h"
#include "render/XrPointerRenderer.h"
#include "core/Log.h"
#include <algorithm>
#include <optional>
#include <string>

using namespace std;

void Renderer::SetMagneticFieldsEnabled(bool enabled, int qualityPreset) {
    _magneticFieldsEnabled = enabled;
    if (enabled) {
        EnsureMagneticFieldsBuilt(qualityPreset);
    }
}

void Renderer::RebuildMagneticFields(int qualityPreset) {
    if (_magneticFieldsEnabled) {
        _magneticFieldsBuilt = false;
        EnsureMagneticFieldsBuilt(qualityPreset);
    }
}

void Renderer::EnsureMagneticFieldsBuilt(int qualityPreset) {
    if (!_magneticFieldRenderer) {
        return;
    }
    if (_magneticFieldsBuilt && _magneticFieldsQuality == qualityPreset) {
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
        const MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(body, qualityPreset);
        MagneticFieldLineMesh mesh;
        mesh.Upload(MagneticFieldTracer::Trace(params));
        _magneticFieldRenderer->AddBody(body, std::move(mesh), params);
    }
    _magneticFieldsBuilt = true;
    _magneticFieldsQuality = qualityPreset;
    LOG_INFO("MagneticField", "Built field-line ribbons for quality preset %d", qualityPreset);
}

void Renderer::RenderMagneticFields(const FrameContext& ctx) {
    if (!_magneticFieldsEnabled || !_magneticFieldRenderer || _magneticFieldRenderer->Empty()) {
        return;
    }

    const float zCoef = ctx.view.zCoef;
    const float now = static_cast<float>(glfwGetTime());
    const glm::vec3 camPos = ctx.view.headPosition;

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
            _magneticFieldRenderer->Draw(ctx.view.projection, ctx.view.view, model, params, body, camPos, zCoef, now,
                                         ribbonWidthScale, opacityScale);
        };

        if (ctx.scene.sun) {
            const MagneticFieldParams sunParams =
                MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Sun, ctx.sim.qualityPreset);
            // Mesh is scaled 0.5 × sphere radius (~2) ≈ 1 unit; inflate so the torus is readable.
            const float sunRadius = 28.0f;
            drawBody(OrbitLayout::Body::Sun, sunParams, ctx.scene.sun->GetPosition(), glm::mat4(1.0f), sunRadius);
        }

        for (const auto& component : (*ctx.scene.components)) {
            if (!component.planet) {
                continue;
            }
            const std::wstring& wideName = component.planet->GetEngName();
            const std::string name(wideName.begin(), wideName.end());
            const OrbitLayout::Body body = OrbitLayout::BodyFromName(name);
            if (body == OrbitLayout::Body::Sun) {
                continue;
            }
            const MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(body, ctx.sim.qualityPreset);
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

    const auto quality = GetQualitySettings(ctx.sim.qualityPreset, ctx.sim.isMobileWeb);
    if (_magneticFieldBloom && quality.enableMagneticBloom) {
        _magneticFieldBloom->Resize(ctx.display.x, ctx.display.y);
        if (_magneticFieldBloom->IsEnabled()) {
            _magneticFieldBloom->BeginCapture();
            bindRibbonState();
            drawBodies(2.0f, 1.35f);
            const float intensity = ctx.sim.isMobileWeb ? 0.48f : (quality.magneticBloomPasses >= 2 ? 0.82f : 0.62f);
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

void Renderer::RenderOrbitPaths(const FrameContext& ctx) const {
    if (!_orbitLinesEnabled || !_orbitPathRenderer || !ctx.scene.sun) {
        return;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    const glm::vec3 sunPos = ctx.scene.sun->GetPosition();
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
        _orbitPathRenderer->Draw(ctx.view.projection, ctx.view.view, model, color);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::RebuildMissionPaths(const MissionCatalog::Catalog& catalog, int qualityPreset) {
    // Vertices are uploaded in the render frame, so this also runs whenever the render
    // origin moves. Reuse the renderers (each owns a compiled shader) and only re-upload.
    const auto& missions = catalog.missions;
    if (_missionPathMeshes.size() != missions.size()) {
        _missionPathMeshes.clear();
        _missionPathMeshes.reserve(missions.size());
        for (size_t i = 0; i < missions.size(); ++i) {
            _missionPathMeshes.push_back(std::make_unique<MissionPathRenderer>());
        }
    }
    for (size_t i = 0; i < missions.size(); ++i) {
        const auto auSamples = MissionCatalog::DownsampledAu(missions[i], qualityPreset);
        std::vector<glm::vec3> scene;
        scene.reserve(auSamples.size());
        for (const auto& au : auSamples) {
            scene.push_back(OrbitLayout::ToRenderFrame(OrbitLayout::HelioAuToSceneDouble(glm::dvec3(au))));
        }
        _missionPathMeshes[i]->Upload(scene);
    }
}

void Renderer::RenderMissionPaths(const FrameContext& ctx) const {
    if (_missionPathMeshes.empty() || !ctx.scene.missions) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    const float zCoef = ctx.view.zCoef;
    const glm::vec3 camPos = ctx.view.headPosition;
    for (size_t i = 0; i < _missionPathMeshes.size(); ++i) {
        if (!_missionPathMeshes[i] || _missionPathMeshes[i]->Empty()) {
            continue;
        }
        if (i >= ctx.scene.missions->missions.size()) {
            break;
        }
        const auto& mission = ctx.scene.missions->missions[i];
        const bool focused = static_cast<int>(i) == ctx.scene.focusedMission;
        _missionPathMeshes[i]->Draw(ctx.view.projection, ctx.view.view, camPos, zCoef, mission.color,
                                    focused ? 0.85f : 0.45f, focused);
        const std::optional<glm::vec3>* probe =
            ctx.scene.missionProbes && i < ctx.scene.missionProbes->size() ? &(*ctx.scene.missionProbes)[i] : nullptr;
        if (probe && probe->has_value()) {
            _missionPathMeshes[i]->DrawProbe(ctx.view.projection, ctx.view.view, camPos,
                                             ctx.view.right, ctx.view.up, zCoef,
                                             **probe, mission.color);
        }
    }

    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

void Renderer::RenderXrPointers(const FrameContext& ctx) const {
#ifdef __EMSCRIPTEN__
    if (!ctx.xr || !_xrPointerRenderer || ctx.sim.qualityPreset <= 0) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    const float zCoef = ctx.view.zCoef;
    _xrPointerRenderer->Draw(ctx.view.projection, ctx.view.view, ctx.view.headPosition, zCoef);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
#else
    (void)ctx;
#endif
}

void Renderer::RenderAsteroidField(const FrameContext& ctx) const {
    if (!ctx.scene.asteroidField || !ctx.scene.sun) {
        return;
    }

    // Advanced once per frame by Application::UpdateScene; this pass only draws.
    const float zCoef = ctx.view.zCoef;
    ctx.scene.asteroidField->Render(ctx.view.projection, ctx.view.view,
                           ctx.scene.sun->GetPosition(), ctx.view.headPosition,
                           ctx.view.right, ctx.view.up,
                           zCoef);

    if (ctx.scene.kuiperField) {
        ctx.scene.kuiperField->Render(ctx.view.projection, ctx.view.view,
                                  ctx.scene.sun->GetPosition(), ctx.view.headPosition,
                                  ctx.view.right, ctx.view.up,
                                  zCoef);
    }
}
