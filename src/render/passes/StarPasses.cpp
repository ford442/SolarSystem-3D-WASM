// The star's passes (docs/ARCHITECTURE.md §9.2): the photosphere with its occlusion queries
// (drawn inside the nearest component's scene pass, so the planets in front hide it), the flat
// or volumetric corona, and the mono effects after the scene: the glow (into the HDR target when
// there is one), the tone-map composite and the lens flare.
#include "Renderer.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "Solar_System/PlanetaryRing.h"
#include "Solar_System/Star.h"
#include "gfx/GlPlatform.h"
#include "gfx/HDR.h"
#include "gfx/Shader.h"
#include "gfx/TextureUnits.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include "render/LensFlare.h"
#include <algorithm>
#include <optional>

namespace {

/** The nearest ring, if any, as the glow and the lens flare see it (they dim behind it). */
std::optional<RingCameraInfo> NearestRingInfo(const FrameContext& ctx) {
    const RenderableSceneComponent* nearest = ctx.scene.Nearest();
    const PlanetaryRing* ring = nearest ? nearest->planetaryRing.get() : nullptr;
    if (!ring) {
        return std::nullopt;
    }
    return RingCameraInfo{ctx.view.headPosition, ring->GetPosition(), ring->GetRingNormal(),
                          glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()), ring->GetRingTexture()};
}

} // namespace

void Renderer::FlatCorona(rhi::CommandList& cmd, int view) {
    RenderStarCorona(cmd, View(view));
}

void Renderer::CoronaVolume(rhi::CommandList& cmd, int view) {
    RenderStarCoronaVolume(cmd, View(view));
}

void Renderer::ProcessStarRendering(rhi::CommandList& cmd, const FrameContext& ctx) {
#ifdef __EMSCRIPTEN__
    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(false).SetDepthTest(false).SetBlend(true));
    RenderStar(cmd, ctx);
    cmd.SetPipelineState(rhi::PipelineState().SetBlend(false).SetDepthTest(true).SetDepthWrite(true));
    ctx.scene.sun->SetVisibility(1.0f);
#else
    // Two sample queries: every fragment of the star, then the ones the scene does not hide.
    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(false));
    glBeginQuery(GL_SAMPLES_PASSED, ctx.scene.sun->GetStarOcclusionValue(0));
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetBlend(true));
    RenderStar(cmd, ctx);
    cmd.SetPipelineState(
        rhi::PipelineState().SetBlend(false).SetDepthTest(true).SetDepthFunc(rhi::CompareOp::LessEqual));
    glEndQuery(GL_SAMPLES_PASSED);

    glBeginQuery(GL_SAMPLES_PASSED, ctx.scene.sun->GetStarOcclusionValue(1));
    RenderStar(cmd, ctx);
    glEndQuery(GL_SAMPLES_PASSED);
    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(true).SetDepthFunc(rhi::CompareOp::Less));

    UpdateOcclusionQuery(*ctx.scene.sun);
#endif
}

void Renderer::UpdateOcclusionQuery(Star& sun) {
    GLuint totalSamples = 0;
    GLuint visibleSamples = 0;
    glGetQueryObjectuiv(sun.GetStarOcclusionValue(0), GL_QUERY_RESULT, &totalSamples);
    glGetQueryObjectuiv(sun.GetStarOcclusionValue(1), GL_QUERY_RESULT, &visibleSamples);

    if (totalSamples == 0) {
        sun.SetVisibility(0.0f);
        return;
    }

    sun.SetVisibility(static_cast<float>(visibleSamples) / static_cast<float>(totalSamples));
}

void Renderer::RenderStarCorona(rhi::CommandList& cmd, const FrameContext& ctx) const {
    // Only declared when the volumetric corona is off (FramePlan::Features::flatCorona).
    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(false).SetBlend(true).SetBlendFunc(rhi::BlendFactor::One,
                                                                                               rhi::BlendFactor::One));

    _mainCoronaStarShader->Use();
    ctx.scene.sun->Draw(*_mainCoronaStarShader);

    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(true).SetBlend(false));
}

void Renderer::RenderStarCoronaVolume(rhi::CommandList& cmd, const FrameContext& ctx) const {
    // Only declared with coronaSlices > 0 and its program built (FramePlan::Features::coronaVolume).
    // In solar radii. The K-corona is visible to a few radii; 6 keeps the slices' discs
    // small enough that the stack stays cheap from planetary distances.
    constexpr float kCoronaExtent = 6.0f;
    // Tuned by eye so the streamers read against black without the inner corona clipping;
    // the HDR glow and lens flare sit on top of it.
    constexpr float kCoronaIntensity = 0.8f;

    const glm::vec3 sunCenter = ctx.scene.sun->GetPosition();
    const float sunRadius = ctx.scene.sun->GetSceneRadius();
    const glm::mat4 inverseView = glm::inverse(ctx.view.view);
    const glm::vec3 camera(inverseView[3]);
    const glm::vec3 cameraUp(inverseView[1]);
    const glm::vec3 cameraLocal = (camera - sunCenter) / sunRadius;
    const float distance = glm::length(cameraLocal);
    if (distance <= 1.0f) {
        return; // Inside the photosphere.
    }

    // Outside, slices are perpendicular to the camera->Sun axis, so the whole corona is
    // crossed squarely. Inside, rays to the edge of the screen would graze those slices, so
    // they follow the view direction instead (every on-screen ray within FOV/2 of normal).
    // The thresholds differ so the switch does not flicker at the boundary.
    _coronaSlicesViewAligned = distance < (_coronaSlicesViewAligned ? 1.25f : 1.15f) * kCoronaExtent;
    const glm::vec3 forward =
        _coronaSlicesViewAligned ? -glm::normalize(glm::vec3(inverseView[2])) : -cameraLocal / distance;

    // Slice depths along `forward` from the Sun's centre: only the part of the sphere in front
    // of the camera, starting a little past the near plane so no slab is clipped by it.
    const float nearMargin = std::max(2.0f * ctx.view.nearPlane / sunRadius, 0.05f);
    const float depthMax = kCoronaExtent;
    const float depthMin = std::max(-kCoronaExtent, glm::dot(cameraLocal, forward) + nearMargin);
    if (depthMin >= depthMax) {
        return; // The corona is entirely behind the camera.
    }

    glm::vec3 right = glm::cross(forward, cameraUp);
    if (glm::dot(right, right) < 1e-8f) {
        right = glm::cross(forward, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    right = glm::normalize(right);
    const glm::vec3 up = glm::cross(right, forward);

    cmd.SetPipelineState(rhi::PipelineState()
                             .SetDepthWrite(false)
                             .SetBlend(true)
                             .SetBlendFunc(rhi::BlendFactor::One, rhi::BlendFactor::One)
                             .SetCullFace(false));

    Shader& shader = *_coronaVolumeShader;
    shader.Use();
    shader.SetVec3("sunCenter", sunCenter);
    shader.SetFloat("sunRadius", sunRadius);
    shader.SetVec3("sliceForward", forward);
    shader.SetVec3("sliceRight", right);
    shader.SetVec3("sliceUp", up);
    shader.SetFloat("coronaExtent", kCoronaExtent);
    shader.SetInt("sliceCount", _coronaSlices);
    shader.SetFloat("sliceDepthMin", depthMin);
    shader.SetFloat("sliceDepthMax", depthMax);
    shader.SetFloat("nearMargin", nearMargin);
    shader.SetVec3("cameraLocal", cameraLocal);
    shader.SetFloat("sliceSpacing", (depthMax - depthMin) / static_cast<float>(_coronaSlices));
    shader.SetVec3("coronaColor", ctx.scene.sun->GetShiftColor());
    shader.SetFloat("coronaIntensity", kCoronaIntensity);
    shader.SetFloat("activity", ctx.scene.sun->GetActivity());
    ctx.scene.sun->DrawCoronaSlices(_coronaSlices);

    cmd.SetPipelineState(rhi::PipelineState().SetCullFace(true).SetBlend(false).SetDepthWrite(true));
}

void Renderer::RenderStar(rhi::CommandList& cmd, const FrameContext& ctx) const {
    cmd.SetPipelineState(
        rhi::PipelineState().SetBlend(true).SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::One));

    _mainStarShader->Use();
    // Bound here, not with the per-view uniforms: the planets drawn before the star reuse
    // the Diffuse unit, and the photosphere colour used to come from the last moon's map.
    cmd.SetBindGroup(rhi::BindGroup().Texture(Unit(TextureUnit::Diffuse), ctx.scene.sun->GetStarSpectrumTexture()));
    ctx.scene.sun->Draw(*_mainStarShader);

    cmd.SetPipelineState(rhi::PipelineState().SetBlend(false));
}

void Renderer::StarGlow(rhi::CommandList& cmd) {
    const FrameContext& ctx = View(0);
    const std::optional<RingCameraInfo> ringCameraInfo = NearestRingInfo(ctx);
    // Into the HDR target when there is one (hdr-composite tone-maps it onto the frame), else
    // straight onto the frame.
    if (_hdrEnabled && _hdr && _hdr->IsEnabled()) {
        cmd.BindFramebuffer(_hdr->GetHdrFBO());
        cmd.Clear(rhi::ClearFlags::ColorDepth);
    } else {
        cmd.BindFramebuffer(ctx.defaultFramebuffer);
    }
    cmd.SetPipelineState(rhi::PipelineState().SetBlend(true).SetBlendFunc(rhi::BlendFactor::SrcAlpha,
                                                                          rhi::BlendFactor::OneMinusSrcAlpha));
    ctx.scene.sun->RenderGlow(*_starGlowShader, ctx.view.projection, ctx.view.view, ctx.view.front - ctx.view.right,
                              ctx.view.aspect, glm::distance(ctx.view.headPosition, ctx.scene.sun->GetPosition()),
                              ringCameraInfo, _starTuning.temperatureKelvin);
}

void Renderer::HdrComposite(rhi::CommandList& cmd) {
    cmd.BindFramebuffer(View(0).defaultFramebuffer);
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetDepthWrite(false).SetBlend(true).SetBlendFunc(
        rhi::BlendFactor::One, rhi::BlendFactor::One));
    _hdr->Render(_starTuning.exposure, _starTuning.gamma);
}

void Renderer::LensFlarePass(rhi::CommandList& cmd) {
    const FrameContext& ctx = View(0);
    cmd.BindFramebuffer(ctx.defaultFramebuffer);
    cmd.SetPipelineState(rhi::PipelineState().SetDepthTest(false).SetDepthWrite(false).SetBlend(true).SetBlendFunc(
        rhi::BlendFactor::One, rhi::BlendFactor::One));
    const float intensity = glm::min(ctx.scene.sun->GetCurrentGlowSize() * ctx.scene.sun->GetVisibility(), 1.0f);
    _lensFlare->Render(ctx.view.projection, ctx.view.view, ctx.scene.sun->GetPosition(), ctx.scene.sun->GetLightTint(),
                       ctx.view.aspect, 0.1, intensity, NearestRingInfo(ctx));
    cmd.SetPipelineState(rhi::PipelineState()
                             .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcAlpha)
                             .SetBlend(false)
                             .SetDepthWrite(true)
                             .SetDepthTest(true));
}
