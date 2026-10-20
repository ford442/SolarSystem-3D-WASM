#ifndef SOLARSYSTEM_FRAMEPASSES_H
#define SOLARSYSTEM_FRAMEPASSES_H
#include <cstddef>
#include <functional>

/**
 * The order of one frame's work, independent of GL so a unit test can count it
 * (tests/test_frame_passes.cpp).
 *
 * The scene is simulated exactly once, then drawn for every view (one mono view, or both XR
 * eyes). Each scene component's shadow map is rendered once and then consumed by every
 * view: there is a single shadow FBO, so the shadow pass for component N has to land
 * between the colour passes of component N-1 and N. Drawing eye by eye instead (the old
 * per-eye RenderFrameContent) re-simulated the scene and re-rendered every shadow map per
 * eye. The views are disjoint viewports of one framebuffer, so interleaving them per
 * component draws the same pixels as drawing them one after the other.
 */
namespace FramePasses {

/** What one frame did; Renderer::stats, logged at debug level. */
struct Stats {
    int sceneUpdates = 0;
    int viewBinds = 0;      // per-view uniform uploads (1 mono; XR re-binds each eye per component)
    int shadowPasses = 0;   // shadow maps actually rendered (0 with shadows off)
    int componentDraws = 0; // component colour passes, summed over views
    unsigned long long uniformCalls = 0; // glUniform* calls over the whole previous frame (HUD included)
};

struct Hooks {
    /** Simulate the frame; returns how many scene components to draw. */
    std::function<std::size_t()> updateScene;
    /** Bind view `view` (viewport, scissor, per-view uniforms). Called before each block of view work. */
    std::function<void(int view)> beginView;
    /** Everything drawn before the scene components: sky, orbit lines, flat corona. */
    std::function<void(int view)> background;
    /** Render component `component`'s shadow map (no-op when shadows are off). */
    std::function<void(std::size_t component)> shadowPass;
    std::function<void(int view, std::size_t component)> drawComponent;
    /** Everything drawn after: volumetric corona, belts, field lines, star glow. */
    std::function<void(int view)> foreground;
};

inline void Run(int viewCount, const Hooks& hooks) {
    const std::size_t componentCount = hooks.updateScene ? hooks.updateScene() : 0;
    if (viewCount <= 0) {
        return;
    }
    for (int view = 0; view < viewCount; ++view) {
        hooks.beginView(view);
        hooks.background(view);
    }
    for (std::size_t component = 0; component < componentCount; ++component) {
        hooks.shadowPass(component);
        for (int view = 0; view < viewCount; ++view) {
            hooks.beginView(view);
            hooks.drawComponent(view, component);
        }
    }
    for (int view = 0; view < viewCount; ++view) {
        hooks.beginView(view);
        hooks.foreground(view);
    }
}

} // namespace FramePasses

#endif // SOLARSYSTEM_FRAMEPASSES_H
