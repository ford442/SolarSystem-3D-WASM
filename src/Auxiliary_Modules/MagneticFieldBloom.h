#pragma once

#include "Shader.h"
#include <cstdint>
#include <memory>

/**
 * Half-resolution bloom for magnetic field ribbons.
 *
 * Intentionally separate from HDR: that FBO only captures star glow and is
 * disabled on the Low preset. Field-line bloom is its own emissive pass
 * (capture → separable blur → additive composite) so star tone-mapping is
 * untouched.
 */
class MagneticFieldBloom {
public:
    MagneticFieldBloom(uint16_t width, uint16_t height, bool enabled);
    ~MagneticFieldBloom();

    MagneticFieldBloom(const MagneticFieldBloom&) = delete;
    MagneticFieldBloom& operator=(const MagneticFieldBloom&) = delete;

    void SetEnabled(bool enabled, uint16_t width, uint16_t height);
    bool IsEnabled() const { return _enabled; }
    void Resize(uint16_t width, uint16_t height);

    /** Bind the half-res capture target. Caller draws additive emissive content. */
    void BeginCapture();
    /** Separable blur, then additively composite onto the framebuffer that was bound before BeginCapture. */
    void BlurAndComposite(int passes, float intensity);

private:
    struct Target {
        unsigned int fbo = 0;
        unsigned int color = 0;
        unsigned int depth = 0;
    };

    void InitQuad();
    void InitTargets(uint16_t width, uint16_t height);
    void DestroyTargets();
    static void CreateColorTarget(Target& target, uint16_t width, uint16_t height, bool withDepth);
    static void DestroyTarget(Target& target);
    void DrawQuad() const;

    bool _enabled = false;
    uint16_t _fullWidth = 0;
    uint16_t _fullHeight = 0;
    uint16_t _width = 0;
    uint16_t _height = 0;
    Target _capture;
    Target _ping;
    Target _pong;
    unsigned int _quadVao = 0;
    unsigned int _quadVbo = 0;
    int _prevFbo = 0;
    int _prevViewport[4] = {0, 0, 0, 0};
    std::unique_ptr<Shader> _blurShader;
    std::unique_ptr<Shader> _compositeShader;
};
