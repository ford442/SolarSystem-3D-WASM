#ifndef SOLARSYSTEM_RENDERER_H
#define SOLARSYSTEM_RENDERER_H
#include "ApplicationTypes.h"
#include "SystemModules.h"
#include <deque>
#include <functional>
#include <string>

class Application;
class SpaceObject;
struct MagneticFieldParams;

/**
 * Shadow/color/overlay/HDR draw passes, the GPU resources they own (shaders, shadow FBO,
 * HDR, skybox, lens flare, text renderer, orbit/mission/magnetic-field overlay meshes) and
 * the render-only toggles (orbit lines, magnetic fields, hint overlay, star tuning).
 *
 * Extracted off Application:: (see SceneRenderer.cpp / SceneOverlayRenderer.cpp); scene
 * data that outlives a single frame — the camera, the planet list, the sun, mission and
 * asteroid state — is still Application-owned; Renderer reaches it through the `_app` back
 * reference rather than duplicating storage. That reference is why Renderer is declared a
 * friend of Application instead of exposing a getter for every field it reads.
 */
class Renderer {
public:
    explicit Renderer(Application& app) : _app(app) {}

    /** Build shaders + GPU resources sized to the current display/quality settings. */
    void Init();

    void ConfigureMainShaders();
    void ProcessSceneComponentsRendering();
    void ProcessStarRendering();
    void RenderStarCorona() const;
    void RenderStarEffects() const;
    void RenderPlanetSatelliteStarDistances() const;
    void RenderHints() const;
    void RenderTextureLoadingProgress() const;
    void RenderOrbitPaths() const;
    void RenderMagneticFields();
    void RenderAsteroidField();
    void RenderMissionPaths() const;
    void RenderXrPointers() const;
    void EnsureMagneticFieldsBuilt();
    void RebuildMissionPaths();

    void SetOrbitLinesEnabled(bool enabled) { orbitLinesEnabled = enabled; }
    bool GetOrbitLinesEnabled() const { return orbitLinesEnabled; }
    void SetMagneticFieldsEnabled(bool enabled);
    bool GetMagneticFieldsEnabled() const { return magneticFieldsEnabled; }
    void ForEachEnabledMagneticField(
        const std::function<void(const SpaceObject& object, const MagneticFieldParams& params)>& fn) const;

    // GPU resources. Public: every module that constructs the scene (StarSystemFactory.cpp),
    // resizes on quality/window changes (RenderSettings.cpp, PlatformWindow.cpp) or draws
    // proxy markers during staged loading (PlanetSystemLoader.cpp) already reached these
    // directly as Application:: members before this split — keeping them public here avoids
    // a getter per field for what is still, in effect, internal engine wiring. Tightening
    // this is a follow-up, not a blocker for getting them off Application.h.
    std::unique_ptr<TextRenderer> textRenderer;
    std::unique_ptr<ShadowMapFBO> shadowMapFBO;
    std::unique_ptr<HDR> hdr;
    bool hdrEnabled = true;
    std::unique_ptr<SkyBox> skyBox;
    std::unique_ptr<Shader> shadowMapShader;
    std::unique_ptr<Shader> mainSkyBoxShader, mainTextShader, mainStarShader, mainCoronaStarShader,
        mainPlanetShader, mainAtmosphereShader, mainCloudsShader, mainRingShader;
    std::unique_ptr<Shader> hdrShader, lensFlareShader, starGlowShader;
    std::unique_ptr<LensFlare> lensFlare;
    std::unique_ptr<OrbitPathRenderer> orbitPathRenderer;
    std::unique_ptr<XrPointerRenderer> xrPointerRenderer;
    std::unique_ptr<MagneticFieldLineRenderer> magneticFieldRenderer;
    std::unique_ptr<MagneticFieldBloom> magneticFieldBloom;
    std::vector<std::unique_ptr<MissionPathRenderer>> missionPathMeshes;

    bool orbitLinesEnabled = true;
    bool magneticFieldsEnabled = false;
    bool magneticFieldsBuilt = false;
    int magneticFieldsQuality = -1;

    bool isRenderHints = true;
    bool isRenderPlanetStarDistances = true;
    bool isRenderSatelliteDistances = true;

    float starExposure = 8.0f;
    float starGamma = 0.4545454f;
    float starTemperatureInKelvin = 5778.0f;

    // Current frame's camera matrices, set by ConfigureMainShaders(); read externally only
    // by PlanetSystemLoader.cpp's proxy-marker text pass, which draws after these are set.
    glm::mat4 cameraProjection = glm::mat4();
    glm::mat4 cameraView = glm::mat4();

private:
    void ShadowMapPass(const RenderableSceneComponent& component);
    void RenderPass(const RenderableSceneComponent& component);
    void RenderAtmospheres(const std::vector<RenderableAtmosphere>& renderableAtmospheres,
                           const glm::mat4& lightSpaceMatrix, const PlanetaryRing* ring) const;
    void RenderClouds(Clouds* renderableClouds, const glm::mat4& lightSpaceMatrix) const;
    void RenderPlanetaryRing(const Shader& shader, PlanetaryRing* planetaryRing, const glm::mat4& lightSpaceMatrix) const;
    void RenderStar() const;
    void RenderSpaceObjectDistance(const SpaceObject* spaceObject) const;
    void UpdateOcclusionQuery();
    void ConfigureMainPlanetShader(const RenderableSceneComponent& renderableComponent);
    /** Upload (or clear) the one moon currently casting an umbra on this component's planet. */
    void ConfigureEclipseUmbra(const RenderableSceneComponent& renderableComponent);

    // Pre-allocated containers for RenderHints to eliminate per-frame allocations
    mutable std::deque<wchar_t> distanceInfoCache;
    mutable std::deque<std::wstring> fpsHintCache;
    mutable std::deque<std::wstring> gpuHintCache;
    mutable std::deque<std::wstring> soundVolumeHintCache;
    mutable std::deque<std::wstring> tmpStringCache;

    Application& _app;
};

#endif //SOLARSYSTEM_RENDERER_H
