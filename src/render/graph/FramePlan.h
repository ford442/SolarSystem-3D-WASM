#ifndef SOLARSYSTEM_RENDER_GRAPH_FRAMEPLAN_H
#define SOLARSYSTEM_RENDER_GRAPH_FRAMEPLAN_H
#include "FrameGraph.h"
#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include <cstddef>
#include <string>
#include <vector>

namespace FrameProfile {
class PassTimings;
}

/**
 * This application's frame, declared on a FrameGraph::Graph (docs/ARCHITECTURE.md §7.5).
 * GL-free: Renderer implements PassSink with the real draws, tests/test_frame_plan.cpp a
 * recorder. Which passes exist is decided here from Features, once; the graph then culls what
 * nothing consumes (the solar-wind advection when no particle draw reads it).
 *
 * Order (one view, or both XR eyes per block):
 *   solar-wind-update
 *   per view: observe-sky | skybox, orbit-paths, mission-paths, xr-pointers, corona
 *   per component: shadow:<name> (or one shadow-clear with shadows off), then scene:<name>/<view>
 *   per view: corona-volume, asteroids, solar-wind, magnetic-fields, magnetic-bloom(+composite),
 *             aurora-bloom(+composite)
 *   mono: proxy-labels, star-glow, hdr-composite, lens-flare
 *   mono: observe-hud | distances, hints; screen-fade
 *   uploads, loading-progress
 *
 * There is one shadow map: component N's shadow pass overwrites what N-1's colour passes read,
 * so the graph's write-after-read edge is what keeps every view's colour pass between them.
 */
namespace FramePlan {

/** What one frame did; Renderer::Stats(), logged at debug level as [FrameStats]. */
struct Stats {
    int sceneUpdates = 0;
    int viewBinds = 0;                   // per-view uniform uploads (1 mono; XR re-binds each eye per component)
    int shadowPasses = 0;                // shadow maps actually rendered (0 with shadows off)
    int componentDraws = 0;              // colour passes of visible components, summed over views
    int bodyDraws = 0;                   // planets and moons drawn in colour passes, summed over views
    int culledComponents = 0;            // components no view could see (no shadow or colour pass)
    int passes = 0;                      // frame-graph passes that ran
    int culledPasses = 0;                // declared but culled (nothing read what they wrote)
    unsigned long long uniformCalls = 0; // glUniform* calls over the whole previous frame (HUD included)
};

/** A scene component in draw order (front to back). */
struct Component {
    std::string name;         // label for the pass ("Earth")
    bool castsShadow = false; // visible: it gets a shadow pass (the nearest is drawn hidden, for the star)
};

/** Which passes this frame has. Renderer::DescribeFrame fills the render toggles, Application the rest. */
struct Features {
    int views = 1;
    bool xr = false;
    bool observe = false;
    std::vector<Component> components;

    bool shadowTarget = true; // false: XR with no layer framebuffer to return to, so no shadow pass at all
    bool shadows = true;      // shadowQuality > 0; off still clears the map once (cleared = fully lit)

    bool orbitPaths = false;
    bool missionPaths = false;
    bool xrPointers = false;
    bool flatCorona = false;
    bool coronaVolume = false;
    bool asteroids = false;
    bool solarWindSystem = false; // the particle system exists: its advection is declared
    bool solarWind = false;       // the particles are drawn
    bool magneticFields = false;
    bool magneticBloom = false;
    bool auroraBloom = false;

    bool starEffects = false; // mono Explore: star glow and lens flare
    bool hdr = false;         // the glow goes through the HDR target and its tone-map composite
    bool proxyLabels = false;

    bool observeHud = false;
    bool distances = false;
    bool hints = false;
    bool screenFade = false;
    bool uploads = true;
    bool loadingProgress = false;

    rhi::TextureDesc display;   // the window's framebuffer
    rhi::TextureDesc shadowMap; // the shadow map's depth texture
};

/** The draws behind each pass. `view` indexes the frame's views; `component` the draw order. */
class PassSink {
public:
    virtual ~PassSink() = default;
    /** Viewport and per-view uniforms of `view`: called before every per-view pass. */
    virtual void BeginView(rhi::CommandList& cmd, int view) = 0;

    virtual void SolarWindUpdate(rhi::CommandList& cmd) = 0;
    virtual void ObserveSkyPass(rhi::CommandList& cmd, int view) = 0;
    virtual void SkyBoxPass(rhi::CommandList& cmd, int view) = 0;
    virtual void OrbitPaths(rhi::CommandList& cmd, int view) = 0;
    virtual void MissionPaths(rhi::CommandList& cmd, int view) = 0;
    virtual void XrPointers(rhi::CommandList& cmd, int view) = 0;
    virtual void FlatCorona(rhi::CommandList& cmd, int view) = 0;

    virtual void ShadowMap(rhi::CommandList& cmd, std::size_t component) = 0;
    virtual void ClearShadowMap(rhi::CommandList& cmd) = 0;
    virtual void SceneComponent(rhi::CommandList& cmd, int view, std::size_t component) = 0;

    virtual void CoronaVolume(rhi::CommandList& cmd, int view) = 0;
    virtual void Asteroids(rhi::CommandList& cmd, int view) = 0;
    virtual void SolarWind(rhi::CommandList& cmd, int view) = 0;
    /** The ribbons; `restoreState` when no bloom pass follows to put the GL state back. */
    virtual void MagneticFields(rhi::CommandList& cmd, int view, bool restoreState) = 0;
    /** Ribbons again, wider, into `bloomSlot`'s capture target. */
    virtual void MagneticBloom(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) = 0;
    virtual void MagneticBloomComposite(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) = 0;
    virtual void AuroraBloom(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) = 0;
    virtual void AuroraBloomComposite(rhi::CommandList& cmd, int view, std::uint32_t bloomSlot) = 0;

    virtual void ProxyLabels(rhi::CommandList& cmd) = 0;
    virtual void StarGlow(rhi::CommandList& cmd) = 0;
    virtual void HdrComposite(rhi::CommandList& cmd) = 0;
    virtual void LensFlarePass(rhi::CommandList& cmd) = 0;

    virtual void ObserveHud(rhi::CommandList& cmd) = 0;
    virtual void Distances(rhi::CommandList& cmd) = 0;
    virtual void Hints(rhi::CommandList& cmd) = 0;
    virtual void ScreenFade(rhi::CommandList& cmd) = 0;
    virtual void Uploads(rhi::CommandList& cmd) = 0;
    virtual void LoadingProgress(rhi::CommandList& cmd) = 0;
};

/** Resources the plan declares, for whoever maps them onto GPU objects after Compile(). */
struct Resources {
    FrameGraph::ResourceHandle backbuffer, depth, shadowMap, hdrColor, particles;
    FrameGraph::ResourceHandle fieldBloom, auroraBloom; // transients; aliased onto one chain when they can be
};

/**
 * Reset `graph` and declare this frame on it. With `timings`, each pass gets the timer slot of
 * its kind. Returns the resource handles; call graph.Compile() next.
 */
Resources Build(FrameGraph::Graph& graph, const Features& features, PassSink& sink,
                FrameProfile::PassTimings* timings = nullptr);

/** The bloom chain's description at a display size (half resolution, RGBA16F). */
rhi::TextureDesc BloomDesc(const rhi::TextureDesc& display);

} // namespace FramePlan

#endif // SOLARSYSTEM_RENDER_GRAPH_FRAMEPLAN_H
