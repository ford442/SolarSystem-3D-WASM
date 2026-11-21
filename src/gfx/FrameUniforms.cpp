#include "FrameUniforms.h"

void FrameUniformBuffer::Upload(std::size_t view, const FrameUniformData& data) {
    if (view >= kMaxViews) {
        return;
    }
    GlBuffer& buffer = _buffers[view];
    if (!buffer) {
        buffer = GlBuffer::Create();
    }
    glBindBuffer(GL_UNIFORM_BUFFER, buffer.get());
    // Orphan + refill: the previous frame's draws may still read the old contents.
    glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUniformData), &data, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void FrameUniformBuffer::Bind(std::size_t view) const {
    if (view < kMaxViews && _buffers[view]) {
        glBindBufferBase(GL_UNIFORM_BUFFER, kBinding, _buffers[view].get());
    }
}
