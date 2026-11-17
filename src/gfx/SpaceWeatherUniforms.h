#ifndef SOLARSYSTEM_GFX_SPACEWEATHERUNIFORMS_H
#define SOLARSYSTEM_GFX_SPACEWEATHERUNIFORMS_H
#include "GlHandle.h"
#include <cstddef>
#include <glm/vec4.hpp>

/**
 * CPU mirror of `MagnetosphereUBO` (resource/shaders/common/space_weather.glsl), std140,
 * binding 1 next to FrameUBO. Every member is a vec4 / ivec4 or an array of structs of vec4s,
 * so the C++ layout is the std140 layout; the static_asserts pin it. Renderer uploads it once
 * a frame from SceneSnapshot::spaceWeather (docs/ARCHITECTURE.md §13).
 */
constexpr int kSpaceWeatherMaxBodies = 6;
constexpr int kSpaceWeatherMaxCmes = 4;
constexpr int kSpaceWeatherMaxKnots = 16;

struct SpaceWeatherBodyData {
    glm::vec4 helio{0.0f};     // xyz heliocentric scene position, w drawn radius (scene units)
    glm::vec4 render{0.0f};    // xyz render-frame centre, w 1 when its particle group runs this frame
    glm::vec4 axis{0.0f};      // xyz magnetic north (scene axes, unit), w standoff (drawn radii)
    glm::vec4 sunward{0.0f};   // xyz unit vector to the Sun, w Shue flaring exponent α
    glm::vec4 state{0.0f};     // x Kp / 9, y standoff / quiet standoff, z wind speed / 400 km/s, w CME strength
    glm::vec4 oval{0.0f};      // magnetic latitude (rad): equatorward midnight, noon; poleward midnight, noon
    glm::vec4 colorLow{0.0f};  // rgb lower curtain, a brightness
    glm::vec4 colorHigh{0.0f}; // rgb upper glow, a quiet standoff (drawn radii)
    glm::vec4 colorEdge{0.0f}; // rgb storm border, a wind density / quiet density
    glm::vec4 shell{0.0f}; // x bottom / radius, y top / radius, z Kp, w aurora mode (0 static, 1 animated, 2 curtains)
};
static_assert(sizeof(SpaceWeatherBodyData) == 160, "std140: SwBody");

struct SpaceWeatherCmeData {
    glm::vec4 axis{0.0f};  // xyz direction (scene axes), w front distance (AU)
    glm::vec4 shape{0.0f}; // x cos(flank edge), y cos(core), z front speed / 400 km/s, w brightness (0: unused)
};
static_assert(sizeof(SpaceWeatherCmeData) == 32, "std140: SwCme");

struct SpaceWeatherUniformData {
    glm::vec4 sun{0.0f}; // xyz the Sun, render frame; w the Sun's drawn radius
    glm::vec4 wind{
        0.0f}; // x AU per visual second at 400 km/s, y outer edge (AU), z Sun rotation (rad per visual s), w visual clock (s)
    glm::vec4 step{0.0f}; // x dt (visual s), y fast-wind belt half-width (rad), z spawn radius (AU), w cycle phase
    glm::vec4 look{0.0f}; // x point size (px), y brightness, z background group's far fade (AU), w frame counter
    glm::ivec4 counts{0}; // x bodies, y CMEs, z AU→scene knots, w unused
    glm::vec4 knotAu[kSpaceWeatherMaxKnots / 4]{}; // OrbitLayout's AU → scene distance map, packed
    glm::vec4 knotScene[kSpaceWeatherMaxKnots / 4]{};
    SpaceWeatherBodyData bodies[kSpaceWeatherMaxBodies]{};
    SpaceWeatherCmeData cmes[kSpaceWeatherMaxCmes]{};
};
static_assert(offsetof(SpaceWeatherUniformData, counts) == 64, "std140: MagnetosphereUBO.counts");
static_assert(offsetof(SpaceWeatherUniformData, knotAu) == 80, "std140: MagnetosphereUBO.knotAu");
static_assert(offsetof(SpaceWeatherUniformData, knotScene) == 144, "std140: MagnetosphereUBO.knotScene");
static_assert(offsetof(SpaceWeatherUniformData, bodies) == 208, "std140: MagnetosphereUBO.bodies");
static_assert(offsetof(SpaceWeatherUniformData, cmes) == 208 + 160 * kSpaceWeatherMaxBodies,
              "std140: MagnetosphereUBO.cmes");
static_assert(sizeof(SpaceWeatherUniformData) == 208 + 160 * kSpaceWeatherMaxBodies + 32 * kSpaceWeatherMaxCmes,
              "std140: MagnetosphereUBO size");

/**
 * The one MagnetosphereUBO buffer, uploaded once a frame. Which particle group a draw is
 * (and whether it reseeds) are plain uniforms on the program, so it is never re-uploaded.
 */
class SpaceWeatherUniformBuffer {
public:
    static constexpr unsigned int kBinding = 1; // MagnetosphereUBO's uniform-buffer binding point

    void Upload(const SpaceWeatherUniformData& data);
    void Bind() const;

private:
    GlBuffer _buffer;
};

#endif // SOLARSYSTEM_GFX_SPACEWEATHERUNIFORMS_H
