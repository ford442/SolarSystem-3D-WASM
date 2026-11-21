#pragma once

#include "../gfx/GlHandle.h"
#include <array>
#include <cstddef>

class Shader;

/**
 * The solar wind as a GPU particle stream (docs/ARCHITECTURE.md §13): transform feedback in
 * WebGL 2 / GL 4.6 core, no compute. Two buffers of particles ping-pong — each frame the
 * update program (solarWindUpdate.vs) reads one and writes the other with the rasterizer off —
 * and the draw program (solarWind.vs) adds the result onto the scene as points.
 *
 * The buffers are cut into groups, each a contiguous range updated and drawn on its own:
 * group 0 is the heliospheric background, born at the Sun; groups 1… are the wind meeting
 * each magnetosphere (SpaceWeatherFrame::bodies, in that order), in the body's own frame. A
 * group whose part of the scene is culled or too small to see is neither updated nor drawn,
 * so the stream costs nothing when nothing of it is on screen; it reseeds to a steady state
 * when it comes back.
 *
 * The MagnetosphereUBO (SpaceWeatherUniformBuffer) must be bound for both programs.
 */
class SolarWindRenderer {
public:
    static constexpr std::size_t kMaxGroups = 7;         // background + six magnetospheres
    static constexpr std::size_t kFloatsPerParticle = 8; // two vec4s

    SolarWindRenderer() = default;
    SolarWindRenderer(const SolarWindRenderer&) = delete;
    SolarWindRenderer& operator=(const SolarWindRenderer&) = delete;

    /** Reallocate for `particles` (0 frees the buffers). A no-op when the count is unchanged. */
    void SetParticleCount(int particles);
    int ParticleCount() const { return _particles; }

    /**
     * Advance the groups `active` marks by the UBO's dt; groups that were not active on the
     * last update reseed. `bodyGroups` is how many body groups exist (≤ kMaxGroups − 1).
     */
    void Update(const Shader& update, const std::array<bool, kMaxGroups>& active);
    /** Draw the groups `active` marks (additive points; the caller sets blend/depth state). */
    void Draw(const Shader& draw, const std::array<bool, kMaxGroups>& active) const;
    /** Every group reseeds on its next update (a new star system, a scale-mode switch). */
    void ResetAll() { _wasActive.fill(false); }
    /** The buffer holding this frame's particles, and group `g`'s first particle and count (tests). */
    GLuint CurrentBuffer() const { return _buffers[static_cast<std::size_t>(_current)].get(); }
    int GroupFirst(std::size_t g) const { return _groups[g].first; }
    int GroupCount(std::size_t g) const { return _groups[g].count; }

private:
    struct Group {
        int first = 0;
        int count = 0;
    };
    /** The update/draw `uGroup` of group `g`: -1 background, else the body slot. */
    static int GroupUniform(std::size_t g) { return static_cast<int>(g) - 1; }

    int _particles = 0;
    std::array<Group, kMaxGroups> _groups{};
    std::array<GlBuffer, 2> _buffers;
    std::array<GlVertexArray, 2> _vaos;
    int _current = 0; // the buffer holding this frame's particles
    std::array<bool, kMaxGroups> _wasActive{};
};
