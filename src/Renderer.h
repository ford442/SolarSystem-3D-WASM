#ifndef SOLARSYSTEM_RENDERER_H
#define SOLARSYSTEM_RENDERER_H
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "FramePasses.h"
#include "SystemModules.h"
#include "gfx/FrameUniforms.h"
#include "astro/MissionCatalog.h"
#include "gfx/ShaderLibrary.h"
#include "Solar_System/CatalogMaterial.h"
#include "Solar_System/ObserveSky.h"
#include <array>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class SpaceObject;
struct QualityTierSettings;
// GPU helpers held by unique_ptr: complete in the Renderer TUs only, which keeps their
// headers out of everything that includes Application.h.
class HDR;
class LensFlare;
class MagneticFieldBloom;
class MagneticFieldLineRenderer;
class MissionPathRenderer;
class OrbitPathRenderer;
class ShadowMapFBO;
class SkyBox;
class SurfacePatchMesh;
class TextRenderer;
class XrPointerRenderer;

/**
 * Shadow/color/overlay/HDR draw passes, the GPU resources they own (shaders, shadow FBO,
 * HDR, skybox, lens flare, text renderer, orbit/mission/magnetic-field overlay meshes) and
 * the render-only toggles (orbit lines, magnetic fields, hint overlay, star tuning).
 *
 * Every pass takes the frame's FrameContext: the camera view, a snapshot of the scene and
 * the sim/quality state. That is all a pass may read — the renderer has no reference back
 * to Application, and nothing in the scene moves during a pass (Application::UpdateScene
 * placed it before the first one). See FrameContext.h.
 */
class Renderer {
public:
    Renderer();
    ~Renderer(); // out of line: the unique_ptr members above are incomplete here
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
    /** Atmosphere/corona effect levels from a quality tier (preset changes call this too). */
    void ApplyEffectQuality(const QualityTierSettings& settings);
    /**
     * Quality/shadow changes: resize the shadow map (unless shadows are off) and switch HDR
     * and the field-line bloom on or off. HDR can refuse (no float FBO); HdrEnabled() follows.
     */
    void ApplyRenderResources(const SimState& sim, uint16_t shadowResolution, bool enableHdr, glm::ivec2 display);
    void ResizeShadowMap(uint16_t resolution);
    bool HdrEnabled() const { return _hdrEnabled; }

    // One frame, as FramePasses::Run orders it. The passes only read scene state: everything
    // that moves (placement, light matrices, belts, LOD) happened in Application::UpdateScene.
    void BeginFrame();
    /** Bind `ctx.view`: its viewport, and its matrices the first time it is bound this frame. */
    void BeginView(const FrameContext& ctx);
    void RenderBackground(const FrameContext& ctx);
    void ShadowMapPass(const FrameContext& ctx, const RenderableSceneComponent& component);
    void RenderPass(const FrameContext& ctx, const RenderableSceneComponent& component);
    void RenderForeground(const FrameContext& ctx);
    /** Mono only: staged-loading proxy labels, then the star glow, HDR composite and lens flare. */
    void RenderMonoEffects(const FrameContext& ctx);
    /** Screen-space text after the scene: Observe's HUD, or distances and the hint list. */
    void RenderHud(const FrameContext& ctx);
    /** Texture-upgrade progress line; also reports streaming progress to the page on web. */
    void RenderTextureLoadingProgress(const FrameContext& ctx) const;
    /** Whether atmospheres with LUTs should take the physical path this frame. */
    bool PhysicalAtmosphereAvailable() const { return _pbrAtmosphereSteps > 0 && _pbrAtmosphereShader != nullptr; }
    /** This frame's pass counts (reset by BeginFrame). */
    const FramePasses::Stats& Stats() const { return _stats; }
    void CountSceneUpdate() { ++_stats.sceneUpdates; }
    void CountCulledComponents(int count) { _stats.culledComponents = count; }

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
    /** Null before Init() and after Release(). */
    XrPointerRenderer* GetXrPointers() { return _xrPointerRenderer.get(); }
    /** Brightest catalog stars Observe draws right now (0 without its shaders). */
    int ObserveStarsDrawn() const;

private:
    void ConfigureMainShaders(const FrameContext& ctx);
    void ProcessStarRendering(const FrameContext& ctx);
    /** Flat corona billboard (Low, or coronaSlices == 0); drawn before the planets. */
    void RenderStarCorona(const FrameContext& ctx) const;
    /** Instanced-slice corona (coronaSlices > 0); drawn after the planets so they occlude it. */
    void RenderStarCoronaVolume(const FrameContext& ctx) const;
    void RenderStarEffects(const FrameContext& ctx) const;
    void RenderPlanetProxyMarkers(const FrameContext& ctx) const;
    void RenderPlanetSatelliteStarDistances(const FrameContext& ctx) const;
    void RenderHints(const FrameContext& ctx) const;
    void RenderOrbitPaths(const FrameContext& ctx) const;
    void RenderMagneticFields(const FrameContext& ctx);
    void RenderAsteroidField(const FrameContext& ctx) const;
    void RenderMissionPaths(const FrameContext& ctx) const;
    void RenderXrPointers(const FrameContext& ctx) const;
    /** Observe mode: the ground-level sky pass (replaces every scene pass). */
    void RenderObserveSky(const FrameContext& ctx);
    /** Observe mode: compass, body labels and the readout line, drawn over the sky. */
    void RenderObserveHud(const FrameContext& ctx);
    /** Compass points and body/star names projected onto `_observeFrame`; `textScale` grows them for a headset. */
    void RenderObserveLabels(const FrameContext& ctx, float width, float height, float textScale);
    void EnsureMagneticFieldsBuilt(int qualityPreset);

    void RenderAtmospheres(const FrameContext& ctx, const RenderableSceneComponent& component);
    void RenderOneilAtmosphere(const FrameContext& ctx, const RenderableAtmosphere& renderableAtmosphere,
                               const RenderableSceneComponent& component);
    void RenderPbrAtmosphere(const FrameContext& ctx, const RenderableAtmosphere& renderableAtmosphere,
                             const RenderableSceneComponent& component);
    void RenderClouds(const FrameContext& ctx, const RenderableSceneComponent& component);
    void RenderPlanetaryRing(const Shader& shader, const RenderableSceneComponent& component) const;
    void RenderStar(const FrameContext& ctx) const;
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
    std::unique_ptr<LensFlare> _lensFlare;
    std::unique_ptr<SurfacePatchMesh> _surfacePatch;
    // What _surfacePatch was last built for, so the second XR eye reuses the first's patch.
    struct SurfacePatchKey {
        const SpaceObject* body = nullptr;
        glm::dvec3 eyeModel{0.0};
        float nearPlane = 0.0f;
    } _surfacePatchKey;
    // Null if its shaders failed to compile; Observe then draws an empty sky.
    std::unique_ptr<ObserveSky> _observeSky;
    std::unique_ptr<OrbitPathRenderer> _orbitPathRenderer;
    std::unique_ptr<XrPointerRenderer> _xrPointerRenderer;
    std::unique_ptr<MagneticFieldLineRenderer> _magneticFieldRenderer;
    std::unique_ptr<MagneticFieldBloom> _magneticFieldBloom;
    std::vector<std::unique_ptr<MissionPathRenderer>> _missionPathMeshes;
    glm::dvec3 _missionPathOrigin{0.0}; // world point the path vertices are relative to

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
    FramePasses::Stats _stats;
};

#endif //SOLARSYSTEM_RENDERER_H
