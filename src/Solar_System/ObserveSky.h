#ifndef SOLARSYSTEM_OBSERVESKY_H
#define SOLARSYSTEM_OBSERVESKY_H

#include "../astro/Observer.h"
#include "../gfx/Shader.h"
#include "../gfx/GlHandle.h"
#include "../astro/StarCatalog.h"
#include "AtmosphereModel.h"
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
 * to front (sky/ground, constellation lines, additive point sprites and the Sun, then the
 * opaque-ish Moon). Bodies are drawn at their refracted (apparent) positions.
 *
 * The sky is Earth's baked LUT atmosphere seen from inside the shell when an `Air` is passed
 * (the presets with enablePbrAtmosphere), else the analytic gradient. See docs/ARCHITECTURE.md.
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

    /** Earth's physical atmosphere for the daytime sky. */
    struct Air {
        unsigned int transmittanceLut = 0;
        unsigned int multiScatteringLut = 0;
        AtmosphereModel::Params params;
        float exposure = 1.0f;  // the catalog's render.atmosphere.physical.exposure
        int steps = 0;          // view-ray samples; 0 draws the gradient sky
    };

    ObserveSky();

    ObserveSky(const ObserveSky&) = delete;
    ObserveSky& operator=(const ObserveSky&) = delete;

    /**
     * `moonTexture` is the Moon's equirectangular diffuse map (GL_TEXTURE_2D, longitude 0 at the
     * centre, north at the top); 0 draws the plain lit disc until the Moon's system has loaded.
     */
    void Render(const Observer::Sky& sky, const Frame& frame, unsigned int moonTexture = 0,
                const Air* air = nullptr);

    /**
     * Colour factor for the Sun or Moon disc seen through the air at `apparentAltDeg`: the hue
     * of the LUT model's transmittance along the view ray, with its strength compressed (the
     * eye adapts to a setting Sun; a straight 1-2% would leave a dim brown dot). White above
     * the air or with no physical atmosphere.
     */
    static glm::vec3 DiscTint(const Air* air, double apparentAltDeg, double observerAltitudeM);

    /** Cap on how many of the brightest catalog stars are drawn (a quality tier's prefix). */
    void SetStarLimit(int count) { _starLimit = count < 0 ? 0 : count; }
    /** Constellation stick figures on/off (the Observe panel's toggle). */
    void SetConstellationsVisible(bool visible) { _constellationsVisible = visible; }
    bool ConstellationsVisible() const { return _constellationsVisible; }
    /** Only the 20 `core` figures (the Low preset). */
    void SetCoreConstellationsOnly(bool coreOnly) { _coreConstellationsOnly = coreOnly; }
    /** Line segments the current star limit and figure set draw (0 until the files have loaded). */
    int ConstellationSegmentCount() const { return _lineSegmentCount; }
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
        glm::vec4 posSize;  // direction xyz, half-extent (tan-plane units for discs, pixels for points)
        glm::vec4 color;    // rgb, alpha / intensity
        glm::vec4 params;   // x = kind, y = disc radius / extent (0: a planet point), z/w = a resolved
                            // planet's phase angle and bright-limb angle in the tangent basis, radians
    };

    /** One instanced draw's worth of sprites with its own VAO/VBO. */
    struct SpriteBatch {
        GlVertexArray vao;
        GlBuffer vbo;
        int capacity = 0;
        int count = 0;
    };

    void InitBatch(SpriteBatch& batch, int capacity) const;
    void UploadBatch(SpriteBatch& batch, const std::vector<SpriteInstance>& instances) const;
    void DrawBatch(const SpriteBatch& batch, int count) const;
    /** Read the catalog and upload it once, the first time a frame needs stars. */
    void EnsureStarsLoaded();
    /** (Re)build the line VBO for the current star limit and figure set. */
    void EnsureConstellationLines();

    std::unique_ptr<Shader> _skyShader;
    std::unique_ptr<Shader> _spriteShader;
    std::unique_ptr<Shader> _lineShader;
    GlVertexArray _emptyVao;
    GlBuffer _quadVbo;
    SpriteBatch _additiveBatch; // planets plus the Sun
    SpriteBatch _moonBatch;
    SpriteBatch _starBatch;      // static: every catalog star, uploaded once
    StarCatalog::Catalog _stars;
    bool _starsLoaded = false;
    bool _starsTried = false;
    int _starLimit = 5000;
    std::vector<StarCatalog::Constellation> _constellations;
    bool _constellationsTried = false;
    bool _constellationsVisible = true;
    bool _coreConstellationsOnly = false;
    GlVertexArray _lineVao;
    GlBuffer _lineVbo;
    int _lineVertexCount = 0;
    int _lineSegmentCount = 0;
    int _linesBuiltForLimit = -1;
    bool _linesBuiltCoreOnly = false;
    std::vector<SpriteInstance> _scratch;
};

#endif // SOLARSYSTEM_OBSERVESKY_H
