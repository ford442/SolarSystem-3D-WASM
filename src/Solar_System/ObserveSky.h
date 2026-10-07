#ifndef SOLARSYSTEM_OBSERVESKY_H
#define SOLARSYSTEM_OBSERVESKY_H

#include "../Auxiliary_Modules/Observer.h"
#include "../Auxiliary_Modules/Shader.h"
#include "../Auxiliary_Modules/StarCatalog.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <memory>
#include <vector>

/**
 * The Observe-mode sky pass: a ground-level view of the Sun, Moon and naked-eye planets at
 * their true angular sizes, drawn straight in the observer's horizon frame (+X East, +Y Up,
 * +Z -North — see Observer.h) with the camera at the origin.
 *
 * It deliberately does not use scene meshes: at scene scale the Sun is ~9x too small and the
 * Moon ~5x too big, and the skybox cubemap's frame against J2000 is undocumented. Everything
 * here comes from Observer::ComputeSky, so there is no depth buffer — layers are drawn back
 * to front (sky/ground, additive point sprites and the Sun, then the opaque-ish Moon).
 */
class ObserveSky {
public:
    /** One camera's worth of view state; XR eyes fill the same struct. */
    struct Frame {
        glm::mat3 viewRot{1.0f};     // horizon frame → camera space
        glm::mat4 projection{1.0f};
        int viewportWidth = 1;
        int viewportHeight = 1;
    };

    ObserveSky();
    ~ObserveSky();

    ObserveSky(const ObserveSky&) = delete;
    ObserveSky& operator=(const ObserveSky&) = delete;

    /**
     * `moonTexture` is the Moon's equirectangular diffuse map (GL_TEXTURE_2D, longitude 0 at the
     * centre, north at the top); 0 draws the plain lit disc until the Moon's system has loaded.
     */
    void Render(const Observer::Sky& sky, const Frame& frame, unsigned int moonTexture = 0);

    /** Cap on how many of the brightest catalog stars are drawn (a quality tier's prefix). */
    void SetStarLimit(int count) { _starLimit = count < 0 ? 0 : count; }
    /** Stars actually submitted last frame: the limit clamped to the catalog size. */
    int DrawnStarCount() const { return _starsLoaded ? std::min(_starLimit, _starBatch.count) : 0; }
    /** The star catalog, loaded on first use; empty if resource/sky/bright_stars.json is missing. */
    const StarCatalog::Catalog& Stars() const { return _stars; }

    /** Faintest magnitude that stands out against the sky for a given Sun altitude. */
    static float LimitingMagnitude(double sunAltDeg);
    /** LimitingMagnitude plus the twilight-like sky of a total solar eclipse; what is actually drawn. */
    static float EffectiveLimitingMagnitude(const Observer::Sky& sky);

    /** Horizon-frame direction → window pixels (origin bottom-left). False when behind the eye. */
    static bool ProjectToPixels(const glm::vec3& dir, const Frame& frame, glm::vec2& outPixels);

private:
    struct SpriteInstance {
        glm::vec4 posSize;  // direction xyz, half-extent in tan-plane units
        glm::vec4 color;    // rgb, alpha / intensity
        glm::vec4 params;   // x = kind, y = disc radius / extent, zw unused
    };

    /** One instanced draw's worth of sprites with its own VAO/VBO. */
    struct SpriteBatch {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        int capacity = 0;
        int count = 0;
    };

    void InitBatch(SpriteBatch& batch, int capacity) const;
    void UploadBatch(SpriteBatch& batch, const std::vector<SpriteInstance>& instances) const;
    void DrawBatch(const SpriteBatch& batch, int count) const;
    /** Read the catalog and upload it once, the first time a frame needs stars. */
    void EnsureStarsLoaded();

    std::unique_ptr<Shader> _skyShader;
    std::unique_ptr<Shader> _spriteShader;
    unsigned int _emptyVao = 0;
    unsigned int _quadVbo = 0;
    SpriteBatch _additiveBatch; // planets plus the Sun
    SpriteBatch _moonBatch;
    SpriteBatch _starBatch;      // static: every catalog star, uploaded once
    StarCatalog::Catalog _stars;
    bool _starsLoaded = false;
    bool _starsTried = false;
    int _starLimit = 5000;
    std::vector<SpriteInstance> _scratch;
};

#endif // SOLARSYSTEM_OBSERVESKY_H
