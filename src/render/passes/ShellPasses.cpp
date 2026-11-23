// A component's translucent shells, drawn after its bodies in the scene pass: atmospheres (the
// additive O'Neil shell or the LUT path, docs/ARCHITECTURE.md §9.2), the cloud deck, and rings.
#include "Renderer.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "PassCommon.h"
#include "Solar_System/AtmosphereModel.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/Clouds.h"
#include "Solar_System/PlanetaryRing.h"
#include "Solar_System/Star.h"
#include "gfx/Shader.h"
#include "gfx/TextureUnits.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"

using RenderPasses::kShellShadowBias;
using RenderPasses::NearSurfaceDefine;
using RenderPasses::Program;
using RenderPasses::ShadowBias;

void Renderer::RenderAtmospheres(rhi::CommandList& cmd, const FrameContext& ctx,
                                 const RenderableSceneComponent& component) {
    if (component.atmospheres.empty()) {
        return;
    }
    cmd.SetPipelineState(rhi::PipelineState().SetDepthWrite(false).SetBlend(true));

    for (const auto& renderableAtmosphere : component.atmospheres) {
        if (!renderableAtmosphere.atmosphere->IsVisible()) {
            continue;
        }
        // Inside the shell, draw its far side (the near side is behind the camera).
        const bool inside = renderableAtmosphere.atmosphere->Contains(ctx.view.eyePosition);
        if (inside) {
            cmd.SetPipelineState(rhi::PipelineState().SetFrontFace(rhi::FrontFace::Clockwise));
        }
        if (renderableAtmosphere.atmosphere->IsPhysicalPathActive()) {
            RenderPbrAtmosphere(cmd, ctx, renderableAtmosphere, component);
        } else {
            RenderOneilAtmosphere(cmd, ctx, renderableAtmosphere, component);
        }
        cmd.SetPipelineState(rhi::PipelineState().SetFrontFace(rhi::FrontFace::CounterClockwise));
    }

    cmd.SetPipelineState(rhi::PipelineState().SetBlend(false).SetDepthWrite(true));
}

void Renderer::RenderOneilAtmosphere(rhi::CommandList& cmd, const FrameContext& ctx,
                                     const RenderableAtmosphere& renderableAtmosphere,
                                     const RenderableSceneComponent& component) {
    Atmosphere& atmosphere = *renderableAtmosphere.atmosphere;
    const PlanetaryRing* ring = component.planetaryRing.get();
    cmd.SetPipelineState(rhi::PipelineState().SetBlendFunc(rhi::BlendFactor::One, rhi::BlendFactor::One));

    static const ShaderSpec nearSpec = Program("atmosphere.vs", "atmosphere.fs", NearSurfaceDefine());
    Shader& program =
        ShellProgram(*_mainAtmosphereShader, _nearAtmosphereShader, nearSpec, NearSurface(atmosphere, ctx.view));
    program.Use();
    program.SetFloat("uSurfaceDim", _magneticFieldsEnabled ? 0.40f : 1.0f);
    program.SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
    program.SetVec2("shadowDepthRange", component.shadowDepthRange);
    program.SetFloat("bias", ShadowBias(component, kShellShadowBias));
    // Per-shell constants are set here, after Use(): Saturn and Titan share a component,
    // so setting them at placement time let the last shell's values win for both.
    program.SetVec3("C_R", atmosphere.GetAtmosphereColor());
    program.SetFloat("innerRadius", atmosphere.GetInnerRadius());
    program.SetFloat("outerRadius", atmosphere.GetOuterRadius());
    program.SetVec3("camPosition", ctx.view.eyePosition - atmosphere.GetPosition());
    program.SetVec3("lightPos", ctx.scene.sun->GetPosition() - atmosphere.GetPosition());
    program.SetVec3("mieTint", atmosphere.GetMieTint());
    program.SetFloat("SCALE_H_FACTOR", atmosphere.GetRow().oneil.hScaleFactor);
    program.SetFloat("SCALE_L_FACTOR", 1.0f);
    program.SetFloat("earthSizeCoefficient", renderableAtmosphere.parentEarthSizeCoefficient);
    program.SetBool("isUseToneMapping", atmosphere.GetRow().oneil.toneMapping);
    program.SetMat3("bodyToSphere", atmosphere.GetBodyToSphere());
    program.SetBool("isNearbyPlanetaryRing", ring != nullptr);

    if (ring) {
        program.SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
        program.SetFloat("ringParentPlanetRadiusSquared",
                         ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
        program.SetBool("isUseSphereIntersect", ring->GetParent() != atmosphere.GetParent());

        program.SetVec3("ringCenter", ring->GetPosition());
        program.SetVec3("ringNormal", ring->GetRingNormal());
        program.SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
        cmd.SetBindGroup(rhi::BindGroup().Texture(Unit(TextureUnit::Ring), ring->GetRingTexture()));
    }

    atmosphere.Draw(program);
}

void Renderer::RenderPbrAtmosphere(rhi::CommandList& cmd, const FrameContext& ctx,
                                   const RenderableAtmosphere& renderableAtmosphere,
                                   const RenderableSceneComponent& component) {
    Atmosphere& atmosphere = *renderableAtmosphere.atmosphere;
    const PlanetaryRing* ring = component.planetaryRing.get();
    const BodyCatalog::AtmosphereRow& row = atmosphere.GetRow();
    const AtmosphereModel::Params params = AtmosphereModel::FromCatalog(row.physical);
    const glm::vec3 center = atmosphere.GetPosition();
    const glm::vec3 camera = ctx.view.eyePosition;

    // Premultiplied: rgb is in-scattered light, alpha what survives of the scene behind.
    cmd.SetPipelineState(rhi::PipelineState().SetBlendFunc(rhi::BlendFactor::One, rhi::BlendFactor::SrcAlpha));

    static const ShaderSpec nearSpec = Program("atmosphere.vs", "atmospherePbr.fs", NearSurfaceDefine());
    Shader& shader =
        ShellProgram(*_pbrAtmosphereShader, _nearPbrAtmosphereShader, nearSpec, NearSurface(atmosphere, ctx.view));
    shader.Use();
    shader.SetFloat("uSurfaceDim", _magneticFieldsEnabled ? 0.40f : 1.0f);
    shader.SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
    shader.SetVec3("camPosition", camera - center);
    shader.SetVec3("lightPos", ctx.scene.sun->GetPosition() - center);
    shader.SetFloat("kmPerSceneUnit", atmosphere.GetKmPerSceneUnit());
    shader.SetBool("isCameraInsideShell", atmosphere.Contains(camera));
    // Oblate giants: the ray is traced where the cloud deck is a sphere (BodyShape.h).
    shader.SetMat3("bodyToSphere", atmosphere.GetBodyToSphere());
    shader.SetMat3("sphereToBody", atmosphere.GetSphereToBody());
    shader.SetInt("uSteps", _pbrAtmosphereSteps);
    shader.SetFloat("uExposure", row.physical.exposure);

    shader.SetFloat("uAtmosphere.bottomRadius", static_cast<float>(params.bottomRadius));
    shader.SetFloat("uAtmosphere.topRadius", static_cast<float>(params.topRadius));
    shader.SetVec3("uAtmosphere.rayleighScattering", glm::vec3(params.rayleighScattering));
    shader.SetFloat("uAtmosphere.rayleighScaleHeight", static_cast<float>(params.rayleighScaleHeight));
    shader.SetVec3("uAtmosphere.mieScattering", glm::vec3(params.mieScattering));
    shader.SetVec3("uAtmosphere.mieExtinction", glm::vec3(params.mieExtinction));
    shader.SetFloat("uAtmosphere.mieScaleHeight", static_cast<float>(params.mieScaleHeight));
    shader.SetFloat("uAtmosphere.miePhaseG", static_cast<float>(params.miePhaseG));
    shader.SetVec3("uAtmosphere.absorptionExtinction", glm::vec3(params.absorptionExtinction));
    shader.SetFloat("uAtmosphere.absorptionCenter", static_cast<float>(params.absorptionCenter));
    shader.SetFloat("uAtmosphere.absorptionHalfWidth", static_cast<float>(params.absorptionHalfWidth));

    cmd.SetBindGroup(rhi::BindGroup()
                         .Texture(Unit(TextureUnit::TransmittanceLut), atmosphere.GetTransmittanceLut())
                         .Texture(Unit(TextureUnit::MultiScatteringLut), atmosphere.GetMultiScatteringLut()));

    shader.SetBool("isNearbyPlanetaryRing", ring != nullptr);
    if (ring) {
        shader.SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
        shader.SetFloat("ringParentPlanetRadiusSquared",
                        ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
        shader.SetBool("isUseSphereIntersect", ring->GetParent() != atmosphere.GetParent());
        // The ring's own planet (Saturn, Uranus): its ring shadow is a band across the disc,
        // so it is tested per view-ray sample. A moon (Titan) tests once per fragment.
        shader.SetBool("isRingShadowPerSample", ring->GetParent() == atmosphere.GetParent());
        shader.SetVec3("ringCenter", ring->GetPosition());
        shader.SetVec3("ringNormal", ring->GetRingNormal());
        shader.SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
        cmd.SetBindGroup(rhi::BindGroup().Texture(Unit(TextureUnit::Ring), ring->GetRingTexture()));
    }

    // A moon's umbra darkens the air above it as well as the ground. Only the primary's
    // atmosphere has casters (its moons); a moon's own atmosphere skips this.
    const EclipseUmbra umbra =
        atmosphere.GetParent() == component.planet ? ComputeEclipseUmbra(ctx, component) : EclipseUmbra{};
    shader.SetBool("hasEclipseCaster", umbra.active);
    if (umbra.active) {
        shader.SetVec3("eclipseCasterCenter", umbra.casterCenter - center);
        shader.SetFloat("eclipseCasterRadius", umbra.casterRadius);
        shader.SetFloat("eclipseStarRadius", umbra.starRadius);
    }

    atmosphere.Draw(shader);
}

void Renderer::RenderClouds(rhi::CommandList& cmd, const FrameContext& ctx, const RenderableSceneComponent& component) {
    const Clouds* renderableClouds = component.clouds.get();
    if (renderableClouds && renderableClouds->IsVisible()) {
        cmd.SetPipelineState(rhi::PipelineState()
                                 .SetDepthWrite(false)
                                 .SetBlend(true)
                                 .SetBlendFunc(rhi::BlendFactor::SrcAlpha, rhi::BlendFactor::OneMinusSrcColor)
                                 .SetCullFace(false));

        static const ShaderSpec nearSpec = Program("planetLighting.vs", "cloudsLighting.fs", NearSurfaceDefine());
        const bool near = NearSurface(*renderableClouds, ctx.view);
        Shader& program = ShellProgram(*_mainCloudsShader, _nearCloudsShader, nearSpec, near);
        program.Use();
        program.SetFloat("uSurfaceDim", _magneticFieldsEnabled ? 0.28f : 1.0f);
        program.SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
        program.SetFloat("bias", ShadowBias(component, kShellShadowBias));
        if (&program != _mainCloudsShader) {
            SetMeshAnchor(program, *renderableClouds, ctx.view);
        }
        renderableClouds->Draw(program);

        cmd.SetPipelineState(rhi::PipelineState().SetCullFace(true).SetBlend(false).SetDepthWrite(true));
    }
}

void Renderer::RenderPlanetaryRing(rhi::CommandList& cmd, const Shader& shader,
                                   const RenderableSceneComponent& component) const {
    const PlanetaryRing* planetaryRing = component.planetaryRing.get();
    if (planetaryRing && planetaryRing->IsVisible()) {
        cmd.SetPipelineState(rhi::PipelineState().SetBlend(true).SetBlendFunc(rhi::BlendFactor::SrcAlpha,
                                                                              rhi::BlendFactor::OneMinusSrcAlpha));

        shader.Use();
        shader.SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
        shader.SetFloat("bias", ShadowBias(component, kShellShadowBias));
        planetaryRing->Draw(shader);

        cmd.SetPipelineState(rhi::PipelineState().SetBlend(false));
    }
}

Shader& Renderer::ShellProgram(Shader& normal, Shader*& nearCache, const ShaderSpec& nearSpec, bool near) {
    if (!near) {
        return normal;
    }
    if (!nearCache) {
        nearCache = _shaders.GetIfReady(nearSpec);
    }
    return nearCache ? *nearCache : normal;
}
