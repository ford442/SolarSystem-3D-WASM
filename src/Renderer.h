#ifndef SOLARSYSTEM_RENDERER_H
#define SOLARSYSTEM_RENDERER_H
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "SystemModules.h"
#include "gfx/FrameUniforms.h"
#include "gfx/GlHandle.h"
#include "astro/MissionCatalog.h"
#include "gfx/ShaderLibrary.h"
#include "gfx/rhi/CommandList.h"
#include "render/graph/FrameGraph.h"
#include "render/graph/FramePlan.h"
#include "Solar_System/CatalogMaterial.h"
#include "Solar_System/ObserveSky.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SpaceObject;
class GpuTimer;
struct QualityTierSettings;
namespace FrameProfile {
class CpuStages;
class PassTimings;
} // namespace FrameProfile
namespace rhi {
class GlCommandList;
}
// GPU helpers held by unique_ptr: complete in the Renderer TUs only, which keeps their
// headers out of everything that includes Application.h.
class HDR;
class LensFlare;
class MagneticFieldBloom;
class MagneticFieldLineRenderer;
struct MagneticFieldTraceJobs;
class MissionPathRenderer;
class OrbitPathRenderer;
class ShadowMapFBO;
class SkyBox;
class SolarWindRenderer;
class SpaceWeatherUniformBuffer;
struct SpaceWeatherUniformData;
class SurfacePatchMesh;
class TerrainRenderer;
class TextRenderer;
class XrPointerRenderer;

/**
 * Shadow/color/overlay/HDR draw passes, the GPU resources they own (shaders, shadow FBO,
 * HDR, skybox, lens flare, text renderer, orbit/mission/magnetic-field overlay meshes) and
 * the render-only toggles (orbit lines, magnetic fields, hint overlay, star tuning).
 *
 * The frame is data (docs/ARCHITECTURE.md §7.5): RenderFrame declares it with
 * render/graph/FramePlan on a FrameGraph::Graph, which culls, orders and runs the passes;
 * Renderer is the PassSink behind each one, recording through an rhi::CommandList. The pass
 * bodies live in src/render/passes/, one file per family.
 *
 * Every pass reads the frame's FrameContext: the camera view, a snapshot of the scene and
 * the sim/quality state. That is all a pass may read — the renderer has no reference back
 * to Application, and nothing in the scene moves during a pass (Application::UpdateScene
 * placed it before the first one). See FrameContext.h.
 */
class Renderer : private FramePlan::PassSink {
public:
    Renderer();
    ~Renderer() override; // out of line: the unique_ptr members above are incomplete here
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /** Hand-tuned star look (keys 3–8): HDR exposure/gamma and the glow's temperature. */
    struct StarTuning {
        float exposure = 8.0f;
        float gamma = 0.4545454f;
        float temperatureKelvin = 5778.0f;
    };
    /** Text overlays toggled from the keyboard (TAB, Z, X). */
    struct OverlayToggles {
        bool hints = true;
        bool planetStarDistances = true;
        bool satelliteDistances = true;
        /** Web staged loading: "Jupiter (approach to load)" at systems not loaded yet. */
        bool stagingLabels = true;
    };

    /**
     * Build shaders + GPU resources sized to `display` and the current quality settings.
     * Takes ownership of `ft` (the text renderer loads its face, then FreeType is released).
     */
    void Init(const SimState& sim, FT_Library ft, glm::ivec2 display);
    /** Follow a framebuffer resize: HUD projection and the screen-sized render targets. */
    void Resize(uint16_t width, uint16_t height);
    /** Delete every GPU resource Init() built. Needs the GL context still current. */
    void Release();
    /**
     * After GpuResources::RecreateAll on a restored context: what the registry cannot rebuild
     * on its own — the empty screen-fade VAO, the mission paths and field lines (uploaded once
     * from data the renderer re-derives), and the HDR flag if the float target was refused.
     */
    void RestoreGpu(const MissionCatalog::Catalog& missions, int qualityPreset);
    /** Atmosphere/corona effect levels from a quality tier (preset changes call this too). */
    void ApplyEffectQuality(const QualityTierSettings& settings);
    /**
     * Quality/shadow changes: resize the shadow map (unless shadows are off) and switch HDR
     * and the field-line bloom on or off. HDR can refuse (no float FBO); HdrEnabled() follows.
     */
    void ApplyRenderResources(const SimState& sim, uint16_t shadowResolution, bool enableHdr, glm::ivec2 display);
    void ResizeShadowMap(uint16_t resolution);
    bool HdrEnabled() const { return _hdrEnabled; }

    // One frame. Application::UpdateScene has placed everything; the passes only read it.
    /** Reset this frame's counters (Stats()). */
    void BeginFrame();
    /**
     * Once a frame, after the scene is placed and before RenderFrame: the photosphere program
     * for the tier and its spot uniforms, the solar-wind groups and the MagnetosphereUBO's
     * contents (docs/ARCHITECTURE.md §13), finished field-line traces, the bloom chain's size.
     */
    void PrepareFrame(const FrameContext& ctx);
    /**
     * The render half of this frame's FramePlan::Features: which overlays, effects and HUD
     * lines exist, the components in draw order (SceneSnapshot::drawOrder) and the targets'
     * sizes. Application adds what only it knows (the loading line).
     */
    void DescribeFrame(const FrameContext& ctx, int viewCount, FramePlan::Features& features);
    /**
     * Declare the frame on the graph, compile it and run it: `views` holds one FrameContext
     * per view (one mono, two XR eyes; none for an XR frame whose eyes have not arrived).
     */
    void RenderFrame(const std::vector<FrameContext>& views, const FramePlan::Features& features);
    /** The upload pump Application runs as the frame's `uploads` pass. */
    void SetUploadPump(std::function<void()> pump) { _uploadPump = std::move(pump); }
    /** Where per-pass timings go (any may be null): see rhi::GlCommandList::SetProfiling. */
    void SetProfiling(GpuTimer* gpu, FrameProfile::PassTimings* passes, FrameProfile::CpuStages* stages);
    /** The last frame's compiled graph. */
    const FrameGraph::Graph& Graph() const { return _graph; }
    /**
     * ?graph=1 on the web, SOLARSYSTEM_GRAPH=1 natively: log the compiled graph as JSON and DOT
     * whenever its passes change ([FrameGraph] lines).
     */
    void SetGraphDump(bool enabled) { _graphDump = enabled; }
    /** Whether atmospheres with LUTs should take the physical path this frame. */
    bool PhysicalAtmosphereAvailable() const { return _pbrAtmosphereSteps > 0 && _pbrAtmosphereShader != nullptr; }
    /** This frame's pass counts (reset by BeginFrame). */
    const FramePlan::Stats& Stats() const { return _stats; }
    void CountSceneUpdate() { ++_stats.sceneUpdates; }
    void CountCulledComponents(int count) { _stats.culledComponents = count; }
    /**
     * Start compiling every planetLighting variant the active catalog can draw with (after
     * StarSystemCatalog::Activate): a new system's PROCEDURAL_SURFACE programs build while the
     * jump is black instead of on its first frame.
     */
    void PrefetchCatalogVariants();

    /**
     * Re-upload every mission path relative to the current render origin (scale or quality
     * change). Draws offset them by where that origin is now, so the per-frame origin move
     * only re-uploads once it has gone kMissionPathRebaseDistance (FollowMissionPathOrigin).
     */
    void RebuildMissionPaths(const MissionCatalog::Catalog& catalog, int qualityPreset);
    /** After the render origin moved: rebuild the paths if it is now far from their upload origin. */
    void FollowMissionPathOrigin(const MissionCatalog::Catalog& catalog, int qualityPreset);
    void SetOrbitLinesEnabled(bool enabled) { _orbitLinesEnabled = enabled; }
    bool GetOrbitLinesEnabled() const { return _orbitLinesEnabled; }
    void SetMagneticFieldsEnabled(bool enabled, int qualityPreset);
    bool GetMagneticFieldsEnabled() const { return _magneticFieldsEnabled; }
    /** After a preset change: re-trace the field lines at the new density if they are shown. */
    void RebuildMagneticFields(int qualityPreset);

    StarTuning& GetStarTuning() { return _starTuning; }
    OverlayToggles& GetOverlayToggles() { return _overlayToggles; }
    const OverlayToggles& GetOverlayToggles() const { return _overlayToggles; }
    /** Null before Init() and after Release(). */
    XrPointerRenderer* GetXrPointers() { return _xrPointerRenderer.get(); }
    /** Brightest catalog stars Observe draws right now (0 without its shaders). */
    int ObserveStarsDrawn() const;
    int ObserveConstellationSegments() const;
    /** Delete the GPU copies of terrain tiles the cache evicted (Terrain::TileCache::TakeEvicted). */
    void ForgetTerrainTiles(const std::vector<uint64_t>& uids);
    /** Terrain tiles with a GPU copy, for the [Terrain] log line. */
    std::size_t TerrainTextureCount() const;

private:
    // --- FramePlan::PassSink: one method per pass (src/render/passes/) ----------------------
    void BeginView(rhi::CommandList& cmd, int view) override;
    void SolarWindUpdate(rhi::CommandList& cmd) override;
    void ObserveSkyPass(rhi::CommandList& cmd, int view) override;
    void SkyBoxPass(rhi::CommandList& cmd, int view) override;
    void OrbitPaths(rhi::CommandList& cmd, int view) override;
    void MissionPaths(rhi::CommandList& cmd, int view) override;
    void XrPointers(rhi::CommandList& cmd, int view) override;
    void FlatCorona(rhi::CommandList& cmd, int view) override;
    void ShadowMap(rhi::CommandList& cmd, std::size_t component) override;
    void ClearShadowMap(rhi::CommandList& cmd) override;
    void SceneComponent(rhi::CommandList& cmd, int view, std::size_t component) override;
    void CoronaVolume(rhi::CommandList& cmd, int view) override;
    void Asteroids(rhi::CommandList& cmd, int view) override;
    void SolarWind(rhi::CommandList& cmd, int view) override;
    void MagneticFields(rhi::CommandList& cmd, int view, bool restoreState) override;
    void MagneticBloom(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) override;
    void MagneticBloomComposite(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) override;
    void AuroraBloom(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) override;
    void AuroraBloomComposite(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) override;
    void ProxyLabels(rhi::CommandList& cmd) override;
    void StarGlow(rhi::CommandList& cmd) override;
    void HdrComposite(rhi::CommandList& cmd) override;
    void LensFlarePass(rhi::CommandList& cmd) override;
    void ObserveHud(rhi::CommandList& cmd) override;
    void Distances(rhi::CommandList& cmd) override;
    void Hints(rhi::CommandList& cmd) override;
    void ScreenFade(rhi::CommandList& cmd) override;
    void Uploads(rhi::CommandList& cmd) override;
    void LoadingProgress(rhi::CommandList& cmd) override;

    /** View `view` of the frame RenderFrame is running. */
    const FrameContext& View(int view) const { return (*_views)[static_cast<std::size_t>(view)]; }
    /** Component `drawIndex` of this frame's draw order. */
    const RenderableSceneComponent& DrawnComponent(const FrameContext& ctx, std::size_t drawIndex) const;
    /** The bloom chain behind physical slot `slot` (FrameGraph aliasing), made on first use. */
    MagneticFieldBloom& BloomChain(std::size_t slot);
    /** Whether the frame's bloom chain is on at this display size (resizes it first). */
    bool BloomChainReady(glm::ivec2 display);
    /** Log the compiled graph when its passes changed since the last dump (SetGraphDump). */
    void DumpGraphIfChanged();

    void ConfigureMainShaders(const FrameContext& ctx);
    void ProcessStarRendering(rhi::CommandList& cmd, const FrameContext& ctx);
    /** Flat corona billboard (Low, or coronaSlices == 0); drawn before the planets. */
    void RenderStarCorona(rhi::CommandList& cmd, const FrameContext& ctx) const;
    /** Instanced-slice corona (coronaSlices > 0); drawn after the planets so they occlude it. */
    void RenderStarCoronaVolume(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderPlanetProxyMarkers(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderPlanetSatelliteStarDistances(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderHints(rhi::CommandList& cmd, const FrameContext& ctx) const;
    /** Texture-upgrade progress line; also reports streaming progress to the page on web. */
    void RenderTextureLoadingProgress(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderOrbitPaths(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderAsteroidField(const FrameContext& ctx) const;
    void RenderMissionPaths(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderXrPointers(rhi::CommandList& cmd, const FrameContext& ctx) const;
    /** The field-line ribbons of the Sun and every planet with a field, at a width and opacity. */
    void DrawMagneticFieldRibbons(const FrameContext& ctx, float ribbonWidthScale, float opacityScale);
    /** Observe mode: the ground-level sky pass (replaces every scene pass). */
    void RenderObserveSky(const FrameContext& ctx);
    /** Observe mode: compass, body labels and the readout line, drawn over the sky. */
    void RenderObserveHud(const FrameContext& ctx);
    /** Compass points and body/star names projected onto `_observeFrame`; `textScale` grows them for a headset. */
    void RenderObserveLabels(const FrameContext& ctx, float width, float height, float textScale);
    void EnsureMagneticFieldsBuilt(int qualityPreset);
    /** Upload the ribbons once every trace job of the build in flight has finished. */
    void CollectMagneticFieldTraces();

    void RenderAtmospheres(rhi::CommandList& cmd, const FrameContext& ctx, const RenderableSceneComponent& component);
    void RenderOneilAtmosphere(rhi::CommandList& cmd, const FrameContext& ctx,
                               const RenderableAtmosphere& renderableAtmosphere,
                               const RenderableSceneComponent& component);
    void RenderPbrAtmosphere(rhi::CommandList& cmd, const FrameContext& ctx,
                             const RenderableAtmosphere& renderableAtmosphere,
                             const RenderableSceneComponent& component);
    void RenderClouds(rhi::CommandList& cmd, const FrameContext& ctx, const RenderableSceneComponent& component);
    // Space weather (render/passes/SpaceWeatherPasses.cpp).
    /** The component's auroral oval, additive, after its atmosphere and clouds. */
    void RenderAurora(rhi::CommandList& cmd, const FrameContext& ctx, const RenderableSceneComponent& component);
    /** Whether the Full tier's aurora bloom has an oval to draw this frame. */
    bool AuroraBloomWanted(const FrameContext& ctx) const;
    /**
     * Space weather's per-frame set-up (PrepareFrame): the photosphere program for the tier and
     * its spot uniforms, the particle groups worth running, and the MagnetosphereUBO's contents.
     */
    void PrepareSpaceWeather(const FrameContext& ctx);
    /** The aurora program for a tier's look, built on first use (null until it has compiled). */
    Shader* AuroraProgram(int auroraMode);
    /** The photosphere program for a tier (star.fs PHOTOSPHERE / SUNSPOTS), or the sprite. */
    Shader* StarProgram(int photosphereMode);
    void RenderPlanetaryRing(rhi::CommandList& cmd, const Shader& shader,
                             const RenderableSceneComponent& component) const;
    void RenderStar(rhi::CommandList& cmd, const FrameContext& ctx) const;
    void RenderSpaceObjectDistance(const FrameContext& ctx, const SpaceObject* spaceObject) const;
    void UpdateOcclusionQuery(Star& sun);
    struct EclipseUmbra {
        bool active = false;
        glm::vec3 casterCenter{0.0f};
        float casterRadius = 0.0f;
        float starRadius = 0.0f;
    };
    EclipseUmbra ComputeEclipseUmbra(const FrameContext& ctx, const RenderableSceneComponent& renderableComponent) const;
    /**
     * The planetLighting program for a CatalogMaterial variant. A variant that is still
     * compiling (or failed) draws with the one without its maps rather than stalling the frame.
     */
    const Shader& PlanetProgram(unsigned variant);
    /** Bind `program` and give it this component's shadow, ring and eclipse uniforms. */
    void ConfigurePlanetProgram(const FrameContext& ctx, const Shader& program,
                                const RenderableSceneComponent& component, const EclipseUmbra& umbra) const;
    /**
     * Whether `object` is close enough to this view's eye for the NEAR_SURFACE permutations:
     * within CameraNavigation::kNearSurfaceRadii of its bounding radius above it.
     */
    static bool NearSurface(const SpaceObject& object, const CameraView& view);
    /** NEAR_SURFACE planetLighting.vs: the relative-to-eye anchor for `object` (gfx/MeshAnchor.h). */
    static void SetMeshAnchor(const Shader& program, const SpaceObject& object, const CameraView& view);
    /**
     * After `body` drew with a NEAR_SURFACE `program`: its facets that pass within metres of
     * the eye, re-tessellated (gfx/SurfacePatch.h) and drawn over them with the same state.
     */
    void DrawSurfacePatch(const Shader& program, const SpaceObject& body, const CameraView& view);
    /**
     * `normal` with NEAR_SURFACE when `near`: the near build of a shell program once it has
     * compiled (it is submitted on first use), `normal` until then.
     */
    Shader& ShellProgram(Shader& normal, Shader*& nearCache, const ShaderSpec& nearSpec, bool near);
    /** Draw one body (planet or moon) with its own material variant. */
    void DrawLitBody(const FrameContext& ctx, const SpaceObject& body, const RenderableSceneComponent& component,
                     const EclipseUmbra& umbra);
    /**
     * terrain.vs + terrain.fs for a material variant (CatalogMaterial bits, NEAR_SURFACE
     * included), falling back as PlanetProgram does; null until even the plain one is built.
     */
    const Shader* TerrainProgram(unsigned variant);
    /** terrain.vs SHADOW_PASS + shadowMap.fs, or null until it is built. */
    const Shader* TerrainShadowProgram();
    /**
     * `body`'s terrain tiles in place of its sphere, with the material, shadow and eclipse
     * uniforms the sphere would have had. False when the programs are not built yet: the
     * caller draws the sphere instead.
     */
    bool DrawTerrain(const FrameContext& ctx, const SpaceObject& body, const Terrain::ActiveBody& tiles,
                     const RenderableSceneComponent& component, const EclipseUmbra& umbra);

    // First, so it is destroyed last: everything below may hold a Shader& from it.
    ShaderLibrary _shaders;
    std::array<Shader*, CatalogMaterial::kVariantCount> _planetVariants{}; // into _shaders; null = not ready

    std::unique_ptr<TextRenderer> _textRenderer;
    std::unique_ptr<ShadowMapFBO> _shadowMapFBO;
    std::unique_ptr<HDR> _hdr;
    bool _hdrEnabled = true;
    std::unique_ptr<SkyBox> _skyBox;
    // Owned by _shaders.
    Shader* _shadowMapShader = nullptr;
    Shader *_mainSkyBoxShader = nullptr, *_mainTextShader = nullptr, *_mainStarShader = nullptr,
           *_mainCoronaStarShader = nullptr, *_mainPlanetShader = nullptr, *_mainAtmosphereShader = nullptr,
           *_mainCloudsShader = nullptr, *_mainRingShader = nullptr;
    Shader *_hdrShader = nullptr, *_lensFlareShader = nullptr, *_starGlowShader = nullptr;
    // LUT atmosphere (atmosphere.vs + atmospherePbr.fs). Null if it failed to compile, in
    // which case every shell stays on mainAtmosphereShader.
    Shader* _pbrAtmosphereShader = nullptr;
    // NEAR_SURFACE builds of the shell programs, compiled on first use (ShellProgram).
    Shader *_nearAtmosphereShader = nullptr, *_nearPbrAtmosphereShader = nullptr, *_nearCloudsShader = nullptr;
    Shader* _coronaVolumeShader = nullptr;
    Shader* _screenFadeShader = nullptr;
    GlVertexArray _screenFadeVao; // empty: screenFade.vs builds its triangle from gl_VertexID
    std::unique_ptr<LensFlare> _lensFlare;
    std::unique_ptr<SurfacePatchMesh> _surfacePatch;
    // What _surfacePatch was last built for, so the second XR eye reuses the first's patch.
    struct SurfacePatchKey {
        const SpaceObject* body = nullptr;
        glm::dvec3 eyeModel{0.0};
        float nearPlane = 0.0f;
    } _surfacePatchKey;
    // Terrain tiles (src/render/terrain/): the shared grid and each tile's height texture.
    std::unique_ptr<TerrainRenderer> _terrainRenderer;
    std::array<Shader*, CatalogMaterial::kVariantCount> _terrainVariants{}; // into _shaders; null = not ready
    Shader* _terrainShadowShader = nullptr;
    // Null if its shaders failed to compile; Observe then draws an empty sky.
    std::unique_ptr<ObserveSky> _observeSky;
    std::unique_ptr<OrbitPathRenderer> _orbitPathRenderer;
    std::unique_ptr<XrPointerRenderer> _xrPointerRenderer;
    std::unique_ptr<MagneticFieldLineRenderer> _magneticFieldRenderer;
    // Bloom chains by FrameGraph physical slot: the field-line and aurora blooms are two
    // transients the graph aliases onto chain 0 (their lifetimes never overlap).
    std::vector<std::unique_ptr<MagneticFieldBloom>> _bloomChains;
    bool _bloomEnabled = false; // the tier's enableMagneticBloom, for chains made later
    std::unique_ptr<MagneticFieldTraceJobs> _magneticFieldTraces; // build in flight, if any
    std::vector<std::unique_ptr<MissionPathRenderer>> _missionPathMeshes;
    glm::dvec3 _missionPathOrigin{0.0}; // world point the path vertices are relative to

    // Space weather (SpaceWeatherRenderer.cpp). Programs are owned by _shaders and built on
    // first use, so a tier that never asks for one never compiles it.
    static constexpr std::size_t kSolarWindGroupCount = 7; // SolarWindRenderer::kMaxGroups
    std::unique_ptr<SpaceWeatherUniformBuffer> _spaceWeatherUbo;
    std::unique_ptr<SolarWindRenderer> _solarWind;
    std::array<bool, kSolarWindGroupCount> _solarWindGroups{};
    Shader* _solarWindUpdateShader = nullptr;
    Shader* _solarWindDrawShader = nullptr;
    // Packed by PrepareSpaceWeather, uploaded by SolarWindUpdate.
    std::unique_ptr<SpaceWeatherUniformData> _spaceWeatherData;
    std::array<Shader*, 3> _auroraShaders{}; // static, animated, curtains
    std::array<Shader*, 3> _starShaders{};   // sprite (_mainStarShader's spec), granulation, spots
    Shader* _spriteStarShader = nullptr;     // star.vs + star.fs with no permutation

    bool _orbitLinesEnabled = true;
    bool _magneticFieldsEnabled = false;
    bool _magneticFieldsBuilt = false;
    int _magneticFieldsQuality = -1;

    OverlayToggles _overlayToggles;
    StarTuning _starTuning;

    // From the quality tier (ApplyEffectQuality); 0 = effect off.
    int _pbrAtmosphereSteps = 0;
    int _coronaSlices = 0;
    int _observeStarCount = 0; // brightest catalog stars Observe draws; see QualityTierSettings
    // Corona slices follow the view direction (not the camera->Sun axis) while the camera
    // is inside the corona; kept across frames for hysteresis.
    mutable bool _coronaSlicesViewAligned = false;

    // Screen-space text (hints, texture-upgrade progress); follows the framebuffer via Resize().
    glm::mat4 _hudProjection = glm::mat4(1.0f);

    // View state of the last RenderObserveSky(), reused to project HUD labels onto the same frame.
    ObserveSky::Frame _observeFrame;

    // Pre-allocated containers for RenderHints to eliminate per-frame allocations
    mutable std::deque<wchar_t> _distanceInfoCache;
    mutable std::deque<std::wstring> _fpsHintCache;
    mutable std::deque<std::wstring> _gpuHintCache;
    mutable std::deque<std::wstring> _soundVolumeHintCache;
    mutable std::deque<std::wstring> _tmpStringCache;
    // RenderTextureLoadingProgress: the hint and the counters it was built from.
    mutable std::deque<std::wstring> _loadingHintCache;
    mutable std::array<int, 4> _loadingHintCounters{};

    FrameUniformBuffer _frameUniforms;
    unsigned int _uploadedViews = 0; // bit per view whose FrameUBO this frame already uploaded
    const CameraView* _configuredView = nullptr; // view whose matrices ConfigureMainShaders last uploaded
    FramePlan::Stats _stats;

    // The frame graph (render/graph/), rebuilt every frame, and the command list it runs on.
    FrameGraph::Graph _graph;
    std::unique_ptr<rhi::GlCommandList> _commands;
    FrameProfile::PassTimings* _passTimings = nullptr;
    const std::vector<FrameContext>* _views = nullptr; // RenderFrame's views while it runs
    std::function<void()> _uploadPump;
    bool _graphDump = false;
    std::string _graphSignature; // the last dumped graph's passes
};

#endif //SOLARSYSTEM_RENDERER_H
