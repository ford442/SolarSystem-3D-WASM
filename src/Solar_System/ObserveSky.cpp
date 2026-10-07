#include "ObserveSky.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

constexpr float kKindPoint = 0.0f;
constexpr float kKindSun = 1.0f;
constexpr float kKindMoon = 2.0f;
constexpr float kKindStar = 3.0f;

constexpr int kMoonTextureUnit = 1;

constexpr const char* kStarCatalogPath = "resource/sky/bright_stars.json";

// The Sun's quad reaches this many solar radii so the halo has room to fade out.
constexpr float kSunQuadRadii = 6.0f;
constexpr float kMoonQuadMargin = 1.03f;

glm::vec3 planetColor(int slot) {
    switch (slot) {
        case Observer::kSkyMercury: return {0.92f, 0.86f, 0.78f};
        case Observer::kSkyVenus: return {1.00f, 0.97f, 0.90f};
        case Observer::kSkyMars: return {1.00f, 0.58f, 0.42f};
        case Observer::kSkyJupiter: return {1.00f, 0.93f, 0.80f};
        case Observer::kSkySaturn: return {1.00f, 0.90f, 0.70f};
        default: return {1.0f, 1.0f, 1.0f};
    }
}

} // namespace

ObserveSky::ObserveSky() {
    _skyShader = std::make_unique<Shader>("resource/shaders/observeSky.vs", "resource/shaders/observeSky.fs");
    _spriteShader = std::make_unique<Shader>("resource/shaders/observeSprite.vs", "resource/shaders/observeSprite.fs");

    // A core-profile draw needs a bound VAO even when every vertex comes from gl_VertexID.
    glGenVertexArrays(1, &_emptyVao);

    const float quad[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};
    glGenBuffers(1, &_quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, _quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    InitBatch(_additiveBatch, 64);
    InitBatch(_moonBatch, 1);
}

ObserveSky::~ObserveSky() {
    for (SpriteBatch* batch : {&_additiveBatch, &_moonBatch, &_starBatch}) {
        if (batch->vbo) glDeleteBuffers(1, &batch->vbo);
        if (batch->vao) glDeleteVertexArrays(1, &batch->vao);
    }
    if (_quadVbo) glDeleteBuffers(1, &_quadVbo);
    if (_emptyVao) glDeleteVertexArrays(1, &_emptyVao);
}

void ObserveSky::InitBatch(SpriteBatch& batch, int capacity) const {
    batch.capacity = capacity;
    batch.count = 0;
    glGenVertexArrays(1, &batch.vao);
    glGenBuffers(1, &batch.vbo);
    glBindVertexArray(batch.vao);

    glBindBuffer(GL_ARRAY_BUFFER, _quadVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity * sizeof(SpriteInstance)), nullptr,
                 GL_DYNAMIC_DRAW);
    const GLsizei stride = static_cast<GLsizei>(sizeof(SpriteInstance));
    for (int i = 0; i < 3; ++i) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(sizeof(float) * 4 * i));
        glVertexAttribDivisor(1 + i, 1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ObserveSky::UploadBatch(SpriteBatch& batch, const std::vector<SpriteInstance>& instances) const {
    batch.count = std::min(static_cast<int>(instances.size()), batch.capacity);
    if (batch.count == 0) {
        return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(batch.count * sizeof(SpriteInstance)),
                    instances.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void ObserveSky::DrawBatch(const SpriteBatch& batch, int count) const {
    if (count <= 0) {
        return;
    }
    glBindVertexArray(batch.vao);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, count);
    glBindVertexArray(0);
}

void ObserveSky::EnsureStarsLoaded() {
    if (_starsTried) {
        return;
    }
    _starsTried = true;

    std::string error;
    if (!StarCatalog::LoadFromFile(kStarCatalogPath, _stars, error)) {
        // Observe still works without stars (planets, Sun and Moon), so this is not fatal.
        std::cerr << "[Observe] star catalog unavailable (" << error << "); drawing no stars" << std::endl;
        return;
    }

    std::vector<SpriteInstance> instances;
    instances.reserve(_stars.stars.size());
    for (const StarCatalog::Star& star : _stars.stars) {
        const float ra = star.raDeg * kDegToRad;
        const float dec = star.decDeg * kDegToRad;
        float rgb[3];
        StarCatalog::ColorFromBV(star.bv, rgb);
        SpriteInstance sprite;
        sprite.posSize = glm::vec4(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec), 0.0f);
        sprite.color = glm::vec4(rgb[0], rgb[1], rgb[2], 1.0f);
        sprite.params = glm::vec4(kKindStar, star.vmag, 0.0f, 0.0f);
        instances.push_back(sprite);
    }
    InitBatch(_starBatch, static_cast<int>(instances.size()));
    UploadBatch(_starBatch, instances);
    _starsLoaded = _starBatch.count > 0;
    std::cout << "[Observe] Loaded " << _stars.stars.size() << " stars from " << kStarCatalogPath << std::endl;
}

float ObserveSky::LimitingMagnitude(double sunAltDeg) {
    // Naked-eye limit through twilight: full dark below -18 deg, Venus alone by day.
    static const struct { double alt, mag; } kTable[] = {
        {-18.0, 6.5}, {-12.0, 3.0}, {-6.0, -0.5}, {0.0, -3.0}, {6.0, -4.2}};
    constexpr int kCount = sizeof(kTable) / sizeof(kTable[0]);
    if (sunAltDeg <= kTable[0].alt) return static_cast<float>(kTable[0].mag);
    for (int i = 1; i < kCount; ++i) {
        if (sunAltDeg <= kTable[i].alt) {
            const double t = (sunAltDeg - kTable[i - 1].alt) / (kTable[i].alt - kTable[i - 1].alt);
            return static_cast<float>(kTable[i - 1].mag + t * (kTable[i].mag - kTable[i - 1].mag));
        }
    }
    return static_cast<float>(kTable[kCount - 1].mag);
}

float ObserveSky::EffectiveLimitingMagnitude(const Observer::Sky& sky) {
    const float limit = LimitingMagnitude(sky.bodies[Observer::kSkySun].altDeg);
    // In totality the sky is as dark as twilight and the brightest stars and planets show (in
    // 2017 Venus, Jupiter, Mars, Mercury and Regulus did); fade the limit up as coverage nears 1.
    const float totality = glm::smoothstep(0.97f, 1.0f, static_cast<float>(sky.sunCoverage));
    return glm::mix(limit, std::max(limit, 1.5f), totality);
}

bool ObserveSky::ProjectToPixels(const glm::vec3& dir, const Frame& frame, glm::vec2& outPixels) {
    const glm::vec4 clip = frame.projection * glm::vec4(frame.viewRot * dir, 1.0f);
    if (clip.w <= 1e-6f) {
        return false;
    }
    const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
    outPixels = glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(frame.viewportWidth),
                          (ndc.y * 0.5f + 0.5f) * static_cast<float>(frame.viewportHeight));
    return true;
}

void ObserveSky::Render(const Observer::Sky& sky, const Frame& frame, unsigned int moonTexture) {
    const Observer::SkyBody& sun = sky.bodies[Observer::kSkySun];
    const glm::vec3 sunDir(static_cast<float>(sun.dir[0]), static_cast<float>(sun.dir[1]),
                           static_cast<float>(sun.dir[2]));

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    // 1. Sky gradient and ground, one fullscreen triangle.
    glDisable(GL_BLEND);
    _skyShader->Use();
    _skyShader->SetMat3("invViewRot", glm::transpose(frame.viewRot)); // rotation: transpose = inverse
    _skyShader->SetMat4("invProjection", glm::inverse(frame.projection));
    _skyShader->SetVec3("sunDir", sunDir);
    _skyShader->SetFloat("eclipse", static_cast<float>(sky.sunCoverage));
    glBindVertexArray(_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // 2. Stars (static catalog, rotated into the horizon frame by one matrix), then planets
    // as point sprites, then the Sun — all additive.
    const float limit = EffectiveLimitingMagnitude(sky);
    EnsureStarsLoaded();
    _scratch.clear();
    for (int slot = Observer::kSkyMercury; slot < Observer::kSkyBodyCount; ++slot) {
        const Observer::SkyBody& body = sky.bodies[slot];
        const float margin = limit - static_cast<float>(body.magnitude);
        if (margin < -0.5f) {
            continue; // lost in the sky glow
        }
        const float brightness = glm::clamp(0.4f + 0.09f * margin, 0.0f, 1.0f);
        const float pixels = 2.4f + 0.45f * glm::clamp(margin, 0.0f, 10.0f);
        SpriteInstance sprite;
        sprite.posSize = glm::vec4(static_cast<float>(body.dir[0]), static_cast<float>(body.dir[1]),
                                   static_cast<float>(body.dir[2]), pixels); // half-size in pixels: planets are points
        sprite.color = glm::vec4(planetColor(slot), brightness);
        sprite.params = glm::vec4(kKindPoint, 1.0f, 0.0f, 0.0f);
        _scratch.push_back(sprite);
    }
    {
        const float radius = static_cast<float>(std::tan(sun.angRadiusDeg * kDegToRad));
        SpriteInstance sprite;
        sprite.posSize = glm::vec4(sunDir, radius * kSunQuadRadii);
        sprite.color = glm::vec4(1.0f, 0.95f, 0.85f, 1.0f);
        sprite.params = glm::vec4(kKindSun, 1.0f / kSunQuadRadii, 0.0f, 0.0f);
        _scratch.push_back(sprite);
    }
    UploadBatch(_additiveBatch, _scratch);

    _spriteShader->Use();
    _spriteShader->SetMat3("viewRot", frame.viewRot);
    _spriteShader->SetMat4("projection", frame.projection);
    _spriteShader->SetVec3("sunDir", sunDir);
    _spriteShader->SetFloat("sunTanRadius", static_cast<float>(std::tan(sun.angRadiusDeg * kDegToRad)));
    _spriteShader->SetFloat("sunCoverage", static_cast<float>(sky.sunCoverage));

    glm::mat3 starRotation(1.0f);
    {
        const Observer::Mat3 rotation = Observer::HorizonFromEquatorialJ2000(sky.julianDate, sky.site);
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                starRotation[col][row] = static_cast<float>(rotation.m[row][col]); // glm is column-major
            }
        }
    }
    _spriteShader->SetMat3("starRotation", starRotation);
    _spriteShader->SetFloat("limitMag", limit);
    _spriteShader->SetVec2("viewportPx", static_cast<float>(frame.viewportWidth), static_cast<float>(frame.viewportHeight));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    DrawBatch(_starBatch, DrawnStarCount());
    DrawBatch(_additiveBatch, _additiveBatch.count);

    // 3. The Moon, an alpha-blended lit disc so it hides what is behind it.
    const Observer::SkyBody& moon = sky.bodies[Observer::kSkyMoon];
    _scratch.clear();
    {
        const float radius = static_cast<float>(std::tan(moon.angRadiusDeg * kDegToRad));
        SpriteInstance sprite;
        sprite.posSize = glm::vec4(static_cast<float>(moon.dir[0]), static_cast<float>(moon.dir[1]),
                                   static_cast<float>(moon.dir[2]), radius * kMoonQuadMargin);
        sprite.color = glm::vec4(0.80f, 0.79f, 0.76f, 1.0f);
        sprite.params = glm::vec4(kKindMoon, 1.0f / kMoonQuadMargin, 0.0f, 0.0f);
        _scratch.push_back(sprite);
    }
    UploadBatch(_moonBatch, _scratch);
    // Orientation: surface normals come out in the horizon frame; the transpose of the body
    // axes (orthonormal) takes them to selenographic coordinates.
    glm::mat3 moonBodyFromHorizon(1.0f);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            moonBodyFromHorizon[col][row] = static_cast<float>(sky.moonBodyToHorizon.m[col][row]); // transpose
        }
    }
    _spriteShader->SetMat3("moonBodyFromHorizon", moonBodyFromHorizon);
    _spriteShader->SetBool("hasMoonTexture", moonTexture != 0);
    _spriteShader->SetInt("moonTexture", kMoonTextureUnit);
    if (moonTexture != 0) {
        glActiveTexture(GL_TEXTURE0 + kMoonTextureUnit);
        glBindTexture(GL_TEXTURE_2D, moonTexture);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    DrawBatch(_moonBatch, _moonBatch.count);
    if (moonTexture != 0) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
