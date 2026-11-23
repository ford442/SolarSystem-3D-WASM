// A scene component's colour pass (docs/ARCHITECTURE.md §7.5): its planet and moons with their
// catalog materials (or terrain tiles, §11.3), the star when this is the nearest component,
// then its shells (ShellPasses.cpp), aurora and ring. Plus the eclipse umbra a moon casts on its
// planet (§11) and the NEAR_SURFACE helpers (§11.1).
#include "Renderer.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "PassCommon.h"
#include "Solar_System/CatalogMaterial.h"
#include "Solar_System/EclipseCaster.h"
#include "Solar_System/Planet.h"
#include "Solar_System/Satellite.h"
#include "Solar_System/SceneScale.h"
#include "Solar_System/Star.h"
#include "core/CameraNavigation.h"
#include "gfx/MeshAnchor.h"
#include "gfx/MeshHolder.h"
#include "gfx/Frustum.h"
#include "gfx/Shader.h"
#include "gfx/SurfacePatch.h"
#include "gfx/SurfacePatchMesh.h"
#include "gfx/TextureUnits.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include "render/terrain/TerrainRenderer.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

void Renderer::SceneComponent(rhi::CommandList& cmd, int view, std::size_t drawIndex) {
    const FrameContext& ctx = View(view);
    const RenderableSceneComponent& component = DrawnComponent(ctx, drawIndex);
    // BeginView set this view's viewport after the shadow pass changed it.
    if (component.visible) {
        ++_stats.componentDraws;
    }
    if (component.planetaryRing) {
        cmd.SetBindGroup(rhi::BindGroup().Texture(Unit(TextureUnit::Ring), component.planetaryRing->GetRingTexture()));
    }

    DrawLitBody(ctx, *component.planet, component, ComputeEclipseUmbra(ctx, component));
    // The umbra decal is the primary's alone: a moon drawn with it would shadow its own
    // night side with itself as the caster.
    for (const auto& satellite : component.satellites) {
        DrawLitBody(ctx, *satellite, component, EclipseUmbra{});
    }

    if (const RenderableSceneComponent* nearest = ctx.scene.Nearest(); nearest && component.planet == nearest->planet)
        ProcessStarRendering(cmd, ctx);

    // The LUT atmosphere attenuates what is behind it, so the cloud deck has to be there
    // first; the additive O'Neil shell keeps its historical order.
    const bool physicalAtmosphere =
        std::any_of(component.atmospheres.begin(), component.atmospheres.end(),
                    [](const RenderableAtmosphere& a) { return a.atmosphere->IsPhysicalPathActive(); });
    if (physicalAtmosphere) {
        RenderClouds(cmd, ctx, component);
        RenderAtmospheres(cmd, ctx, component);
    } else {
        RenderAtmospheres(cmd, ctx, component);
        RenderClouds(cmd, ctx, component);
    }
    RenderAurora(cmd, ctx, component);
    RenderPlanetaryRing(cmd, *_mainRingShader, component);
}

Renderer::EclipseUmbra Renderer::ComputeEclipseUmbra(const FrameContext& ctx,
                                                     const RenderableSceneComponent& renderableComponent) const {
    // Real physical constants, because the eclipse *decision* has to be made at true scale.
    // The scene draws body radii, moon orbits, and planet orbits at three different
    // exaggerations; at art scale the Moon sits 12.5 Earth-radii away instead of 60, so its
    // 5.1 deg of inclination cannot clear Earth's disc and a shadow would land on nearly
    // every new moon. Deciding in kilometres and only *drawing* at art scale is what keeps
    // eclipses as rare as they are. See docs/ARCHITECTURE.md § 11.
    constexpr float kEarthRadiusKm = 6371.0f;
    const auto kStarRadiusKm = static_cast<float>(SceneScale::StarRadiusKm()); // the active star (Sol: 695 700)
    constexpr float kAuKm = 149597870.7f;

    const std::shared_ptr<Planet>& planet = renderableComponent.planet;
    if (!planet || !ctx.scene.sun || renderableComponent.satellites.empty()) {
        return {};
    }

    const glm::vec3 planetPosition = planet->GetPosition();
    const glm::vec3 sunPosition = ctx.scene.sun->GetPosition();
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

        const float miss = EclipseCaster::ShadowAxisMissDistance(planetPosition, sunPosition, satellite->GetPosition());
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
        return {};
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
    // (Referenced to 1 AU, as for the Sun seen from Earth; only systems with moons cast.)
    const float starRadius = lightDistance * (kStarRadiusKm / kAuKm);

    // Low drops the penumbra entirely: a zero-radius star gives a hard-edged disc, which is
    // one smoothstep and no extra bandwidth.
    const bool softPenumbra = ctx.sim.qualityPreset >= 2;

    EclipseUmbra umbra;
    umbra.active = true;
    umbra.casterCenter = caster->GetPosition();
    umbra.casterRadius = casterRadius;
    umbra.starRadius = softPenumbra ? starRadius : 0.0f;
    return umbra;
}

const Shader& Renderer::PlanetProgram(unsigned variant) {
    using CatalogMaterial::kNearSurface;
    using CatalogMaterial::kProcedural;
    using CatalogMaterial::kSphereIntersect;
    // Not built yet (or failed): drop the maps, keeping the sphere test and, close to a
    // surface, fragment depth (without it the facets around the eye clip wrongly); then drop
    // NEAR_SURFACE too. Each candidate is submitted the first time it is asked for. A
    // procedural body keeps PROCEDURAL_SURFACE throughout: it has no maps to fall back to.
    const unsigned keep = variant & kProcedural;
    const unsigned candidates[] = {variant, (variant & (kSphereIntersect | kNearSurface)) | keep,
                                   variant & ~kNearSurface, (variant & kSphereIntersect) | keep};
    for (const unsigned candidate : candidates) {
        Shader*& program = _planetVariants[candidate];
        if (!program) {
            program = _shaders.GetIfReady(RenderPasses::PlanetSpec(candidate));
        }
        if (program) {
            return *program;
        }
    }
    return *_mainPlanetShader;
}

bool Renderer::NearSurface(const SpaceObject& object, const CameraView& view) {
    const BoundingSphere bounds = object.GetBoundingSphere();
    const auto reach = static_cast<float>(1.0 + CameraNavigation::kNearSurfaceRadii);
    return glm::distance(view.eyePosition, bounds.center) < bounds.radius * reach;
}

void Renderer::SetMeshAnchor(const Shader& program, const SpaceObject& object, const CameraView& view) {
    // The body's render-frame position in double, rather than the model matrix's float
    // translation: that rounding is exactly what the anchor exists to keep out.
    const glm::dvec3 bodyRender = object.GetWorldPosition() - view.renderOrigin;
    const glm::dvec3 eyeRender(view.eyePosition);
    const MeshAnchor anchor = ComputeMeshAnchor(glm::mat3(object.GetModelMatrix()), eyeRender - bodyRender, eyeRender);
    program.SetVec3("meshAnchor", anchor.anchor);
    program.SetVec3("meshAnchorOffset", anchor.anchorOffset);
}

void Renderer::DrawSurfacePatch(const Shader& program, const SpaceObject& body, const CameraView& view) {
    const std::vector<std::string>& defines = program.Spec().defines;
    if (!_surfacePatch ||
        std::find(defines.begin(), defines.end(), RenderPasses::NearSurfaceDefine()[0]) == defines.end()) {
        return; // a fallback program would place the eye-relative positions as model ones
    }
    const MeshHolder& mesh = body.GetModel();
    const MeshHolder::Triangles& triangles = mesh.GetTriangles();
    const glm::dmat3 shape{glm::mat3(body.GetModelMatrix())};
    // Built around the head, not this eye, so both XR eyes share one patch.
    const glm::dvec3 eyeModel = glm::inverse(shape) * (view.renderOrigin - body.GetWorldPosition());
    if (!SurfacePatch::InReach(shape, eyeModel, mesh.BoundingRadius(), triangles.maxEdge, view.nearPlane)) {
        return;
    }
    const SurfacePatchKey key{&body, eyeModel, view.nearPlane};
    if (key.body != _surfacePatchKey.body || key.eyeModel != _surfacePatchKey.eyeModel ||
        key.nearPlane != _surfacePatchKey.nearPlane) {
        _surfacePatch->Stream(
            SurfacePatch::Build(triangles.vertices, triangles.indices, shape, eyeModel, view.nearPlane));
        _surfacePatchKey = key;
    }
    // The positions are offsets from eyeModel, which is the render origin: no anchor, and
    // an offset that is zero up to rounding.
    program.SetVec3("meshAnchor", glm::vec3(0.0f));
    program.SetVec3("meshAnchorOffset", glm::vec3(body.GetWorldPosition() + shape * eyeModel - view.renderOrigin));
    _surfacePatch->Draw();
}

void Renderer::ConfigurePlanetProgram(const FrameContext& ctx, const Shader& program,
                                      const RenderableSceneComponent& component, const EclipseUmbra& umbra) const {
    // Every variant gets the full set; the value cache turns the repeats into lookups.
    program.Use();
    program.SetVec3("starGlowTint", ctx.scene.sun->GetGlowTintMult());
    program.SetFloat("uSurfaceDim", _magneticFieldsEnabled ? 0.28f : 1.0f);
    program.SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
    program.SetVec2("shadowDepthRange", component.shadowDepthRange);
    program.SetFloat("bias", RenderPasses::ShadowBias(component, RenderPasses::kSurfaceShadowBias));

    program.SetBool("hasEclipseCaster", umbra.active);
    if (umbra.active) {
        program.SetVec3("eclipseCasterCenter", umbra.casterCenter);
        program.SetFloat("eclipseCasterRadius", umbra.casterRadius);
        program.SetFloat("eclipseStarRadius", umbra.starRadius);
    }

    const PlanetaryRing* ring = component.planetaryRing.get();
    program.SetBool("isNearbyPlanetaryRing", ring != nullptr);
    if (ring) {
        program.SetVec3("ringCenter", ring->GetPosition());
        program.SetVec3("ringNormal", ring->GetRingNormal());
        program.SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
        // USE_SPHERE_INTERSECT (the ringed planet's moons): the planet that hides the ring.
        const auto& parent = ring->GetParent();
        program.SetVec3("parentPlanetCenter", parent->GetPosition());
        program.SetFloat("parentPlanetRadiusSquared", parent->GetRadius() * parent->GetRadius());
    }
}

void Renderer::DrawLitBody(const FrameContext& ctx, const SpaceObject& body, const RenderableSceneComponent& component,
                           const EclipseUmbra& umbra) {
    if (!body.IsVisible()) {
        return;
    }
    ++_stats.bodyDraws;
    if (const SceneSnapshot::TerrainBody* terrain = ctx.scene.TerrainFor(&body)) {
        if (!terrain->tiles) {
            // Within reach but still the sphere: submit the programs its descent will need.
            TerrainProgram(body.ShaderVariant() | CatalogMaterial::kNearSurface);
            TerrainShadowProgram();
        } else if (DrawTerrain(ctx, body, *terrain->tiles, component, umbra)) {
            return;
        }
    }
    const bool near = NearSurface(body, ctx.view);
    const unsigned variant = body.ShaderVariant() | (near ? CatalogMaterial::kNearSurface : 0u);
    const Shader& program = PlanetProgram(variant);
    ConfigurePlanetProgram(ctx, program, component, umbra);
    if (near) {
        // Harmless on a fallback program without NEAR_SURFACE: it has no such uniforms.
        SetMeshAnchor(program, body, ctx.view);
    }
    body.Draw(program);
    if (near) {
        DrawSurfacePatch(program, body, ctx.view);
    }
}

const Shader* Renderer::TerrainProgram(unsigned variant) {
    using CatalogMaterial::kNearSurface;
    // Terrain bodies are textured (render.terrain is a Sol feature): never PROCEDURAL_SURFACE.
    variant &= ~CatalogMaterial::kProcedural;
    // As PlanetProgram: drop the maps, then NEAR_SURFACE. A tile placed without it is in the
    // same place; only its depth is per vertex.
    const unsigned candidates[] = {variant, variant & kNearSurface, variant & ~kNearSurface, 0u};
    for (const unsigned candidate : candidates) {
        Shader*& program = _terrainVariants[candidate];
        if (!program) {
            program = _shaders.GetIfReady(RenderPasses::TerrainSpec(candidate));
        }
        if (program) {
            return program;
        }
    }
    return nullptr;
}

bool Renderer::DrawTerrain(const FrameContext& ctx, const SpaceObject& body, const Terrain::ActiveBody& tiles,
                           const RenderableSceneComponent& component, const EclipseUmbra& umbra) {
    const unsigned material = body.ShaderVariant() & ~CatalogMaterial::kNearSurface;
    const Shader* far = TerrainProgram(material);
    if (!_terrainRenderer || !far) {
        return false;
    }
    const Shader* near = TerrainProgram(material | CatalogMaterial::kNearSurface);
    const glm::mat4 model = body.GetModelMatrix();
    const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
    for (const auto& [program, pass] :
         {std::pair{far, TerrainRenderer::Pass::ColourFar}, std::pair{near, TerrainRenderer::Pass::ColourNear}}) {
        ConfigurePlanetProgram(ctx, *program, component, umbra);
        body.BindMaterial(*program);
        program->SetMat3("normalMatrix", normalMatrix);
        _terrainRenderer->Draw(*program, tiles, model, pass);
    }
    return true;
}

void Renderer::ForgetTerrainTiles(const std::vector<uint64_t>& uids) {
    if (_terrainRenderer && !uids.empty()) {
        _terrainRenderer->Forget(uids);
    }
}

std::size_t Renderer::TerrainTextureCount() const {
    return _terrainRenderer ? _terrainRenderer->TextureCount() : 0;
}
