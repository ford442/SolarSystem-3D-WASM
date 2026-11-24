#include "SpaceWeatherUniforms.h"
#include "GlPlatform.h"
#include "rhi/WgslBindings.h"

// The WebGPU backend binds MagnetosphereUBO at the same number (gfx/rhi/WgslBindings.h).
static_assert(rhi::wgsl::UniformBlockBinding("MagnetosphereUBO") == SpaceWeatherUniformBuffer::kBinding);

void SpaceWeatherUniformBuffer::Upload(const SpaceWeatherUniformData& data) {
    if (!_buffer) {
        _buffer = GlBuffer::Create();
    }
    glBindBuffer(GL_UNIFORM_BUFFER, _buffer.get());
    // Orphan + refill, like FrameUniformBuffer: last frame's draws may still be reading it.
    glBufferData(GL_UNIFORM_BUFFER, sizeof(SpaceWeatherUniformData), &data, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void SpaceWeatherUniformBuffer::Bind() const {
    if (_buffer) {
        glBindBufferBase(GL_UNIFORM_BUFFER, kBinding, _buffer.get());
    }
}
