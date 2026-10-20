// Shadow/color passes for planets, atmospheres, rings, clouds and text overlays, plus the
// GPU-resource setup they need. Renderer:: methods; everything a pass reads about the
// camera and the scene comes in through its FrameContext (FrameContext.h).
#include "Solar_System/TexturePaths.h"
#include "Renderer.h"
#include "gfx/TextureUnits.h"
#include "JsBridge.h"
#include "QualitySettings.h"
#include "ResourceManifest.h"
#include "Solar_System/EclipseCaster.h"
#include "Solar_System/AtmosphereModel.h"
#include "SimState.h"
#include "Solar_System/OrbitLayout.h"
#include "Auxiliary_Modules/Ephemeris.h"
#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include <algorithm>
#include <array>
#include <deque>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

using namespace std;

namespace {

ShaderSpec Program(const char* vertex, const char* fragment, vector<string> defines = {}) {
    return ShaderSpec(string("resource/shaders/") + vertex, string("resource/shaders/") + fragment, std::move(defines));
}

ShaderSpec PlanetSpec(unsigned variant) {
    return Program("planetLighting.vs", "planetLighting.fs", CatalogMaterial::Defines(variant));
}

// Every planetLighting variant some catalog row can draw with (CatalogMaterial::PossibleVariants).
vector<unsigned> CatalogPlanetVariants() {
    vector<unsigned> variants{0};
    for (const BodyCatalog::Entry& entry : BodyCatalog::kEntries) {
        for (const unsigned variant : CatalogMaterial::PossibleVariants(CatalogMaterial::Material::FromEntry(entry))) {
            if (std::find(variants.begin(), variants.end(), variant) == variants.end()) {
                variants.push_back(variant);
            }
        }
    }
    return variants;
}

} // namespace

void Renderer::Init(const SimState& sim, FT_Library ft, glm::ivec2 display) {
    const auto qualitySettings = GetQualitySettings(
        sim.shadowQuality == 0 ? sim.qualityPreset : sim.shadowQuality - 1, sim.isMobileWeb);
    _shadowMapFBO = make_unique<ShadowMapFBO>(qualitySettings.shadowResolution, qualitySettings.shadowResolution);

    // Submit every program before waiting on any: with KHR_parallel_shader_compile the
    // driver compiles them side by side, and Get() below only waits for what is left.
    const ShaderSpec hdrSpec = Program("passThrough.vs", "hdr.fs");
    const ShaderSpec textSpec = Program("text.vs", "text.fs");
    const ShaderSpec shadowSpec = Program("shadowMap.vs", "shadowMap.fs");
    const ShaderSpec skyBoxSpec = Program("skyBox.vs", "skyBox.fs");
    const ShaderSpec starSpec = Program("star.vs", "star.fs");
    const ShaderSpec coronaSpec = Program("starCorona.vs", "starCorona.fs");
    const ShaderSpec atmosphereSpec = Program("atmosphere.vs", "atmosphere.fs");
    const ShaderSpec pbrAtmosphereSpec = Program("atmosphere.vs", "atmospherePbr.fs");
    const ShaderSpec cloudsSpec = Program("planetLighting.vs", "cloudsLighting.fs");
    const ShaderSpec ringSpec = Program("planetaryRingLighting.vs", "planetaryRingLighting.fs");
    const ShaderSpec lensFlareSpec = Program("lensFlare.vs", "lensFlare.fs");
    const ShaderSpec starGlowSpec = Program("starGlow.vs", "starGlow.fs");
    const ShaderSpec coronaVolumeSpec = Program("starCoronaVolume.vs", "starCoronaVolume.fs");
    const vector<unsigned> planetVariants = CatalogPlanetVariants();
    for (const ShaderSpec* spec : {&hdrSpec, &textSpec, &shadowSpec, &skyBoxSpec, &starSpec, &coronaSpec,
                                   &atmosphereSpec, &pbrAtmosphereSpec, &cloudsSpec, &ringSpec, &lensFlareSpec,
                                   &starGlowSpec, &coronaVolumeSpec}) {
        _shaders.Prefetch(*spec);
    }
    for (const unsigned variant : planetVariants) {
        _shaders.Prefetch(PlanetSpec(variant));
    }
    _hdrEnabled = qualitySettings.enableHdr;
    _hdrShader = &_shaders.Get(hdrSpec);
    _hdr = make_unique<HDR>(*_hdrShader, display.x, display.y, _hdrEnabled);
    _hudProjection = glm::ortho(0.0f, static_cast<float>(display.x), 0.0f, static_cast<float>(display.y));
    // HDR turns itself off when the GPU cannot give it a complete float FBO; follow it,
    // otherwise the composite path would keep sampling an empty buffer.
    _hdrEnabled = _hdr->IsEnabled();
    LogQualityTier(qualitySettings, _hdrEnabled, sim.shadowQuality);

    _mainTextShader = &_shaders.Get(textSpec);
    _textRenderer = make_unique<TextRenderer>(ft, "resource/fonts/Arial.ttf");
    FT_Done_FreeType(ft);
    _shadowMapShader = &_shaders.Get(shadowSpec);
    _mainSkyBoxShader = &_shaders.Get(skyBoxSpec);
    _mainStarShader = &_shaders.Get(starSpec);
    _mainCoronaStarShader = &_shaders.Get(coronaSpec);
    _mainAtmosphereShader = &_shaders.Get(atmosphereSpec);
    // Already reported by Shader (and fatal to the CI shader self-test); the O'Neil shell
    // still draws every atmosphere.
    _pbrAtmosphereShader = _shaders.TryGet(pbrAtmosphereSpec);
    if (!_pbrAtmosphereShader) {
        std::cerr << "[Atmosphere] LUT atmosphere shader unavailable; using the O'Neil shell" << std::endl;
    }
    _mainCloudsShader = &_shaders.Get(cloudsSpec);
    _mainRingShader = &_shaders.Get(ringSpec);
    _lensFlareShader = &_shaders.Get(lensFlareSpec);
    _starGlowShader = &_shaders.Get(starGlowSpec);
    _coronaVolumeShader = _shaders.TryGet(coronaVolumeSpec);
    if (!_coronaVolumeShader) {
        std::cerr << "[Corona] volumetric corona shader unavailable; using the flat corona" << std::endl;
    }
    // The base variant must build (bodies hold it from construction); the others fall back
    // to it in PlanetProgram if their own compile failed.
    _planetVariants.fill(nullptr);
    _mainPlanetShader = &_shaders.Get(PlanetSpec(0));
    for (const unsigned variant : planetVariants) {
        _planetVariants[variant] = _shaders.TryGet(PlanetSpec(variant));
    }

    // After every program is built: a missing skybox face aborts start-up (native has no
    // checkerboard fallback), and the native CI smoke run should still have compiled the
    // shaders by then.
    _skyBox = make_unique<SkyBox>(GetSkyBoxFaces());
    _lensFlare = make_unique<LensFlare>(*_lensFlareShader, TextureImage2D(TexturePaths::Resolve("resource/textures_low/flares_bright_Low.dds")),
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
    try {
        _observeSky = make_unique<ObserveSky>();
    } catch (const std::exception&) {
        std::cerr << "[Observe] sky shaders unavailable; Observe mode will draw an empty sky" << std::endl;
        _observeSky.reset();
    }
    _orbitPathRenderer = make_unique<OrbitPathRenderer>();
    _xrPointerRenderer = make_unique<XrPointerRenderer>();
    _magneticFieldRenderer = make_unique<MagneticFieldLineRenderer>();
    {
        const auto fieldQuality = GetQualitySettings(sim.qualityPreset, sim.isMobileWeb);
        _magneticFieldBloom = make_unique<MagneticFieldBloom>(display.x, display.y,
                                                              fieldQuality.enableMagneticBloom);
        ApplyEffectQuality(fieldQuality);
    }
}

void Renderer::Resize(uint16_t width, uint16_t height) {
    _hudProjection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
    if (_hdr) {
        _hdr->Resize(width, height);
    }
    if (_magneticFieldBloom) {
        _magneticFieldBloom->Resize(width, height);
    }
}

void Renderer::Release() {
    // Users before the shaders they hold by reference (LensFlare, HDR).
    _missionPathMeshes.clear();
    _magneticFieldBloom.reset();
    _magneticFieldRenderer.reset();
    _magneticFieldsBuilt = false;
    _xrPointerRenderer.reset();
    _orbitPathRenderer.reset();
    _observeSky.reset();
    _lensFlare.reset();
    _skyBox.reset();
    _hdr.reset();
    _textRenderer.reset();
    _shadowMapFBO.reset();
    for (Shader** shader : {&_shadowMapShader, &_mainSkyBoxShader, &_mainTextShader, &_mainStarShader,
                            &_mainCoronaStarShader, &_mainPlanetShader, &_mainAtmosphereShader,
                            &_mainCloudsShader, &_mainRingShader, &_hdrShader, &_lensFlareShader,
                            &_starGlowShader, &_pbrAtmosphereShader, &_coronaVolumeShader}) {
        *shader = nullptr;
    }
    _planetVariants.fill(nullptr);
    _shaders.Clear();
}

void Renderer::ApplyRenderResources(const SimState& sim, uint16_t shadowResolution, bool enableHdr, glm::ivec2 display) {
    _hdrEnabled = enableHdr;

    if (_shadowMapFBO && sim.shadowQuality > 0) {
        _shadowMapFBO->Resize(shadowResolution, shadowResolution);
    }

    if (_hdr) {
        _hdr->SetEnabled(enableHdr, display.x, display.y);
        // SetEnabled can refuse (no renderable float buffers / incomplete FBO).
        _hdrEnabled = _hdr->IsEnabled();
    }
    if (_magneticFieldBloom) {
        const auto bloomSettings = GetQualitySettings(sim.qualityPreset, sim.isMobileWeb);
        _magneticFieldBloom->SetEnabled(bloomSettings.enableMagneticBloom, display.x, display.y);
    }
}

void Renderer::ResizeShadowMap(uint16_t resolution) {
    if (_shadowMapFBO) {
        _shadowMapFBO->Resize(resolution, resolution);
    }
}

void Renderer::ApplyEffectQuality(const QualityTierSettings& settings) {
    _pbrAtmosphereSteps = settings.enablePbrAtmosphere ? settings.atmosphereSteps : 0;
    _coronaSlices = settings.enableVolumetricCorona ? settings.coronaSlices : 0;
    _observeStarCount = settings.observeStarCount;
    if (_observeSky) {
        _observeSky->SetStarLimit(_observeStarCount);
    }
}

void Renderer::BeginFrame() {
    _configuredView = nullptr;
    _uploadedViews = 0;
    _stats = FramePasses::Stats{};
}

void Renderer::BeginView(const FrameContext& ctx) {
    const glm::ivec4& viewport = ctx.view.viewport;
    glViewport(viewport.x, viewport.y, viewport.z, viewport.w);
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
        data.time = static_cast<float>(glfwGetTime());
        data.exposure = _starTuning.exposure;
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

void Renderer::RenderBackground(const FrameContext& ctx) {
    if (ctx.scene.observeActive) {
        // Ground-level sky pass only: no scene meshes, orbit lines, glow or lens flare.
        RenderObserveSky(ctx);
        return;
    }
    _skyBox->Render(*_mainSkyBoxShader);
    RenderOrbitPaths(ctx);
    RenderMissionPaths(ctx);
    RenderXrPointers(ctx);
    RenderStarCorona(ctx);
}

void Renderer::RenderForeground(const FrameContext& ctx) {
    if (ctx.scene.observeActive) {
        return;
    }
    RenderStarCoronaVolume(ctx);
    RenderAsteroidField(ctx);
    RenderMagneticFields(ctx);
}

void Renderer::RenderMonoEffects(const FrameContext& ctx) {
    if (ctx.scene.observeActive) {
        return;
    }
    RenderPlanetProxyMarkers(ctx);
    RenderStarEffects(ctx);
}

void Renderer::RenderHud(const FrameContext& ctx) {
    if (ctx.scene.observeActive) {
        RenderObserveHud(ctx);
        return;
    }
    if (_overlayToggles.planetStarDistances || _overlayToggles.satelliteDistances) {
        RenderPlanetSatelliteStarDistances(ctx);
    }
    if (_overlayToggles.hints) {
        RenderHints(ctx);
    }
}

void Renderer::ShadowMapPass(const FrameContext& ctx, const RenderableSceneComponent& component) {
    // Under WebXR the frame composites into the XRWebGLLayer framebuffer, not FBO 0, so
    // every exit below restores DefaultFramebuffer() instead of hardcoding 0. Getting
    // that wrong is what used to force the whole pass to be skipped in VR, leaving the
    // lighting shaders reading a cleared depth map as "fully lit".
#ifdef __EMSCRIPTEN__
    if (ctx.xr && ctx.defaultFramebuffer == 0) {
        // No layer framebuffer to return to (JS could not register it). Fall back to the
        // old behaviour rather than composite the eyes into FBO 0; RenderXrStereoFrame
        // logs the reason once per session.
        return;
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, _shadowMapFBO->GetFBO());
    glClear(GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, _shadowMapFBO->GetShadowMapWidth(), _shadowMapFBO->GetShadowMapHeight());

    // A cleared depth texture contains 1.0 everywhere, which the existing
    // lighting shaders interpret as fully lit. Keep the texture bound but skip
    // all shadow geometry when shadows are disabled.
    if (ctx.sim.shadowQuality == 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFramebuffer);
        return;
    }

    ++_stats.shadowPasses;
    _shadowMapShader->Use();
    _shadowMapShader->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);

    component.planet->SetShader(*_shadowMapShader);
    component.planet->Render();

    for (const auto& satellite : component.satellites) {
        satellite->SetShader(*_shadowMapShader);
        satellite->Render();
    }

    RenderPlanetaryRing(*_shadowMapShader, component.planetaryRing.get(), component.lightSpaceMatrix);
    glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFramebuffer);
}

void Renderer::RenderPass(const FrameContext& ctx, const RenderableSceneComponent& component) {
    // BeginView set this view's viewport after the shadow pass changed it.
    ++_stats.componentDraws;
    if (component.planetaryRing) {
        glBindTextureUnit(Unit(TextureUnit::Ring), component.planetaryRing->GetRingTexture());
    }

    DrawLitBody(ctx, *component.planet, component, ComputeEclipseUmbra(ctx, component));
    // The umbra decal is the primary's alone: a moon drawn with it would shadow its own
    // night side with itself as the caster.
    for (const auto& satellite : component.satellites) {
        DrawLitBody(ctx, *satellite, component, EclipseUmbra{});
    }

    if (const RenderableSceneComponent* nearest = ctx.scene.Nearest(); nearest && component.planet == nearest->planet)
        ProcessStarRendering(ctx);

    // The LUT atmosphere attenuates what is behind it, so the cloud deck has to be there
    // first; the additive O'Neil shell keeps its historical order.
    const bool physicalAtmosphere =
        std::any_of(component.atmospheres.begin(), component.atmospheres.end(),
                    [](const RenderableAtmosphere& a) { return a.atmosphere->IsPhysicalPathActive(); });
    if (physicalAtmosphere) {
        RenderClouds(component.clouds.get(), component.lightSpaceMatrix);
        RenderAtmospheres(ctx, component);
    } else {
        RenderAtmospheres(ctx, component);
        RenderClouds(component.clouds.get(), component.lightSpaceMatrix);
    }
    RenderPlanetaryRing(*_mainRingShader, component.planetaryRing.get(), component.lightSpaceMatrix);
}

void Renderer::RenderAtmospheres(const FrameContext& ctx, const RenderableSceneComponent& component) const {
    if (component.atmospheres.empty()) {
        return;
    }
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    for (const auto& renderableAtmosphere : component.atmospheres) {
        // Inside the shell, draw its far side (the near side is behind the camera).
        const bool inside = renderableAtmosphere.atmosphere->Contains(ctx.view.eyePosition);
        if (inside) {
            glFrontFace(GL_CW);
        }
        if (renderableAtmosphere.atmosphere->IsPhysicalPathActive()) {
            RenderPbrAtmosphere(ctx, renderableAtmosphere, component);
        } else {
            RenderOneilAtmosphere(ctx, renderableAtmosphere, component);
        }
        glFrontFace(GL_CCW);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void Renderer::RenderOneilAtmosphere(const FrameContext& ctx, const RenderableAtmosphere& renderableAtmosphere,
                                     const RenderableSceneComponent& component) const {
    Atmosphere& atmosphere = *renderableAtmosphere.atmosphere;
    const PlanetaryRing* ring = component.planetaryRing.get();
    glBlendFunc(GL_ONE, GL_ONE);

    _mainAtmosphereShader->Use();
    _mainAtmosphereShader->SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);
    // Per-shell constants are set here, after Use(): Saturn and Titan share a component,
    // so setting them at placement time let the last shell's values win for both.
    _mainAtmosphereShader->SetVec3("C_R", atmosphere.GetAtmosphereColor());
    _mainAtmosphereShader->SetFloat("innerRadius", atmosphere.GetInnerRadius());
    _mainAtmosphereShader->SetFloat("outerRadius", atmosphere.GetOuterRadius());
    _mainAtmosphereShader->SetVec3("camPosition", ctx.view.eyePosition - atmosphere.GetPosition());
    _mainAtmosphereShader->SetVec3("lightPos", ctx.scene.sun->GetPosition() - atmosphere.GetPosition());
    _mainAtmosphereShader->SetVec3("mieTint", atmosphere.GetMieTint());
    _mainAtmosphereShader->SetFloat("SCALE_H_FACTOR", atmosphere.GetRow().oneil.hScaleFactor);
    _mainAtmosphereShader->SetFloat("SCALE_L_FACTOR", 1.0f);
    _mainAtmosphereShader->SetFloat("earthSizeCoefficient", renderableAtmosphere.parentEarthSizeCoefficient);
    _mainAtmosphereShader->SetBool("isUseToneMapping", atmosphere.GetRow().oneil.toneMapping);
    _mainAtmosphereShader->SetMat3("bodyToSphere", atmosphere.GetBodyToSphere());
    _mainAtmosphereShader->SetBool("isNearbyPlanetaryRing", ring != nullptr);

    if (ring) {
        _mainAtmosphereShader->SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
        _mainAtmosphereShader->SetFloat("ringParentPlanetRadiusSquared", ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
        _mainAtmosphereShader->SetBool("isUseSphereIntersect", ring->GetParent() != atmosphere.GetParent());

        _mainAtmosphereShader->SetVec3("ringCenter", ring->GetPosition());
        _mainAtmosphereShader->SetVec3("ringNormal", ring->GetRingNormal());
        _mainAtmosphereShader->SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
        glBindTextureUnit(Unit(TextureUnit::Ring), ring->GetRingTexture());
    }

    atmosphere.SetShader(*_mainAtmosphereShader);
    atmosphere.Render();
}

void Renderer::RenderPbrAtmosphere(const FrameContext& ctx, const RenderableAtmosphere& renderableAtmosphere,
                                   const RenderableSceneComponent& component) const {
    Atmosphere& atmosphere = *renderableAtmosphere.atmosphere;
    const PlanetaryRing* ring = component.planetaryRing.get();
    const BodyCatalog::AtmosphereRow& row = atmosphere.GetRow();
    const AtmosphereModel::Params params = AtmosphereModel::FromCatalog(row.physical);
    const glm::vec3 center = atmosphere.GetPosition();
    const glm::vec3 camera = ctx.view.eyePosition;

    // Premultiplied: rgb is in-scattered light, alpha what survives of the scene behind.
    glBlendFunc(GL_ONE, GL_SRC_ALPHA);

    Shader& shader = *_pbrAtmosphereShader;
    shader.Use();
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

    glBindTextureUnit(Unit(TextureUnit::TransmittanceLut), atmosphere.GetTransmittanceLut());
    glBindTextureUnit(Unit(TextureUnit::MultiScatteringLut), atmosphere.GetMultiScatteringLut());

    shader.SetBool("isNearbyPlanetaryRing", ring != nullptr);
    if (ring) {
        shader.SetVec3("ringParentPlanetCenter", ring->GetParent()->GetPosition());
        shader.SetFloat("ringParentPlanetRadiusSquared", ring->GetParent()->GetRadius() * ring->GetParent()->GetRadius());
        shader.SetBool("isUseSphereIntersect", ring->GetParent() != atmosphere.GetParent());
        // The ring's own planet (Saturn, Uranus): its ring shadow is a band across the disc,
        // so it is tested per view-ray sample. A moon (Titan) tests once per fragment.
        shader.SetBool("isRingShadowPerSample", ring->GetParent() == atmosphere.GetParent());
        shader.SetVec3("ringCenter", ring->GetPosition());
        shader.SetVec3("ringNormal", ring->GetRingNormal());
        shader.SetVec2("ringInnerOuterRadiuses", glm::vec2(ring->GetInnerRadius(), ring->GetOuterRadius()));
        glBindTextureUnit(Unit(TextureUnit::Ring), ring->GetRingTexture());
    }

    // A moon's umbra darkens the air above it as well as the ground. Only the primary's
    // atmosphere has casters (its moons); a moon's own atmosphere skips this.
    const EclipseUmbra umbra = atmosphere.GetParent() == component.planet ? ComputeEclipseUmbra(ctx, component)
                                                                           : EclipseUmbra{};
    shader.SetBool("hasEclipseCaster", umbra.active);
    if (umbra.active) {
        shader.SetVec3("eclipseCasterCenter", umbra.casterCenter - center);
        shader.SetFloat("eclipseCasterRadius", umbra.casterRadius);
        shader.SetFloat("eclipseStarRadius", umbra.starRadius);
    }

    atmosphere.SetShader(shader);
    atmosphere.Render();
}

void Renderer::RenderClouds(Clouds* renderableClouds, const glm::mat4& lightSpaceMatrix) const {
    if (renderableClouds) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_COLOR);
        glDisable(GL_CULL_FACE);

        _mainCloudsShader->Use();
        _mainCloudsShader->SetMat4("lightSpaceMatrix", lightSpaceMatrix);
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

void Renderer::ProcessStarRendering(const FrameContext& ctx) {
#ifdef __EMSCRIPTEN__
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar(ctx);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    ctx.scene.sun->SetVisibility(1.0f);
#else
    glDepthMask(GL_FALSE);
    glBeginQuery(GL_SAMPLES_PASSED, ctx.scene.sun->GetStarOcclusionValue(0));
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    RenderStar(ctx);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEndQuery(GL_SAMPLES_PASSED);

    glBeginQuery(GL_SAMPLES_PASSED, ctx.scene.sun->GetStarOcclusionValue(1));
    RenderStar(ctx);
    glEndQuery(GL_SAMPLES_PASSED);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

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

void Renderer::RenderStarCorona(const FrameContext& ctx) const {
    if (_coronaSlices > 0 && _coronaVolumeShader) {
        return; // RenderStarCoronaVolume draws the corona instead.
    }
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);

    _mainCoronaStarShader->Use();
    ctx.scene.sun->SetShader(*_mainCoronaStarShader);
    ctx.scene.sun->TakeStarSystemCenter();
    ctx.scene.sun->Render();
    ctx.scene.sun->SetShader(*_mainStarShader);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void Renderer::RenderStarCoronaVolume(const FrameContext& ctx) const {
    if (_coronaSlices <= 0 || !_coronaVolumeShader) {
        return;
    }
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
    const glm::vec3 forward = _coronaSlicesViewAligned ? -glm::normalize(glm::vec3(inverseView[2]))
                                                      : -cameraLocal / distance;

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

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glDisable(GL_CULL_FACE);

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
    ctx.scene.sun->DrawCoronaSlices(_coronaSlices);

    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void Renderer::RenderStar(const FrameContext& ctx) const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    _mainStarShader->Use();
    // Bound here, not with the per-view uniforms: the planets drawn before the star reuse
    // the Diffuse unit, and the photosphere colour used to come from the last moon's map.
    glBindTextureUnit(Unit(TextureUnit::Diffuse), ctx.scene.sun->GetStarSpectrumTexture());
    ctx.scene.sun->TakeStarSystemCenter();
    ctx.scene.sun->Render();

    glDisable(GL_BLEND);
}

void Renderer::RenderStarEffects(const FrameContext& ctx) const {
    const RenderableSceneComponent* nearest = ctx.scene.Nearest();
    const PlanetaryRing* nearestPlanetaryRing = nearest ? nearest->planetaryRing.get() : nullptr;

    optional<RingCameraInfo> ringCameraInfo;
    if (nearestPlanetaryRing) {
        ringCameraInfo = {ctx.view.headPosition, nearestPlanetaryRing->GetPosition(), nearestPlanetaryRing->GetRingNormal(),
                          glm::vec2(nearestPlanetaryRing->GetInnerRadius(), nearestPlanetaryRing->GetOuterRadius()),
                          nearestPlanetaryRing->GetRingTexture()};
    }

    if (_hdrEnabled && _hdr && _hdr->IsEnabled()) {
        glBindFramebuffer(GL_FRAMEBUFFER, _hdr->GetHdrFBO());
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.scene.sun->RenderGlow(ctx.view.projection, ctx.view.view, ctx.view.front - ctx.view.right, ctx.view.aspect,
                         glm::distance(ctx.view.headPosition, ctx.scene.sun->GetPosition()), ringCameraInfo, _starTuning.temperatureKelvin);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFramebuffer);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        ctx.scene.sun->RenderGlow(ctx.view.projection, ctx.view.view, ctx.view.front - ctx.view.right, ctx.view.aspect,
                         glm::distance(ctx.view.headPosition, ctx.scene.sun->GetPosition()), ringCameraInfo, _starTuning.temperatureKelvin);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, ctx.defaultFramebuffer);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    if (_hdrEnabled && _hdr && _hdr->IsEnabled()) {
        glBlendFunc(GL_ONE, GL_ONE);
        _hdr->Render(_starTuning.exposure, _starTuning.gamma);
    }

    glBlendFunc(GL_ONE, GL_ONE);
    float intensity = glm::min(ctx.scene.sun->GetCurrentGlowSize() * ctx.scene.sun->GetVisibility(), 1.0f);
    _lensFlare->Render(ctx.view.projection, ctx.view.view, ctx.scene.sun->GetPosition(), glm::vec3(1.0), ctx.view.aspect, 0.1, intensity, ringCameraInfo);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderPlanetProxyMarkers(const FrameContext& ctx) const {
    // Web staged loading: a label at the orbital position of every system not READY yet.
    if (!ctx.scene.stagedSystems || ctx.scene.stagedSystems->empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", ctx.view.projection);
    _mainTextShader->SetMat4("view", ctx.view.view);
    _mainTextShader->SetBool("is3D", true);

    for (const auto& manifest : *ctx.scene.stagedSystems) {
        if (manifest.state == PlanetSystemManifest::State::READY) continue;

        std::wstring label(manifest.name.begin(), manifest.name.end());
        if (manifest.state == PlanetSystemManifest::State::NOT_LOADED) {
            label += L" (approach to load)";
        } else if (manifest.state == PlanetSystemManifest::State::DOWNLOADING) {
            int done = manifest.totalDownloads - manifest.pendingDownloads;
            int pct = (manifest.totalDownloads > 0) ? (done * 100 / manifest.totalDownloads) : 0;
            label += L" [downloading " + std::to_wstring(pct) + L"%]";
        }

        std::deque<wchar_t> chars(label.begin(), label.end());
        _mainTextShader->SetVec3("particleCenterWorldSpace", manifest.proxyPosition);
        _textRenderer->Render(*_mainTextShader, chars, 0.0, 0.0, 0.075, glm::vec3(0.5f, 0.7f, 1.0f));
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderPlanetSatelliteStarDistances(const FrameContext& ctx) const {
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (_overlayToggles.planetStarDistances)
        RenderSpaceObjectDistance(ctx, ctx.scene.sun);

    for(const auto& renderableComponentPS : (*ctx.scene.components)) {
        if (_overlayToggles.planetStarDistances) {
            RenderSpaceObjectDistance(ctx, renderableComponentPS.planet.get());
        }

        if (_overlayToggles.satelliteDistances) {
            for(const auto& satellite : renderableComponentPS.satellites) {
                RenderSpaceObjectDistance(ctx, satellite.get());
            }
        }
    }

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderSpaceObjectDistance(const FrameContext& ctx, const SpaceObject* spaceObject) const {
    // Reuse pre-allocated container to eliminate per-frame heap allocation
    _distanceInfoCache.clear();
    _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetEngName().begin(), spaceObject->GetEngName().end());

    if (!spaceObject->GetOtherLangName().empty()) {
        _distanceInfoCache.push_back(L'[');
        _distanceInfoCache.insert(_distanceInfoCache.end(), spaceObject->GetOtherLangName().begin(), spaceObject->GetOtherLangName().end());
        _distanceInfoCache.push_back(L']');
        _distanceInfoCache.push_back(L' ');
    }

    wstring distance(to_wstring(static_cast<uint16_t>(glm::distance(ctx.view.headPosition, spaceObject->GetPosition()))));
    _distanceInfoCache.insert(_distanceInfoCache.end(), make_move_iterator(distance.begin()), make_move_iterator(distance.end()));

    _mainTextShader->Use();
    _mainTextShader->SetVec3("particleCenterWorldSpace", spaceObject->GetPosition());
    _mainTextShader->SetBool("is3D", true);
    _textRenderer->Render(*_mainTextShader, _distanceInfoCache, 0.0, 0.0, 0.075, glm::vec3(0.98431, 0.80784, 0.69412));
}

void Renderer::RenderHints(const FrameContext& ctx) const {
    static const string gpuHintString = string(reinterpret_cast<const char *>(glGetString(GL_RENDERER)));
    static constexpr glm::vec3 textColor = glm::vec3(0.98431, 0.80784, 0.69412);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _hudProjection);
    _mainTextShader->SetBool("is3D", false);

    // Reuse pre-allocated containers to eliminate per-frame heap allocations
    _fpsHintCache.clear();
    _fpsHintCache.emplace_back(L"FPS: ");
    _fpsHintCache.emplace_back(to_wstring(ctx.hud->fps));

    _gpuHintCache.clear();
    _gpuHintCache.emplace_back(wstring(gpuHintString.begin(), gpuHintString.end()));

#ifndef __EMSCRIPTEN__ // web: volume and track live in the settings panel (web/src/music.ts)
    _soundVolumeHintCache.clear();
    stringstream soundVolumeStream;
    soundVolumeStream << fixed << setprecision(0) << ctx.hud->musicVolumePercent;
    string soundVolume = soundVolumeStream.str();
    _soundVolumeHintCache.emplace_back(wstring(L"Sound volume(PgUp/PgDown): ").append(soundVolume.begin(), soundVolume.end()).append(L" %"));

    _tmpStringCache.clear();
    _tmpStringCache.emplace_back(wstring(ctx.hud->musicTrack.begin(), ctx.hud->musicTrack.end()));
#endif

    deque<wstring> timeRunHint;
    timeRunHint.emplace_back(L"Time (P:pause, +/-:scale, .:step): ");
    std::wstringstream wss;
    wss << (ctx.sim.timePaused ? L"paused" : L"run") << L" x" << static_cast<int>(ctx.sim.timeScale);
    int year = 0;
    int month = 0;
    int day = 0;
    Ephemeris::YmdFromJulianDate(OrbitLayout::GetJulianDate(), year, month, day);
    wss << L" | " << year << L'-' << std::setfill(L'0') << std::setw(2) << month
        << L'-' << std::setw(2) << day;
    timeRunHint.emplace_back(wss.str());

    // Sky event driven by the ephemeris, not a baked animation — see SkyEvents.h.
    deque<wstring> conjunctionHint;
    if (const SkyEvents::SkyEvent* next = ctx.scene.nextSkyEvent; next && next->valid) {
        const std::string line = SkyEvents::FormatEvent(*next);
        conjunctionHint.emplace_back(line.begin(), line.end()); // ASCII only, see SkyEvents
    }

    deque<wstring> planetStarHint;
    planetStarHint.emplace_back(L"Planet/Star distances(Z): ");
    planetStarHint.emplace_back((_overlayToggles.planetStarDistances) ? L"On" : L"Off");

    deque<wstring> satelliteHint;
    satelliteHint.emplace_back(L"Satellite distances(X): ");
    satelliteHint.emplace_back((_overlayToggles.satelliteDistances) ? L"On" : L"Off");

#ifdef __EMSCRIPTEN__
    // Indicate low-res start + high-res streaming (visual in streaming-progress + on-screen when active)
    deque<wstring> textureHint;
    textureHint.emplace_back(L"Web: low-res start; high-res streams when close (see HUD)");
#endif

    deque<wstring> cameraSpeedHint;
    cameraSpeedHint.emplace_back(L"Camera speed(1/2): ");
    cameraSpeedHint.emplace_back(to_wstring(ctx.hud->cameraSpeed));

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
    starExposureHint.emplace_back(to_wstring(_starTuning.exposure));

    deque<wstring> starGammaHint;
    starGammaHint.emplace_back(L"Star Gamma(5/6): ");
    starGammaHint.emplace_back(to_wstring(_starTuning.gamma));

    deque<wstring> starTemperatureHint;
    stringstream  starTemperatureStream;
    starTemperatureStream << fixed << setprecision(0) << _starTuning.temperatureKelvin;
    string starTemperatureStr = starTemperatureStream.str();
    starTemperatureHint.emplace_back(wstring(L"Star Temperature(7/8): ").append(make_move_iterator(starTemperatureStr.begin()),
                                                                                make_move_iterator(starTemperatureStr.end())));
    deque<wstring> vertSyncHint;
    vertSyncHint.emplace_back(L"Vert Sync(F1): ");
    vertSyncHint.emplace_back((ctx.hud->vsync) ? L"On" : L"Off");

    deque<wstring> textHints;
    textHints.emplace_back(L"Text hints(TAB)");

    deque<wstring> magneticHint;
    magneticHint.emplace_back(L"Magnetic fields(M): ");
    magneticHint.emplace_back(_magneticFieldsEnabled ? L"On" : L"Off");

#ifndef __EMSCRIPTEN__
    _textRenderer->ReverseRender(*_mainTextShader, _tmpStringCache, 0.99 * ctx.display.x, 0.95 * ctx.display.y, 0.35, textColor);
#endif
    if (!conjunctionHint.empty()) {
        _textRenderer->ReverseRender(*_mainTextShader, conjunctionHint, 0.99 * ctx.display.x, 0.925 * ctx.display.y, 0.35, textColor);
    }
    _textRenderer->Render(*_mainTextShader, _fpsHintCache, 0.01 * ctx.display.x, 0.95 * ctx.display.y, 0.35, ctx.hud->fpsColor);
    _textRenderer->Render(*_mainTextShader, _gpuHintCache, 0.01 * ctx.display.x, 0.925 * ctx.display.y, 0.35, textColor);
#ifndef __EMSCRIPTEN__
    _textRenderer->Render(*_mainTextShader, _soundVolumeHintCache, 0.01 * ctx.display.x, 0.9 * ctx.display.y, 0.35, textColor);
#endif
    _textRenderer->Render(*_mainTextShader, timeRunHint, 0.01 * ctx.display.x, 0.875 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, planetStarHint, 0.01 * ctx.display.x, 0.85 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, satelliteHint, 0.01 * ctx.display.x, 0.825 * ctx.display.y, 0.35, textColor);
#ifdef __EMSCRIPTEN__
    _textRenderer->Render(*_mainTextShader, textureHint, 0.01 * ctx.display.x, 0.80 * ctx.display.y, 0.30, glm::vec3(0.6f, 0.8f, 1.0f));
#endif
    _textRenderer->Render(*_mainTextShader, cameraSpeedHint, 0.01 * ctx.display.x, 0.8 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, smoothCameraHint, 0.01 * ctx.display.x, 0.775 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, smoothZoomHint, 0.01 * ctx.display.x, 0.75 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, movementHint, 0.01 * ctx.display.x, 0.725 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, speedBostHint, 0.01 * ctx.display.x, 0.7 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starExposureHint, 0.01 * ctx.display.x, 0.675 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starGammaHint, 0.01 * ctx.display.x, 0.65 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, starTemperatureHint, 0.01 * ctx.display.x, 0.625 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, vertSyncHint, 0.01 * ctx.display.x, 0.6 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, textHints, 0.01 * ctx.display.x, 0.575 * ctx.display.y, 0.35, textColor);
    _textRenderer->Render(*_mainTextShader, magneticHint, 0.01 * ctx.display.x, 0.55 * ctx.display.y, 0.35, textColor);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::RenderTextureLoadingProgress(const FrameContext& ctx) const {
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

    static constexpr glm::vec3 textColor = glm::vec3(0.3f, 0.8f, 1.0f);

    _mainTextShader->Use();
    _mainTextShader->SetMat4("projection", _hudProjection);
    _mainTextShader->SetBool("is3D", false);

    // Hint string with progress counters, e.g. "Mid-res upgrade (2/5)". Rebuilt only when a
    // counter changes, but drawn every frame: the frame is cleared, so a hint drawn one frame
    // in ten (as the old throttle did) flickers.
    const std::array<int, 4> counters = {completed, total, active, tierCode};
    if (_loadingHintCache.empty() || counters != _loadingHintCounters) {
        _loadingHintCounters = counters;
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
        _loadingHintCache.clear();
        _loadingHintCache.emplace_back(std::move(hint));
    }
    _textRenderer->Render(*_mainTextShader, _loadingHintCache, 0.5f * ctx.display.x - 150, 0.1f * ctx.display.y, 0.25, textColor);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::ConfigureMainShaders(const FrameContext& ctx) {
    // Matrices, eye position, Sun position, far plane, zCoef and time are in the FrameUBO
    // (BeginView). What is left here is per-view state of programs that do not read it, and
    // per-frame constants; Shader drops a Set* that repeats the value already there.

    // Skybox uses the active view's rotation; projection matches the eye frustum in XR.
    const glm::mat4 skyBoxProjection = ctx.xr ? ctx.view.projection : glm::perspective(glm::radians(45.0f), ctx.view.aspect, ctx.view.nearPlane, ctx.view.farPlane);

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
    _mainAtmosphereShader->SetFloat("bias", 0.001);
    _mainAtmosphereShader->SetFloat("uSurfaceDim", atmosphereDim);

    if (_pbrAtmosphereShader) {
        _pbrAtmosphereShader->Use();
        _pbrAtmosphereShader->SetFloat("uSurfaceDim", atmosphereDim);
    }

    _mainCloudsShader->Use();
    _mainCloudsShader->SetFloat("bias", 0.001);
    _mainCloudsShader->SetFloat("uSurfaceDim", surfaceDim);

    _mainRingShader->Use();
    _mainRingShader->SetVec3("starGlowTint", sun.GetGlowTintMult());
    _mainRingShader->SetFloat("bias", 0.001);
    _mainRingShader->SetFloat("uSurfaceDim", ringDim);

    // Every lit program samples the shadow map from the same unit (TU_SHADOW_MAP).
    glBindTextureUnit(Unit(TextureUnit::ShadowMap), _shadowMapFBO->GetShadowMap());
}

Renderer::EclipseUmbra Renderer::ComputeEclipseUmbra(const FrameContext& ctx, const RenderableSceneComponent& renderableComponent) const {
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
    const float starRadius = lightDistance * (kSunRadiusKm / kAuKm);

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
    Shader*& program = _planetVariants[variant];
    if (!program) {
        program = _shaders.GetIfReady(PlanetSpec(variant));
    }
    if (program) {
        return *program;
    }
    // Not built yet (or failed): drop the maps, keep the sphere test if that one is there.
    if (Shader* withoutMaps = _planetVariants[variant & CatalogMaterial::kSphereIntersect]) {
        return *withoutMaps;
    }
    return *_mainPlanetShader;
}

void Renderer::ConfigurePlanetProgram(const FrameContext& ctx, const Shader& program,
                                      const RenderableSceneComponent& component, const EclipseUmbra& umbra) const {
    // Every variant gets the full set; the value cache turns the repeats into lookups.
    program.Use();
    program.SetVec3("starGlowTint", ctx.scene.sun->GetGlowTintMult());
    program.SetFloat("bias", 0.0005f);
    program.SetFloat("uSurfaceDim", _magneticFieldsEnabled ? 0.28f : 1.0f);
    program.SetMat4("lightSpaceMatrix", component.lightSpaceMatrix);

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

void Renderer::DrawLitBody(const FrameContext& ctx, SpaceObject& body, const RenderableSceneComponent& component,
                           const EclipseUmbra& umbra) {
    const Shader& program = PlanetProgram(body.ShaderVariant());
    ConfigurePlanetProgram(ctx, program, component, umbra);
    body.SetShader(program);
    body.Render();
}
