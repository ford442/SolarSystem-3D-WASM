#ifndef SOLARSYSTEM_CINEMATICS_H
#define SOLARSYSTEM_CINEMATICS_H
#include <glm/glm.hpp>
#include <string>

/**
 * Offline capture for web timelines and video (docs/ARCHITECTURE.md §11.2), and the native
 * golden capture built on it (scripts/golden-native.sh): while a capture runs, frames come only
 * from Step(), each a fixed step of the animation clock rather than of the wall clock, and a
 * frame can be read back before its swap.
 *
 * Not an IAppModule: it puts nothing in the scene snapshot; it decides how far each frame
 * advances (FrameDelta) and runs frames itself through its Host.
 */
class Cinematics {
public:
    /** What a capture needs of the application that owns it. */
    class Host {
    public:
        Host() = default;
        virtual ~Host() = default;
        Host(const Host&) = delete;
        Host& operator=(const Host&) = delete;
        /** One whole frame (simulate, draw, swap). */
        virtual void RunCaptureFrame() = 0;
        /** Follow a new render size (web: the canvas backing store was set to it). */
        virtual void ResizeRenderTarget(int width, int height) = 0;
        /** Web: size the canvas to its CSS box again. */
        virtual void FitRenderTargetToPage() = 0;
        virtual bool XrPresenting() const = 0;
        virtual glm::ivec2 RenderTargetSize() const = 0;
        /** The next live frame measures its delta from now. */
        virtual void RestartFrameClock() = 0;
        /** Shader time (SimState::animationSeconds) back to 0. */
        virtual void ResetAnimationClock() = 0;
    };

    explicit Cinematics(Host& host) : _host(host) {}

    /** Start a capture; a `width` × `height` above 0 renders at that size (web) until End(). */
    void Begin(int width, int height);
    /** Draw one capture frame `deltaSeconds` after the previous one (0 redraws the same instant). */
    void Step(double deltaSeconds);
    void End();
    bool Active() const { return _active; }
    /** Begin set the render size: window and canvas resizes leave it alone until End. */
    bool RenderSizeLocked() const { return _resized; }
    /** How far this frame advances: a capture's fixed step, else `wallDelta`. */
    double FrameDelta(double wallDelta) const { return _active ? _stepSeconds : wallDelta; }

    /** Golden capture: the hint list's FPS line holds still. */
    bool FrozenHud() const { return _frozenHud; }
    void SetFrozenHud(bool frozen) { _frozenHud = frozen; }
#ifndef __EMSCRIPTEN__
    /** Write the next frame's back buffer to `path` (binary PPM) before it is swapped. */
    void RequestReadback(std::string path) { _readbackPath = std::move(path); }
    /** Before the swap: the requested readback, if any, of a `size` back buffer. */
    void BeforeSwap(glm::ivec2 size);
#endif

private:
    Host& _host;
    bool _active = false;
    double _stepSeconds = 0.0;
    bool _resized = false; // Begin set a render size; End fits the canvas again
#ifdef __EMSCRIPTEN__
    int _width = 0; // that size, re-asserted before every captured frame
    int _height = 0;
#endif
    bool _frozenHud = false;
    std::string _readbackPath;
};

#endif // SOLARSYSTEM_CINEMATICS_H
