// Non-body scene overlays: magnetic field-line ribbons (with their optional bloom: a capture
// pass into a bloom chain and a composite pass, FramePlan's magnetic-bloom), orbit path rings,
// mission trajectory paths, XR controller rays and the asteroid belt, plus the toggles that
// gate them. Renderer:: methods; scene and camera state comes in through the FrameContext.
#include "Renderer.h"
#include "FrameContext.h"
#include "QualitySettings.h"
#include "Solar_System/AsteroidField.h"
#include "Solar_System/OrbitLayout.h"
#include "astro/MagneticFieldModel.h"
#include "astro/MagneticFieldParams.h"
#include "astro/MissionCatalog.h"
#include "astro/MagneticFieldTracer.h"
#include "core/JobSystem.h"
#include "render/MagneticFieldBloom.h"
#include "render/MagneticFieldLineMesh.h"
#include "render/MagneticFieldLineRenderer.h"
#include "render/MagneticFieldTraceJobs.h"
#include "render/MissionPathRenderer.h"
#include "render/OrbitPathRenderer.h"
#include "render/XrPointerRenderer.h"
#include "core/Log.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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
    if (_magneticFieldTraces && _magneticFieldTraces->qualityPreset == qualityPreset) {
        CollectMagneticFieldTraces();
        return; // already tracing this preset
    }

    // Trace every body in a job; the ribbons drawn now stay until the new set is in.
    static constexpr OrbitLayout::Body kFieldBodies[] = {
        OrbitLayout::Body::Sun,    OrbitLayout::Body::Mercury, OrbitLayout::Body::Earth,   OrbitLayout::Body::Jupiter,
        OrbitLayout::Body::Saturn, OrbitLayout::Body::Uranus,  OrbitLayout::Body::Neptune,
    };
    auto traces = std::make_unique<MagneticFieldTraceJobs>();
    traces->qualityPreset = qualityPreset;
    for (const auto body : kFieldBodies) {
        const MagneticFieldParams params = MagneticFieldCatalog::ParamsForBody(body, qualityPreset);
        traces->bodies.push_back({body, params, Jobs::Submit([params] { return MagneticFieldTracer::Trace(params); })});
    }
    _magneticFieldTraces = std::move(traces);
    CollectMagneticFieldTraces(); // inline jobs (SOLARSYSTEM_JOBS=0, single-threaded web) are done already
}

void Renderer::CollectMagneticFieldTraces() {
    if (!_magneticFieldTraces || !_magneticFieldRenderer || !_magneticFieldTraces->Ready()) {
        return;
    }
    _magneticFieldRenderer->Clear();
    for (MagneticFieldTraceJobs::Body& traced : _magneticFieldTraces->bodies) {
        MagneticFieldLineMesh mesh;
        mesh.Upload(traced.lines.Get());
        _magneticFieldRenderer->AddBody(traced.body, std::move(mesh), traced.params);
    }
    _magneticFieldsBuilt = true;
    _magneticFieldsQuality = _magneticFieldTraces->qualityPreset;
    _magneticFieldTraces.reset();
    LOG_INFO("MagneticField", "Built field-line ribbons for quality preset %d", _magneticFieldsQuality);
}

namespace {

// The ribbons' state: additive over the scene, depth-tested but not written, both faces,
// pulled towards the camera so they sit on the bodies they wrap.
void BindRibbonState(rhi::CommandList& cmd) {
    cmd.SetPipelineState(rhi::PipelineState()
                             .SetBlend(true)
                             .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::One)
                             .SetDepthTest(true)
                             .SetDepthWrite(false)
                             .SetCullFace(false)
                             .SetPolygonOffset(true, -1.0f, -1.0f));
}

void RestoreRibbonState(rhi::CommandList& cmd) {
    cmd.SetPipelineState(rhi::PipelineState()
                             .SetPolygonOffset(false)
                             .SetDepthWrite(true)
                             .SetCullFace(true)
                             .SetDepthTest(true)
                             .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha)
                             .SetBlend(false));
}

} // namespace

void Renderer::DrawMagneticFieldRibbons(const FrameContext& ctx, float ribbonWidthScale, float opacityScale) {
    const float now = static_cast<float>(ctx.sim.animationSeconds);
    const glm::vec3 camPos = ctx.view.headPosition;
    auto drawBody = [&](OrbitLayout::Body body, const MagneticFieldParams& params, const glm::vec3& position,
                        const glm::mat4& rotation, float radius) {
        if (!params.enabled || radius < 1.0e-4f) {
            return;
        }
        glm::mat4 model(1.0f);
        model = glm::translate(model, position);
        model *= rotation;
        model *= glm::mat4(MagneticFieldModel::DipoleFrame(params));
        model = glm::scale(model, glm::vec3(radius));
        // The wind's squeeze on the dayside and stretch into the tail (magneticFieldLine.vs).
        const SpaceWeatherFrame::Body* weather =
            ctx.scene.spaceWeather ? ctx.scene.spaceWeather->Find(static_cast<int>(body)) : nullptr;
        if (weather) {
            _magneticFieldRenderer->SetCompression(weather->sunward,
                                                   weather->standoff / std::max(weather->quietStandoff, 1e-3f));
        } else {
            _magneticFieldRenderer->SetCompression(glm::vec3(1.0f, 0.0f, 0.0f), 1.0f);
        }
        _magneticFieldRenderer->Draw(ctx.view.projection, ctx.view.view, model, params, body, camPos, now,
                                     ribbonWidthScale, opacityScale);
    };

    if (ctx.scene.sun) {
        const MagneticFieldParams sunParams =
            MagneticFieldCatalog::ParamsForBody(OrbitLayout::Body::Sun, ctx.sim.qualityPreset);
        // Mesh is scaled 0.5 × sphere radius (~2) ≈ 1 unit; inflate so the torus is readable.
        const float sunRadius = 28.0f * ctx.scene.sun->SizeFactor();
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
        const float radius = std::max(component.planet->GetRadius(), 0.5f * component.planet->SizeFactor());
        drawBody(body, params, component.planet->GetPosition(), component.planet->GetRotationMatrix(), radius);
    }
}

void Renderer::MagneticFields(rhi::CommandList& cmd, int view, bool restoreState) {
    // Declared in Sol with the fields on and their ribbons built (Features::magneticFields); the
    // traces were collected in PrepareFrame.
    BindRibbonState(cmd);
    DrawMagneticFieldRibbons(View(view), 1.0f, 1.0f);
    if (restoreState) {
        RestoreRibbonState(cmd); // no magnetic-bloom pass follows to do it
    }
}

void Renderer::MagneticBloom(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) {
    // The ribbons again, wider and brighter, into the bloom chain the graph gave this pass
    // (DescribeFrame sized it and checked that it is on).
    BloomChain(bloomSlot).BeginCapture();
    BindRibbonState(cmd);
    DrawMagneticFieldRibbons(View(view), 2.0f, 1.35f);
}

void Renderer::MagneticBloomComposite(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) {
    const FrameContext& ctx = View(view);
    const auto quality = GetQualitySettings(ctx.sim.qualityPreset, ctx.sim.isMobileWeb);
    const float intensity = ctx.sim.isMobileWeb ? 0.48f : (quality.magneticBloomPasses >= 2 ? 0.82f : 0.62f);
    BloomChain(bloomSlot).BlurAndComposite(quality.magneticBloomPasses, intensity);
    RestoreRibbonState(cmd);
}

void Renderer::OrbitPaths(rhi::CommandList& cmd, int view) {
    RenderOrbitPaths(cmd, View(view));
}

void Renderer::MissionPaths(rhi::CommandList& cmd, int view) {
    RenderMissionPaths(cmd, View(view));
}

void Renderer::XrPointers(rhi::CommandList& cmd, int view) {
    RenderXrPointers(cmd, View(view));
}

void Renderer::Asteroids(rhi::CommandList& /*cmd*/, int view) {
    RenderAsteroidField(View(view));
}

void Renderer::RenderOrbitPaths(rhi::CommandList& cmd, const FrameContext& ctx) const {
    // Declared with orbit lines on (Features::orbitPaths).
    cmd.SetPipelineState(rhi::PipelineState()
                             .SetBlend(true)
                             .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha)
                             .SetDepthTest(true)
                             .SetDepthWrite(false));

    const glm::vec3 sunPos = ctx.scene.sun->GetPosition();
    const float orbitAlpha = _magneticFieldsEnabled ? 0.08f : 0.35f;
    const glm::vec4 color(0.45f, 0.55f, 0.75f, orbitAlpha);

    for (int i = 1; i < OrbitLayout::BodyCount(); ++i) {
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

    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(true).SetBlend(false));
}

namespace {

// Back to the scene's defaults after an additive overlay drawn with both faces.
const rhi::PipelineState kOverlayDone =
    rhi::PipelineState()
        .SetCullFace(true)
        .SetDepthWrite(true)
        .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha)
        .SetBlend(false);

// How far the render origin may move from the mission paths' upload origin before they are
// re-uploaded. Floats below 512 are spaced at most 6e-5 units apart, under a thousandth of
// Earth's realistic radius, so a path near the camera stays under a pixel off its probe.
constexpr double kMissionPathRebaseDistance = 512.0;

} // namespace

void Renderer::RebuildMissionPaths(const MissionCatalog::Catalog& catalog, int qualityPreset) {
    // Vertices are relative to the render origin at upload time; RenderMissionPaths offsets
    // them by where that point is now. Reuse the renderers (each owns a compiled shader) and
    // only re-upload.
    _missionPathOrigin = OrbitLayout::GetRenderOrigin();
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
            scene.push_back(glm::vec3(OrbitLayout::HelioAuToSceneDouble(glm::dvec3(au)) - _missionPathOrigin));
        }
        _missionPathMeshes[i]->Upload(scene);
    }
}

void Renderer::FollowMissionPathOrigin(const MissionCatalog::Catalog& catalog, int qualityPreset) {
    if (glm::distance(OrbitLayout::GetRenderOrigin(), _missionPathOrigin) > kMissionPathRebaseDistance) {
        RebuildMissionPaths(catalog, qualityPreset);
    }
}

void Renderer::RenderMissionPaths(rhi::CommandList& cmd, const FrameContext& ctx) const {
    // Declared once the paths are uploaded (Features::missionPaths).
    cmd.SetPipelineState(rhi::PipelineState()
                             .SetBlend(true)
                             .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::One)
                             .SetDepthTest(true)
                             .SetDepthWrite(false)
                             .SetCullFace(false));

    const glm::vec3 camPos = ctx.view.headPosition;
    const glm::vec3 pathOffset(_missionPathOrigin - ctx.view.renderOrigin);
    for (size_t i = 0; i < _missionPathMeshes.size(); ++i) {
        if (!_missionPathMeshes[i] || _missionPathMeshes[i]->Empty()) {
            continue;
        }
        if (i >= ctx.scene.missions->missions.size()) {
            break;
        }
        const auto& mission = ctx.scene.missions->missions[i];
        const bool focused = static_cast<int>(i) == ctx.scene.focusedMission;
        _missionPathMeshes[i]->Draw(ctx.view.projection, ctx.view.view, camPos, pathOffset, mission.color,
                                    focused ? 0.85f : 0.45f, focused);
        const std::optional<glm::vec3>* probe =
            ctx.scene.missionProbes && i < ctx.scene.missionProbes->size() ? &(*ctx.scene.missionProbes)[i] : nullptr;
        if (probe && probe->has_value()) {
            _missionPathMeshes[i]->DrawProbe(ctx.view.projection, ctx.view.view, camPos, ctx.view.right, ctx.view.up,
                                             **probe, mission.color);
        }
    }

    cmd.SetPipelineState(kOverlayDone);
}

void Renderer::RenderXrPointers(rhi::CommandList& cmd, const FrameContext& ctx) const {
    // Declared in a WebXR session above Low (Features::xrPointers).
    cmd.SetPipelineState(rhi::PipelineState()
                             .SetBlend(true)
                             .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::One)
                             .SetDepthWrite(false)
                             .SetCullFace(false));
    _xrPointerRenderer->Draw(ctx.view.projection, ctx.view.view, ctx.view.headPosition);
    cmd.SetPipelineState(kOverlayDone);
}

void Renderer::RenderAsteroidField(const FrameContext& ctx) const {
    // Declared in Sol (Features::asteroids). Advanced once per frame by Application::UpdateScene; this pass only draws, with the
    // view's FrameUBO (BeginView) supplying matrices, eye, Sun and log-depth constants.
    ctx.scene.asteroidField->Render(ctx.view.right);
    if (ctx.scene.kuiperField) {
        ctx.scene.kuiperField->Render(ctx.view.right);
    }
}
