// Space weather, Renderer's side (docs/ARCHITECTURE.md §13): the MagnetosphereUBO, the
// solar-wind particles (render/SolarWindRenderer), the auroral shells, and which photosphere
// program draws the Sun. Everything it draws comes from SceneSnapshot::spaceWeather, which
// Application::UpdateSpaceWeather fills from astro/SpaceWeather; nothing here runs the model.
#include "Renderer.h"
#include "ApplicationTypes.h"
#include "FrameContext.h"
#include "Solar_System/AuroraShell.h"
#include "Solar_System/BodyCatalog.h"
#include "Solar_System/Star.h"
#include "gfx/GlPlatform.h"
#include "gfx/Shader.h"
#include "gfx/SpaceWeatherUniforms.h"
#include "render/MagneticFieldBloom.h"
#include "render/SolarWindRenderer.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

namespace {
ShaderSpec SpaceWeatherProgram(const char* vertex, const char* fragment, std::vector<std::string> defines = {}) {
    return ShaderSpec(std::string("resource/shaders/") + vertex, std::string("resource/shaders/") + fragment,
                      std::move(defines));
}

const ShaderSpec& SolarWindUpdateSpec() {
    static const ShaderSpec spec = [] {
        ShaderSpec s = SpaceWeatherProgram("solarWindUpdate.vs", "solarWindUpdate.fs");
        s.feedbackVaryings = {"vState0", "vState1"};
        return s;
    }();
    return spec;
}

// Steps through the layer per fragment, by aurora mode (the static ring takes one sample).
constexpr int kAuroraSteps[] = {1, 6, 14};
// Point size (px) and brightness of the particles, by tier: fewer, larger and brighter on Medium.
constexpr float kFullPointSize = 2.0f;
constexpr float kMediumPointSize = 2.6f;
constexpr float kFullBrightness = 0.55f;
constexpr float kMediumBrightness = 0.85f;
constexpr int kAuroraBloomPasses = 2;
constexpr float kAuroraBloomIntensity = 0.6f;

glm::vec4 Pack(const glm::vec3& xyz, float w) {
    return glm::vec4(xyz, w);
}
} // namespace

Shader* Renderer::AuroraProgram(int auroraMode) {
    const std::size_t mode = static_cast<std::size_t>(std::clamp(auroraMode, 0, 2));
    if (!_auroraShaders[mode]) {
        static const ShaderSpec specs[] = {
            SpaceWeatherProgram("aurora.vs", "aurora.fs"),
            SpaceWeatherProgram("aurora.vs", "aurora.fs", {"AURORA_ANIMATED"}),
            SpaceWeatherProgram("aurora.vs", "aurora.fs", {"AURORA_CURTAINS"}),
        };
        _auroraShaders[mode] = _shaders.GetIfReady(specs[mode]);
    }
    return _auroraShaders[mode];
}

Shader* Renderer::StarProgram(int photosphereMode) {
    if (!_spriteStarShader) {
        _spriteStarShader = _mainStarShader; // Init built the sprite as the star program
    }
    const int mode = std::clamp(photosphereMode, 0, 2);
    if (mode == 0) {
        return _spriteStarShader;
    }
    const std::size_t slot = static_cast<std::size_t>(mode);
    if (!_starShaders[slot]) {
        static const ShaderSpec granulation = SpaceWeatherProgram("star.vs", "star.fs", {"PHOTOSPHERE"});
        static const ShaderSpec spots = SpaceWeatherProgram("star.vs", "star.fs", {"PHOTOSPHERE", "SUNSPOTS"});
        _starShaders[slot] = _shaders.GetIfReady(mode == 1 ? granulation : spots);
    }
    // Until it has compiled, the sprite.
    return _starShaders[slot] ? _starShaders[slot] : _spriteStarShader;
}

void Renderer::UpdateSpaceWeather(const FrameContext& ctx) {
    static_assert(kSolarWindGroupCount == SolarWindRenderer::kMaxGroups, "one flag per particle group");
    const SpaceWeatherFrame* weather = ctx.scene.spaceWeather;

    // The photosphere follows the tier; RenderStarEffects sets the rest of its uniforms.
    if (Shader* star = StarProgram(weather ? weather->photosphereMode : 0)) {
        _mainStarShader = star;
        if (weather && weather->photosphereMode > 0 && star != _spriteStarShader) {
            star->Use();
            star->SetVec4("uSpotBands", weather->spotBandsRad);
            star->SetVec2("uSpotCoverage", weather->spotCoverage);
            star->SetFloat("uSpotDays", weather->spotDays);
        }
    }

    // Only the particle programs read the MagnetosphereUBO: with the wind off (or on Low, which
    // has no particles) nothing is packed or uploaded, so the tier pays nothing for it.
    if (!weather || weather->particles == 0) {
        _solarWindGroups.fill(false);
        if (_solarWind) {
            _solarWind->SetParticleCount(0);
        }
        return;
    }

    SpaceWeatherUniformData data;
    data.sun = Pack(ctx.scene.sun ? ctx.scene.sun->GetPosition() : glm::vec3(0.0f),
                    ctx.scene.sun ? ctx.scene.sun->GetSceneRadius() : 1.0f);
    data.wind = glm::vec4(weather->auPerSecond, weather->edgeAu, weather->sunRotation, weather->visualClock);
    data.step = glm::vec4(weather->visualDt, weather->beltHalfWidthRad, weather->spawnAu, weather->cyclePhase);
    const bool full = weather->particles >= 100000;
    const float pixelScale = std::max(ctx.sim.backingStoreScale, 1.0f);
    data.look =
        glm::vec4((full ? kFullPointSize : kMediumPointSize) * pixelScale, full ? kFullBrightness : kMediumBrightness,
                  weather->edgeAu, static_cast<float>(weather->frameCounter % 16777216u));
    const int knots = std::min(static_cast<int>(weather->knots.size()), kSpaceWeatherMaxKnots);
    for (int i = 0; i < knots; ++i) {
        data.knotAu[i / 4][i % 4] = weather->knots[static_cast<std::size_t>(i)].x;
        data.knotScene[i / 4][i % 4] = weather->knots[static_cast<std::size_t>(i)].y;
    }
    int bodySlots = 0;
    _solarWindGroups.fill(false);
    _solarWindGroups[0] = weather->backgroundActive;
    for (const SpaceWeatherFrame::Body& b : weather->bodies) {
        if (b.slot < 0 || b.slot >= kSpaceWeatherMaxBodies) {
            continue;
        }
        SpaceWeatherBodyData& out = data.bodies[b.slot];
        out.helio = Pack(glm::vec3(b.heliocentric), b.radius);
        out.render = Pack(b.render, b.groupActive ? 1.0f : 0.0f);
        out.axis = Pack(b.pole, b.standoff);
        out.sunward = Pack(b.sunward, b.flaring);
        out.state = glm::vec4(b.kp / 9.0f, b.standoff / std::max(b.quietStandoff, 1e-3f), b.windRatio, b.cmeStrength);
        out.oval = b.ovalRad;
        out.colorLow = Pack(b.colorLow, b.brightness);
        out.colorHigh = Pack(b.colorHigh, b.quietStandoff);
        out.colorEdge = Pack(b.colorEdge, b.densityRatio);
        out.shell = glm::vec4(b.shellBottom, b.shellTop, b.kp, static_cast<float>(weather->auroraMode));
        bodySlots = std::max(bodySlots, b.slot + 1);
        _solarWindGroups[static_cast<std::size_t>(b.slot) + 1] = b.groupActive;
    }
    const int cmes = std::min(static_cast<int>(weather->cmes.size()), kSpaceWeatherMaxCmes);
    for (int i = 0; i < cmes; ++i) {
        const SpaceWeatherFrame::Cme& c = weather->cmes[static_cast<std::size_t>(i)];
        data.cmes[i].axis = Pack(c.direction, c.frontAu);
        data.cmes[i].shape = glm::vec4(c.cosEdge, c.cosCore, c.speedRatio, c.brightness);
    }
    data.counts = glm::ivec4(bodySlots, cmes, knots, 0);

    if (!_spaceWeatherUbo) {
        _spaceWeatherUbo = std::make_unique<SpaceWeatherUniformBuffer>();
    }
    _spaceWeatherUbo->Upload(data);
    _spaceWeatherUbo->Bind();

    if (!_solarWind) {
        _solarWind = std::make_unique<SolarWindRenderer>();
    }
    _solarWind->SetParticleCount(weather->particles);
    if (!_solarWindUpdateShader) {
        _solarWindUpdateShader = _shaders.GetIfReady(SolarWindUpdateSpec());
    }
    if (!_solarWindDrawShader) {
        _solarWindDrawShader = _shaders.GetIfReady(SpaceWeatherProgram("solarWind.vs", "solarWind.fs"));
    }
    if (_solarWindUpdateShader && _solarWindDrawShader) {
        // With the clock stopped this still runs, so a group that comes into view reseeds.
        _solarWind->Update(*_solarWindUpdateShader, _solarWindGroups);
    }
}

void Renderer::RenderSolarWind(const FrameContext& ctx) const {
    if (!ctx.scene.spaceWeather || !_solarWind || !_solarWindDrawShader || !_spaceWeatherUbo ||
        _solarWind->ParticleCount() == 0) {
        return;
    }
    _spaceWeatherUbo->Bind();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
#ifndef __EMSCRIPTEN__
    glEnable(GL_PROGRAM_POINT_SIZE); // always on in GLES 3 / WebGL 2
#endif
    _solarWind->Draw(*_solarWindDrawShader, _solarWindGroups);
#ifndef __EMSCRIPTEN__
    glDisable(GL_PROGRAM_POINT_SIZE);
#endif
    glDepthMask(GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
}

void Renderer::RenderAurora(const FrameContext& ctx, const RenderableSceneComponent& component) {
    const AuroraShell* shell = component.aurora.get();
    const SpaceWeatherFrame* weather = ctx.scene.spaceWeather;
    if (!shell || !shell->IsVisible() || !weather) {
        return;
    }
    const SpaceWeatherFrame::Body* body = weather->Find(static_cast<int>(shell->Body()));
    Shader* program = AuroraProgram(weather->auroraMode);
    if (!body || !program) {
        return;
    }
    const BodyCatalog::AuroraRow& row = shell->Row();
    const glm::mat3 toSphere = shell->ToSphere();
    const bool inside = shell->Contains(ctx.view.eyePosition);

    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    if (inside) {
        // The far side, and no depth test: the march itself stops at the ground.
        glFrontFace(GL_CW);
        glDisable(GL_DEPTH_TEST);
    }

    program->Use();
    program->SetVec3("uCenter", shell->GetPlanet().GetPosition());
    program->SetMat3("uToSphere", toSphere);
    program->SetVec3("uPole", glm::normalize(toSphere * body->pole));
    program->SetVec3("uSunDir", glm::normalize(toSphere * body->sunward));
    program->SetVec4("uOval", body->ovalRad);
    program->SetVec2("uShell", glm::vec2(shell->BottomRatio(), shell->TopRatio()));
    program->SetFloat("uKp", body->kp);
    program->SetFloat("uBrightness", row.brightness);
    program->SetVec3("uColorLow", glm::vec3(row.colorLow.r, row.colorLow.g, row.colorLow.b));
    program->SetVec3("uColorHigh", glm::vec3(row.colorHigh.r, row.colorHigh.g, row.colorHigh.b));
    program->SetVec3("uColorEdge", glm::vec3(row.colorEdge.r, row.colorEdge.g, row.colorEdge.b));
    program->SetInt("uSteps", kAuroraSteps[std::clamp(weather->auroraMode, 0, 2)]);
    shell->Draw(*program);

    if (inside) {
        glFrontFace(GL_CCW);
        glEnable(GL_DEPTH_TEST);
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

void Renderer::RenderAuroraBloom(const FrameContext& ctx) {
    const SpaceWeatherFrame* weather = ctx.scene.spaceWeather;
    if (!weather || weather->auroraMode < 2 || ctx.xr || !_magneticFieldBloom || !ctx.scene.components) {
        return;
    }
    const bool any = std::any_of(ctx.scene.components->begin(), ctx.scene.components->end(),
                                 [](const RenderableSceneComponent& c) { return c.aurora && c.aurora->IsVisible(); });
    if (!any) {
        return;
    }
    _magneticFieldBloom->Resize(ctx.display.x, ctx.display.y);
    if (!_magneticFieldBloom->IsEnabled()) {
        return;
    }
    _magneticFieldBloom->BeginCapture();
    for (const RenderableSceneComponent& component : *ctx.scene.components) {
        RenderAurora(ctx, component);
    }
    _magneticFieldBloom->BlurAndComposite(kAuroraBloomPasses, kAuroraBloomIntensity);
}
