#include "SolarWindRenderer.h"
#include "../gfx/GlHandle.h"
#include "../gfx/GlPlatform.h"
#include "../gfx/Shader.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
constexpr GLsizei kStride = static_cast<GLsizei>(SolarWindRenderer::kFloatsPerParticle * sizeof(float));
// The background gets this share of the budget; the rest is split among the magnetospheres.
constexpr float kBackgroundShare = 0.45f;
} // namespace

void SolarWindRenderer::SetParticleCount(int particles) {
    particles = particles > 0 ? particles : 0;
    if (particles == _particles) {
        return;
    }
    _particles = particles;
    _wasActive.fill(false);
    _current = 0;
    if (particles == 0) {
        _buffers = {};
        _vaos = {};
        _groups = {};
        return;
    }

    const int background = static_cast<int>(static_cast<float>(particles) * kBackgroundShare);
    const int perBody = (particles - background) / static_cast<int>(kMaxGroups - 1);
    int first = 0;
    for (std::size_t g = 0; g < kMaxGroups; ++g) {
        const int count = g == 0 ? background : perBody;
        _groups[g] = Group{first, count};
        first += count;
    }

    // Every particle starts empty (mode -1): its group's first update reseeds it.
    std::vector<float> initial(static_cast<std::size_t>(first) * kFloatsPerParticle, 0.0f);
    for (std::size_t i = 0; i < static_cast<std::size_t>(first); ++i) {
        initial[i * kFloatsPerParticle + 4] = -1.0f;
    }
    for (std::size_t b = 0; b < 2; ++b) {
        _buffers[b] = GlBuffer::Create();
        _vaos[b] = GlVertexArray::Create();
        glBindVertexArray(_vaos[b].get());
        glBindBuffer(GL_ARRAY_BUFFER, _buffers[b].get());
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(initial.size() * sizeof(float)), initial.data(),
                     GL_DYNAMIC_COPY);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, kStride, nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, kStride,
                              reinterpret_cast<const void*>(static_cast<std::uintptr_t>(4 * sizeof(float))));
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void SolarWindRenderer::Update(const Shader& update, const std::array<bool, kMaxGroups>& active) {
    if (_particles == 0) {
        return;
    }
    bool any = false;
    for (const bool a : active) {
        any = any || a;
    }
    if (!any) {
        _wasActive.fill(false);
        return;
    }
    const int source = _current;
    const int target = 1 - _current;

    update.Use();
    glEnable(GL_RASTERIZER_DISCARD);
    glBindVertexArray(_vaos[static_cast<std::size_t>(source)].get());
    for (std::size_t g = 0; g < kMaxGroups; ++g) {
        if (!active[g] || _groups[g].count == 0) {
            continue;
        }
        update.SetInt("uGroup", GroupUniform(g));
        update.SetFloat("uReset", _wasActive[g] ? 0.0f : 1.0f);
        // Feedback writes from the start of the bound range: bind this group's slice.
        glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, _buffers[static_cast<std::size_t>(target)].get(),
                          static_cast<GLintptr>(_groups[g].first) * kStride,
                          static_cast<GLsizeiptr>(_groups[g].count) * kStride);
        glBeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, _groups[g].first, _groups[g].count);
        glEndTransformFeedback();
    }
    // WebGL 2 refuses a draw that reads a buffer still bound for feedback.
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
    glBindVertexArray(0);
    glDisable(GL_RASTERIZER_DISCARD);
    _current = target;
    _wasActive = active;
}

void SolarWindRenderer::Draw(const Shader& draw, const std::array<bool, kMaxGroups>& active) const {
    if (_particles == 0) {
        return;
    }
    draw.Use();
    glBindVertexArray(_vaos[static_cast<std::size_t>(_current)].get());
    for (std::size_t g = 0; g < kMaxGroups; ++g) {
        // Only what this frame's update wrote: a group that just woke has not been seeded
        // in this buffer.
        if (!active[g] || !_wasActive[g] || _groups[g].count == 0) {
            continue;
        }
        draw.SetInt("uGroup", GroupUniform(g));
        glDrawArrays(GL_POINTS, _groups[g].first, _groups[g].count);
    }
    glBindVertexArray(0);
}
