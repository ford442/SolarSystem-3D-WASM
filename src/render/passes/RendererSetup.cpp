// Renderer's lifetime and GPU resources: building every program and target the passes use,
// following resizes and quality changes, and releasing or restoring them with the context
// (docs/ARCHITECTURE.md §7.8). The passes themselves are the other files in src/render/passes/.
#include "Renderer.h"
#include "PassCommon.h"
#include "QualitySettings.h"
#include "ResourceManifest.h"
#include "SimState.h"
#include "astro/MissionCatalog.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/CatalogMaterial.h"
#include "Solar_System/ObserveSky.h"
#include "Solar_System/SkyBox.h"
#include "Solar_System/TexturePaths.h"
#include "core/Log.h"
#include "gfx/GlHandle.h"
#include "gfx/HDR.h"
#include "gfx/Shader.h"
#include "gfx/ShaderLibrary.h"
#include "gfx/ShadowMapFBO.h"
#include "gfx/SpaceWeatherUniforms.h" // IWYU pragma: keep (~Renderer destroys the unique_ptr)
#include "gfx/SurfacePatchMesh.h"
#include "gfx/TextureImage2D.h"
#include "gfx/rhi/GlCommandList.h"
#include "render/LensFlare.h"
#include "render/MagneticFieldBloom.h"
#include "render/MagneticFieldLineRenderer.h"
#include "render/MagneticFieldTraceJobs.h" // IWYU pragma: keep (~Renderer destroys the unique_ptr)
#include "render/MissionPathRenderer.h"    // IWYU pragma: keep (~Renderer destroys the unique_ptrs)
#include "render/OrbitPathRenderer.h"
#include "render/SolarWindRenderer.h" // IWYU pragma: keep (~Renderer destroys the unique_ptr)
#include "render/TextRenderer.h"
#include "render/XrPointerRenderer.h"
#include "render/terrain/TerrainRenderer.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <vector>

using RenderPasses::PlanetSpec;
using RenderPasses::Program;

Renderer::Renderer() : _commands(std::make_unique<rhi::GlCommandList>()) {}
Renderer::~Renderer() = default;

namespace {

// Every planetLighting variant some catalog row can draw with (CatalogMaterial::PossibleVariants).
std::vector<unsigned> CatalogPlanetVariants() {
    std::vector<unsigned> variants{0};
    for (const BodyCatalog::Entry& entry : BodyCatalog::Entries()) {
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
    const auto qualitySettings =
        GetQualitySettings(sim.shadowQuality == 0 ? sim.qualityPreset : sim.shadowQuality - 1, sim.isMobileWeb);
    _shadowMapFBO = std::make_unique<ShadowMapFBO>(qualitySettings.shadowResolution, qualitySettings.shadowResolution);

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
    const ShaderSpec screenFadeSpec = Program("screenFade.vs", "screenFade.fs");
    const std::vector<unsigned> planetVariants = CatalogPlanetVariants();
    for (const ShaderSpec* spec :
         {&hdrSpec, &textSpec, &shadowSpec, &skyBoxSpec, &starSpec, &coronaSpec, &atmosphereSpec, &pbrAtmosphereSpec,
          &cloudsSpec, &ringSpec, &lensFlareSpec, &starGlowSpec, &coronaVolumeSpec, &screenFadeSpec}) {
        _shaders.Prefetch(*spec);
    }
    for (const unsigned variant : planetVariants) {
        _shaders.Prefetch(PlanetSpec(variant));
    }
    _hdrEnabled = qualitySettings.enableHdr;
    _hdrShader = &_shaders.Get(hdrSpec);
    _hdr = std::make_unique<HDR>(*_hdrShader, display.x, display.y, _hdrEnabled);
    _hudProjection = glm::ortho(0.0f, static_cast<float>(display.x), 0.0f, static_cast<float>(display.y));
    // HDR turns itself off when the GPU cannot give it a complete float FBO; follow it,
    // otherwise the composite path would keep sampling an empty buffer.
    _hdrEnabled = _hdr->IsEnabled();
    LogQualityTier(qualitySettings, _hdrEnabled, sim.shadowQuality, sim.contextSamples);

    _mainTextShader = &_shaders.Get(textSpec);
    _textRenderer = std::make_unique<TextRenderer>(ft, "resource/fonts/Arial.ttf");
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
        LOG_WARN("Atmosphere", "LUT atmosphere shader unavailable; using the O'Neil shell");
    }
    _mainCloudsShader = &_shaders.Get(cloudsSpec);
    _mainRingShader = &_shaders.Get(ringSpec);
    _lensFlareShader = &_shaders.Get(lensFlareSpec);
    _starGlowShader = &_shaders.Get(starGlowSpec);
    _coronaVolumeShader = _shaders.TryGet(coronaVolumeSpec);
    if (!_coronaVolumeShader) {
        LOG_WARN("Corona", "volumetric corona shader unavailable; using the flat corona");
    }
    _screenFadeShader = _shaders.TryGet(screenFadeSpec);
    _screenFadeVao = GlVertexArray::Create();
    // The base variant must build; the others fall back to it in PlanetProgram if their own
    // compile failed.
    _planetVariants.fill(nullptr);
    _mainPlanetShader = &_shaders.Get(PlanetSpec(0));
    for (const unsigned variant : planetVariants) {
        _planetVariants[variant] = _shaders.TryGet(PlanetSpec(variant));
    }

    // After every program is built: a missing skybox face aborts start-up (native has no
    // checkerboard fallback), and the native CI smoke run should still have compiled the
    // shaders by then.
    _skyBox = std::make_unique<SkyBox>(GetSkyBoxFaces());
    _lensFlare = std::make_unique<LensFlare>(
        *_lensFlareShader, TextureImage2D(TexturePaths::Resolve("resource/textures_low/flares_bright_Low.dds")),
        FlaresInfo{4,
                   {FlareSprite{false, 1.0, 7.0, 0}, FlareSprite{false, 1.35, 0.3, 1}, FlareSprite{false, 1.5, 0.4, 4},
                    FlareSprite{false, 1.7, 0.6, 5}, FlareSprite{false, 1.9, 1.2, 6}, FlareSprite{false, 2.1, 0.4, 2},
                    FlareSprite{false, 2.25, 0.2, 3}, FlareSprite{false, 2.75, 2.0, 7}}});
    try {
        _observeSky = std::make_unique<ObserveSky>();
    } catch (const std::exception& error) {
        LOG_WARN("Observe", "sky shaders unavailable (%s); Observe mode will draw an empty sky", error.what());
        _observeSky.reset();
    }
    _orbitPathRenderer = std::make_unique<OrbitPathRenderer>();
    _surfacePatch = std::make_unique<SurfacePatchMesh>();
    _surfacePatchKey = SurfacePatchKey{};
    // Its programs compile on first use, once a terrain body is within reach (DrawLitBody).
    _terrainRenderer = std::make_unique<TerrainRenderer>();
    _terrainVariants.fill(nullptr);
    _terrainShadowShader = nullptr;
    _xrPointerRenderer = std::make_unique<XrPointerRenderer>();
    _magneticFieldRenderer = std::make_unique<MagneticFieldLineRenderer>();
    {
        const auto fieldQuality = GetQualitySettings(sim.qualityPreset, sim.isMobileWeb);
        // Chain 0 of the bloom pool (BloomChain): the graph aliases both blooms onto it.
        _bloomEnabled = fieldQuality.enableMagneticBloom;
        _bloomChains.clear();
        _bloomChains.push_back(std::make_unique<MagneticFieldBloom>(display.x, display.y, _bloomEnabled));
        ApplyEffectQuality(fieldQuality);
    }
}

void Renderer::Resize(uint16_t width, uint16_t height) {
    _hudProjection = glm::ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height));
    if (_hdr) {
        _hdr->Resize(width, height);
    }
    for (const std::unique_ptr<MagneticFieldBloom>& chain : _bloomChains) {
        chain->Resize(width, height);
    }
}

void Renderer::Release() {
    // Users before the shaders they hold by reference (LensFlare, HDR).
    _missionPathMeshes.clear();
    _bloomChains.clear();
    _magneticFieldRenderer.reset();
    _magneticFieldsBuilt = false;
    _xrPointerRenderer.reset();
    _orbitPathRenderer.reset();
    _surfacePatch.reset();
    _surfacePatchKey = SurfacePatchKey{};
    _terrainRenderer.reset();
    _observeSky.reset();
    _lensFlare.reset();
    _skyBox.reset();
    _hdr.reset();
    _screenFadeVao.reset();
    _textRenderer.reset();
    _shadowMapFBO.reset();
    for (Shader** shader :
         {&_shadowMapShader,         &_mainSkyBoxShader,   &_mainTextShader,       &_mainStarShader,
          &_mainCoronaStarShader,    &_mainPlanetShader,   &_mainAtmosphereShader, &_mainCloudsShader,
          &_mainRingShader,          &_hdrShader,          &_lensFlareShader,      &_starGlowShader,
          &_pbrAtmosphereShader,     &_coronaVolumeShader, &_screenFadeShader,     &_nearAtmosphereShader,
          &_nearPbrAtmosphereShader, &_nearCloudsShader,   &_terrainShadowShader,  &_solarWindUpdateShader,
          &_solarWindDrawShader,     &_spriteStarShader}) {
        *shader = nullptr;
    }
    _auroraShaders.fill(nullptr);
    _starShaders.fill(nullptr);
    _solarWind.reset();
    _spaceWeatherUbo.reset();
    _planetVariants.fill(nullptr);
    _terrainVariants.fill(nullptr);
    _shaders.Clear();
}

void Renderer::RestoreGpu(const MissionCatalog::Catalog& missions, int qualityPreset) {
    _screenFadeVao = GlVertexArray::Create();
    _surfacePatchKey = SurfacePatchKey{}; // the patch re-streams on its next draw
    _configuredView = nullptr;
    _uploadedViews = 0;
    if (_hdr) {
        _hdrEnabled = _hdr->IsEnabled();
    }
    if (!_missionPathMeshes.empty()) {
        RebuildMissionPaths(missions, qualityPreset);
    }
    RebuildMagneticFields(qualityPreset); // re-traced in jobs; nothing drawn until they land
}

void Renderer::ApplyRenderResources(const SimState& sim, uint16_t shadowResolution, bool enableHdr,
                                    glm::ivec2 display) {
    _hdrEnabled = enableHdr;

    if (_shadowMapFBO && sim.shadowQuality > 0) {
        _shadowMapFBO->Resize(shadowResolution, shadowResolution);
    }

    if (_hdr) {
        _hdr->SetEnabled(enableHdr, display.x, display.y);
        // SetEnabled can refuse (no renderable float buffers / incomplete FBO).
        _hdrEnabled = _hdr->IsEnabled();
    }
    const auto bloomSettings = GetQualitySettings(sim.qualityPreset, sim.isMobileWeb);
    _bloomEnabled = bloomSettings.enableMagneticBloom;
    for (const std::unique_ptr<MagneticFieldBloom>& chain : _bloomChains) {
        chain->SetEnabled(_bloomEnabled, display.x, display.y);
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
        _observeSky->SetCoreConstellationsOnly(settings.observeCoreConstellationsOnly);
    }
}

void Renderer::PrefetchCatalogVariants() {
    for (const unsigned variant : CatalogPlanetVariants()) {
        _shaders.Prefetch(PlanetSpec(variant));
    }
}
