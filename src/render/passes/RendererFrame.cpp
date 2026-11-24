// One frame as a graph (docs/ARCHITECTURE.md §7.5): what the frame has (DescribeFrame), its
// declaration on the FrameGraph (render/graph/FramePlan), compile and execute, the per-view
// binding every view pass starts with, and the ?graph=1 dump. The passes are the other files in
// src/render/passes/.
#include "Renderer.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "QualitySettings.h"
#include "SimState.h"
#include "Solar_System/SkyBox.h"
#include "Solar_System/Star.h"
#include "Solar_System/StarSystemCatalog.h"
#include "core/Log.h"
#include "gfx/FrameUniforms.h"
#include "gfx/GlPlatform.h"
#include "gfx/HDR.h"
#include "gfx/Shader.h"
#include "gfx/ShadowMapFBO.h"
#include "gfx/TextureUnits.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/GlCommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include "render/MagneticFieldBloom.h"
#include "render/MagneticFieldLineRenderer.h"
#include "render/SolarWindRenderer.h"
#include "render/graph/FrameGraph.h"
#include "render/graph/FramePlan.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string AsciiName(const std::wstring& name) {
    std::string out;
    out.reserve(name.size());
    for (const wchar_t c : name) {
        out += c > 0 && c < 128 ? static_cast<char>(c) : '?';
    }
    return out;
}

} // namespace

void Renderer::BeginFrame() {
    _configuredView = nullptr;
    _uploadedViews = 0;
    _stats = FramePlan::Stats{};
}

void Renderer::PrepareFrame(const FrameContext& ctx) {
    PrepareSpaceWeather(ctx);
    // Field-line traces finished by their jobs go to the GPU before anything draws them.
    CollectMagneticFieldTraces();
    PrepareFlight(ctx);
}

void Renderer::DescribeFrame(const FrameContext& ctx, int viewCount, FramePlan::Features& f) {
    const SceneSnapshot& scene = ctx.scene;
    f.views = viewCount;
    f.xr = ctx.xr;
    f.observe = scene.observeActive;
    f.display.width = static_cast<std::uint32_t>(ctx.display.x);
    f.display.height = static_cast<std::uint32_t>(ctx.display.y);
    f.display.format = rhi::TextureFormat::Rgba8;
    if (_shadowMapFBO) {
        f.shadowMap.width = _shadowMapFBO->GetShadowMapWidth();
        f.shadowMap.height = _shadowMapFBO->GetShadowMapHeight();
        f.shadowMap.format = rhi::TextureFormat::Depth32F;
    }

    f.components.clear();
    if (scene.components && scene.drawOrder) {
        for (const std::size_t index : *scene.drawOrder) {
            const RenderableSceneComponent& component = (*scene.components)[index];
            f.components.push_back({AsciiName(component.planet->GetEngName()), component.visible});
        }
    }
    // XR with no layer framebuffer to return to: no shadow pass (RenderXrStereoFrame logs why).
    f.shadowTarget = !(ctx.xr && ctx.defaultFramebuffer == 0);
    f.shadows = ctx.sim.shadowQuality > 0;

    f.orbitPaths = _orbitLinesEnabled && _orbitPathRenderer && scene.sun;
    // The flight planner's craft draws in the same pass as the probes' paths.
    f.missionPaths = (!_missionPathMeshes.empty() && scene.missions) || (scene.flight && _flightUploaded);
#ifdef __EMSCRIPTEN__
    f.xrPointers = ctx.xr && _xrPointerRenderer && ctx.sim.qualityPreset > 0;
#endif
    const bool volumetricCorona = _coronaSlices > 0 && _coronaVolumeShader;
    f.flatCorona = !volumetricCorona;
    f.coronaVolume = volumetricCorona;
    f.asteroids = scene.asteroidField && scene.sun;

    // The advection (with the MagnetosphereUBO upload) is declared whenever the wind has
    // particles; the graph keeps it only if a solar-wind pass reads its result.
    const SpaceWeatherFrame* weather = scene.spaceWeather;
    f.solarWindSystem = weather && weather->particles > 0 && _solarWind && _spaceWeatherUbo;
    f.solarWind = weather && _solarWind && _solarWindDrawShader && _spaceWeatherUbo && _solarWind->ParticleCount() > 0;

    f.magneticFields = StarSystemCatalog::IsSol() && _magneticFieldsEnabled && _magneticFieldRenderer &&
                       !_magneticFieldRenderer->Empty();
    const QualityTierSettings quality = GetQualitySettings(ctx.sim.qualityPreset, ctx.sim.isMobileWeb);
    f.magneticBloom = f.magneticFields && quality.enableMagneticBloom && BloomChainReady(ctx.display);
    f.auroraBloom = AuroraBloomWanted(ctx) && BloomChainReady(ctx.display);

    const bool mono = !ctx.xr && viewCount > 0;
    f.starEffects = mono && !f.observe;
    f.hdr = _hdrEnabled && _hdr && _hdr->IsEnabled();
    f.proxyLabels =
        f.starEffects && _overlayToggles.stagingLabels && scene.stagedSystems && !scene.stagedSystems->empty();
    f.observeHud = mono && f.observe;
    f.distances = mono && !f.observe && (_overlayToggles.planetStarDistances || _overlayToggles.satelliteDistances);
    f.hints = mono && !f.observe && _overlayToggles.hints;
    f.screenFade = mono && ctx.hud && ctx.hud->fadeAmount > 0.0f && _screenFadeShader && _screenFadeVao;
    f.uploads = true;
}

void Renderer::RenderFrame(const std::vector<FrameContext>& views, const FramePlan::Features& features) {
    _views = &views;
    if (!views.empty()) {
        _commands->SetDefaultFramebuffer(views.front().defaultFramebuffer);
    }
    FramePlan::Build(_graph, features, *this, _passTimings);
    if (!_graph.Compile()) {
        // A plan bug, not a runtime condition: say so once rather than every frame.
        static bool reported = false;
        if (!reported) {
            reported = true;
            LOG_ERROR("FrameGraph", "Frame not drawn: %s", _graph.Errors().front().c_str());
        }
        _views = nullptr;
        return;
    }
    _stats.passes = static_cast<int>(_graph.Order().size());
    _stats.culledPasses = static_cast<int>(_graph.CulledCount());
    DumpGraphIfChanged();
    _graph.Execute(*_commands);
    _views = nullptr;
}

void Renderer::SetProfiling(GpuTimer* gpu, FrameProfile::PassTimings* passes, FrameProfile::CpuStages* stages) {
    _passTimings = passes;
    _commands->SetProfiling(gpu, passes, stages);
}

void Renderer::SetDebugGroups(bool enabled) {
    _commands->SetDebugGroups(enabled);
}

void Renderer::DumpGraphIfChanged() {
    if (!_graphDump) {
        return;
    }
    std::string signature = _graph.Signature();
    if (signature == _graphSignature) {
        return;
    }
    _graphSignature = std::move(signature);
    LOG_INFO("FrameGraph", "%zu passes run, %zu culled, %u transient slot(s)", _graph.Order().size(),
             _graph.CulledCount(), _graph.PhysicalCount());
    LOG_INFO("FrameGraph", "json %s", _graph.ToJson().c_str());
    LOG_INFO("FrameGraph", "dot\n%s", _graph.ToDot().c_str());
}

const RenderableSceneComponent& Renderer::DrawnComponent(const FrameContext& ctx, std::size_t drawIndex) const {
    return (*ctx.scene.components)[(*ctx.scene.drawOrder)[drawIndex]];
}

MagneticFieldBloom& Renderer::BloomChain(std::size_t slot) {
    // Slot 0 is the chain Init made; a frame whose blooms overlap would need a second one.
    while (_bloomChains.size() <= slot) {
        const glm::ivec2 display = View(0).display;
        _bloomChains.push_back(std::make_unique<MagneticFieldBloom>(display.x, display.y, _bloomEnabled));
    }
    return *_bloomChains[slot];
}

bool Renderer::BloomChainReady(glm::ivec2 display) {
    if (_bloomChains.empty()) {
        return false;
    }
    _bloomChains.front()->Resize(display.x, display.y);
    return _bloomChains.front()->IsEnabled();
}

void Renderer::BeginView(rhi::CommandList& cmd, int view) {
    const FrameContext& ctx = View(view);
    const glm::ivec4& viewport = ctx.view.viewport;
    cmd.SetViewport(viewport.x, viewport.y, viewport.z, viewport.w);
    const std::size_t viewIndex = ctx.view.eye < 0 ? 0 : static_cast<std::size_t>(ctx.view.eye);
    if ((_uploadedViews & (1u << viewIndex)) == 0) {
        // Once per view per frame; XR comes back to each eye for every component.
        FrameUniformData data;
        data.view = ctx.view.view;
        data.proj = ctx.view.projection;
        data.viewProj = ctx.view.projection * ctx.view.view;
        data.cameraPos = glm::vec4(ctx.view.eyePosition, 1.0f);
        data.sunPos = glm::vec4(ctx.scene.sun ? ctx.scene.sun->GetPosition() : glm::vec3(0.0f), 1.0f);
        data.farPlane = ctx.view.farPlane;
        data.zCoef = ctx.view.zCoef;
        data.logDepthC = ctx.view.logDepthC;
        data.nearPlane = ctx.view.nearPlane;
        data.time = static_cast<float>(ctx.sim.animationSeconds);
        data.exposure = _starTuning.exposure;
        data.starColor = glm::vec4(ctx.scene.sun ? ctx.scene.sun->GetLightTint() : glm::vec3(1.0f), 1.0f);
        _frameUniforms.Upload(viewIndex, data);
        _uploadedViews |= 1u << viewIndex;
    }
    _frameUniforms.Bind(viewIndex);
    // The per-view uniforms only change with the view; a shadow pass in between leaves them.
    if (&ctx.view != _configuredView) {
        ConfigureMainShaders(ctx);
        _configuredView = &ctx.view;
        ++_stats.viewBinds;
    }
}

void Renderer::ConfigureMainShaders(const FrameContext& ctx) {
    // Matrices, eye position, Sun position, far plane, zCoef and time are in the FrameUBO
    // (BeginView). What is left here is per-view state of programs that do not read it, and
    // per-frame constants; Shader drops a Set* that repeats the value already there.

    // Skybox uses the active view's rotation; projection matches the eye frustum in XR.
    const glm::mat4 skyBoxProjection =
        ctx.xr ? ctx.view.projection
               : glm::perspective(glm::radians(45.0f), ctx.view.aspect, ctx.view.nearPlane, ctx.view.farPlane);

    _mainSkyBoxShader->Use();
    _mainSkyBoxShader->SetMat4("view", glm::mat4(glm::mat3(ctx.view.view)));
    _mainSkyBoxShader->SetMat4("projection", skyBoxProjection);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", ctx.view.projection);
    _mainTextShader->SetMat4("view", ctx.view.view);

    const Star& sun = *ctx.scene.sun;
    _mainStarShader->Use();
    _mainStarShader->SetVec3("shiftStarColor", sun.GetShiftColor());
    _mainStarShader->SetVec3("colorMult", glm::vec3(0.96862745, 0.58039215, 0.235294117) * sun.GetShiftColor());
    _mainStarShader->SetFloat("starRadiusInKilometers", sun.GetStarRadius());
    _mainStarShader->SetFloat("uColorMap", sun.GetTemperatureColorUCoordinate());
    _mainStarShader->SetBool("isVisible", sun.GetVisibility() == 1.0);

    _mainCoronaStarShader->Use();
    _mainCoronaStarShader->SetVec3("center", sun.GetPosition());
    // The eye's axes, not the head's (the billboard faces each XR eye).
    const glm::mat4 inverseView = glm::inverse(ctx.view.view);
    _mainCoronaStarShader->SetVec3("cameraRight", glm::normalize(glm::vec3(inverseView[0])));
    _mainCoronaStarShader->SetVec3("cameraUp", glm::normalize(glm::vec3(inverseView[1])));
    _mainCoronaStarShader->SetVec3("starShiftColor", sun.GetShiftColor());
    _mainCoronaStarShader->SetFloat("maxSize", 7.1);
    _mainCoronaStarShader->SetFloat("starRadius", sun.GetStarRadius());

    const float surfaceDim = _magneticFieldsEnabled ? 0.28f : 1.0f;
    const float atmosphereDim = _magneticFieldsEnabled ? 0.40f : 1.0f;
    const float ringDim = _magneticFieldsEnabled ? 0.35f : 1.0f;

    // planetLighting's per-view values go on in ConfigurePlanetProgram: which of its variants
    // draw this view is only known per body.

    _mainAtmosphereShader->Use();
    _mainAtmosphereShader->SetFloat("uSurfaceDim", atmosphereDim);

    if (_pbrAtmosphereShader) {
        _pbrAtmosphereShader->Use();
        _pbrAtmosphereShader->SetFloat("uSurfaceDim", atmosphereDim);
    }

    _mainCloudsShader->Use();
    _mainCloudsShader->SetFloat("uSurfaceDim", surfaceDim);

    _mainRingShader->Use();
    _mainRingShader->SetVec3("starGlowTint", sun.GetGlowTintMult());
    _mainRingShader->SetFloat("uSurfaceDim", ringDim);

    // Every lit program samples the shadow map from the same unit (TU_SHADOW_MAP).
    glBindTextureUnit(Unit(TextureUnit::ShadowMap), _shadowMapFBO->GetShadowMap());
}

void Renderer::SkyBoxPass(rhi::CommandList& /*cmd*/, int /*view*/) {
    _skyBox->Render(*_mainSkyBoxShader);
}

void Renderer::Uploads(rhi::CommandList& /*cmd*/) {
    if (_uploadPump) {
        _uploadPump();
    }
}
