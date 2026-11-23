// The shadow pass (docs/ARCHITECTURE.md §7.5): one shadow map, drawn per scene component
// from the Sun into that component's fitted light box, and read by every view's colour pass of
// the same component before the next component overwrites it (FramePlan's write-after-read).
#include "Renderer.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "PassCommon.h"
#include "gfx/Shader.h"
#include "gfx/ShadowMapFBO.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include "render/terrain/TerrainRenderer.h"
#include <cstddef>

using RenderPasses::Program;

void Renderer::ShadowMap(rhi::CommandList& cmd, std::size_t drawIndex) {
    const FrameContext& ctx = View(0);
    const RenderableSceneComponent& component = DrawnComponent(ctx, drawIndex);
    // Under WebXR the frame composites into the XRWebGLLayer framebuffer, not FBO 0, so the
    // pass returns to DefaultFramebuffer() instead of hardcoding 0. With no layer framebuffer
    // registered, FramePlan declares no shadow pass at all (Features::shadowTarget), and a
    // hidden component (drawn only for the star) none of its own (Component::castsShadow).
    cmd.SetRenderTarget(
        {_shadowMapFBO->GetFBO(), 0, 0, _shadowMapFBO->GetShadowMapWidth(), _shadowMapFBO->GetShadowMapHeight()});
    cmd.Clear(rhi::ClearFlags::Depth);

    ++_stats.shadowPasses;
    _shadowMapShader->Use();
    _shadowMapShader->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);

    // A body drawing terrain casts from its tiles: its sphere's facets sag below the ground
    // the colour pass draws, and would shadow it from underneath.
    const auto drawCaster = [&](const SpaceObject& object) {
        const SceneSnapshot::TerrainBody* terrain = ctx.scene.TerrainFor(&object);
        const Shader* program = terrain && terrain->tiles ? TerrainShadowProgram() : nullptr;
        if (!program) {
            object.Draw(*_shadowMapShader);
            return;
        }
        program->Use();
        program->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
        _terrainRenderer->Draw(*program, *terrain->tiles, object.GetModelMatrix(), TerrainRenderer::Pass::Shadow);
        _shadowMapShader->Use();
    };
    drawCaster(*component.planet);
    for (const auto& satellite : component.satellites) {
        drawCaster(*satellite);
    }

    if (component.planetaryRing) {
        component.planetaryRing->Draw(*_shadowMapShader);
    }
    cmd.BindFramebuffer(ctx.defaultFramebuffer);
}

void Renderer::ClearShadowMap(rhi::CommandList& cmd) {
    // Shadows off: a cleared depth texture holds 1.0 everywhere, which the lighting shaders
    // read as fully lit. Clear it once so no map from before shadows were turned off lingers.
    cmd.SetRenderTarget(
        {_shadowMapFBO->GetFBO(), 0, 0, _shadowMapFBO->GetShadowMapWidth(), _shadowMapFBO->GetShadowMapHeight()});
    cmd.Clear(rhi::ClearFlags::Depth);
    cmd.BindFramebuffer(View(0).defaultFramebuffer);
}

const Shader* Renderer::TerrainShadowProgram() {
    if (!_terrainShadowShader) {
        _terrainShadowShader = _shaders.GetIfReady(Program("terrain.vs", "shadowMap.fs", {"SHADOW_PASS"}));
    }
    return _terrainShadowShader;
}
