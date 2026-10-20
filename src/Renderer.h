#ifndef SOLARSYSTEM_RENDERER_H
#define SOLARSYSTEM_RENDERER_H
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "FramePasses.h"
#include "SystemModules.h"
#include "gfx/FrameUniforms.h"
#include "Auxiliary_Modules/MissionCatalog.h"
#include "Solar_System/ObserveSky.h"
#include <array>
#include <deque>
#include <string>

class SpaceObject;
struct QualityTierSettings;

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

    /** Re-upload every mission path in the render frame (origin move, scale or quality change). */
    void RebuildMissionPaths(const MissionCatalog::Catalog& catalog, int qualityPreset);
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

    // Programs the scene objects draw with. Bodies keep a reference (SetShader), so these
    // must exist (Init) before StarSystemFactory builds anything.
    Shader& StarShader() { return *_mainStarShader; }
    Shader& StarGlowShader() { return *_starGlowShader; }
    Shader& PlanetShader() { return *_mainPlanetShader; }
    Shader& AtmosphereShader() { return *_mainAtmosphereShader; }
    Shader& CloudsShader() { return *_mainCloudsShader; }

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

    void RenderAtmospheres(const FrameContext& ctx, const RenderableSceneComponent& component) const;
    void RenderOneilAtmosphere(const FrameContext& ctx, const RenderableAtmosphere& renderableAtmosphere,
                               const RenderableSceneComponent& component) const;
    void RenderPbrAtmosphere(const FrameContext& ctx, const RenderableAtmosphere& renderableAtmosphere,
                             const RenderableSceneComponent& component) const;
    void RenderClouds(Clouds* renderableClouds, const glm::mat4& lightSpaceMatrix) const;
    void RenderPlanetaryRing(const Shader& shader, PlanetaryRing* planetaryRing, const glm::mat4& lightSpaceMatrix) const;
    void RenderStar(const FrameContext& ctx) const;
    void RenderSpaceObjectDistance(const FrameContext& ctx, const SpaceObject* spaceObject) const;
    void UpdateOcclusionQuery(Star& sun);
    void ConfigureMainPlanetShader(const FrameContext& ctx, const RenderableSceneComponent& renderableComponent);
    /** Upload (or clear) the one moon currently casting an umbra on this component's planet. */
    void ConfigureEclipseUmbra(const FrameContext& ctx, const RenderableSceneComponent& renderableComponent);
    struct EclipseUmbra {
        bool active = false;
        glm::vec3 casterCenter{0.0f};
        float casterRadius = 0.0f;
        float starRadius = 0.0f;
    };
    EclipseUmbra ComputeEclipseUmbra(const FrameContext& ctx, const RenderableSceneComponent& renderableComponent) const;

    std::unique_ptr<TextRenderer> _textRenderer;
    std::unique_ptr<ShadowMapFBO> _shadowMapFBO;
    std::unique_ptr<HDR> _hdr;
    bool _hdrEnabled = true;
    std::unique_ptr<SkyBox> _skyBox;
    std::unique_ptr<Shader> _shadowMapShader;
    std::unique_ptr<Shader> _mainSkyBoxShader, _mainTextShader, _mainStarShader, _mainCoronaStarShader,
        _mainPlanetShader, _mainAtmosphereShader, _mainCloudsShader, _mainRingShader;
    std::unique_ptr<Shader> _hdrShader, _lensFlareShader, _starGlowShader;
    // LUT atmosphere (atmosphere.vs + atmospherePbr.fs). Null if it failed to compile, in
    // which case every shell stays on mainAtmosphereShader.
    std::unique_ptr<Shader> _pbrAtmosphereShader;
    std::unique_ptr<Shader> _coronaVolumeShader;
    std::unique_ptr<LensFlare> _lensFlare;
    // Null if its shaders failed to compile; Observe then draws an empty sky.
    std::unique_ptr<ObserveSky> _observeSky;
    std::unique_ptr<OrbitPathRenderer> _orbitPathRenderer;
    std::unique_ptr<XrPointerRenderer> _xrPointerRenderer;
    std::unique_ptr<MagneticFieldLineRenderer> _magneticFieldRenderer;
    std::unique_ptr<MagneticFieldBloom> _magneticFieldBloom;
    std::vector<std::unique_ptr<MissionPathRenderer>> _missionPathMeshes;

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
