#include "GlCommandList.h"
#include "RhiTypes.h"
#include "core/FrameProfile.h"
#include "gfx/GlPlatform.h"
#include "gfx/GpuTimer.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rhi {

namespace {

GLenum ToGl(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return GL_ZERO;
        case BlendFactor::One: return GL_ONE;
        case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
    }
    return GL_ONE;
}

GLenum ToGl(CompareOp op) {
    switch (op) {
        case CompareOp::Less: return GL_LESS;
        case CompareOp::LessEqual: return GL_LEQUAL;
        case CompareOp::Always: return GL_ALWAYS;
    }
    return GL_LESS;
}

void Toggle(GLenum capability, bool enabled) {
    if (enabled) {
        glEnable(capability);
    } else {
        glDisable(capability);
    }
}

} // namespace

void GlCommandList::SetProfiling(GpuTimer* gpu, FrameProfile::PassTimings* passes, FrameProfile::CpuStages* stages) {
    _gpu = gpu;
    _passes = passes;
    _stages = stages;
}

void GlCommandList::BeginPass(const PassInfo& pass) {
    _open = pass;
    _inPass = true;
#ifndef __EMSCRIPTEN__
    if (_debugGroups && glPushDebugGroup) {
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, static_cast<GLsizei>(std::strlen(pass.name)), pass.name);
    }
#endif
    if (_gpu && pass.timerSlot >= 0) {
        _gpu->Begin(FrameProfile::kStageCount + static_cast<std::size_t>(pass.timerSlot));
    }
    _started = FrameProfile::CpuStages::Clock::now();
}

void GlCommandList::EndPass() {
    if (!_inPass) {
        return;
    }
    const auto elapsed = FrameProfile::CpuStages::Clock::now() - _started;
    if (_gpu && _open.timerSlot >= 0) {
        _gpu->End(FrameProfile::kStageCount + static_cast<std::size_t>(_open.timerSlot));
    }
    if (_passes) {
        _passes->Add(_open.timerSlot, elapsed);
    }
    if (_stages && _open.stage >= 0 && _open.stage < static_cast<int>(FrameProfile::kStageCount)) {
        _stages->Add(static_cast<FrameProfile::Stage>(_open.stage), elapsed);
    }
#ifndef __EMSCRIPTEN__
    if (_debugGroups && glPopDebugGroup) {
        glPopDebugGroup();
    }
#endif
    _inPass = false;
}

void GlCommandList::SetRenderTarget(const RenderTarget& target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(target.x, target.y, target.width, target.height);
}

void GlCommandList::BindFramebuffer(FramebufferId framebuffer) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
}

void GlCommandList::SetViewport(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    glViewport(x, y, width, height);
}

void GlCommandList::Clear(ClearFlags flags) {
    const auto bits = static_cast<std::uint8_t>(flags);
    GLbitfield mask = 0;
    if (bits & static_cast<std::uint8_t>(ClearFlags::Color)) {
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (bits & static_cast<std::uint8_t>(ClearFlags::Depth)) {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (mask) {
        glClear(mask);
    }
}

void GlCommandList::SetPipelineState(const PipelineState& state) {
    if (state.blend) {
        Toggle(GL_BLEND, *state.blend);
    }
    if (state.blendFunc) {
        glBlendFunc(ToGl(state.blendFunc->src), ToGl(state.blendFunc->dst));
    }
    if (state.depthTest) {
        Toggle(GL_DEPTH_TEST, *state.depthTest);
    }
    if (state.depthWrite) {
        glDepthMask(*state.depthWrite ? GL_TRUE : GL_FALSE);
    }
    if (state.depthFunc) {
        glDepthFunc(ToGl(*state.depthFunc));
    }
    if (state.cullFace) {
        Toggle(GL_CULL_FACE, *state.cullFace);
    }
    if (state.frontFace) {
        glFrontFace(*state.frontFace == FrontFace::Clockwise ? GL_CW : GL_CCW);
    }
    if (state.polygonOffset) {
        Toggle(GL_POLYGON_OFFSET_FILL, *state.polygonOffset);
        if (*state.polygonOffset) {
            glPolygonOffset(state.polygonOffsetFactor, state.polygonOffsetUnits);
        }
    }
    if (state.rasterizerDiscard) {
        Toggle(GL_RASTERIZER_DISCARD, *state.rasterizerDiscard);
    }
#ifndef __EMSCRIPTEN__
    if (state.programPointSize) {
        Toggle(GL_PROGRAM_POINT_SIZE, *state.programPointSize); // always on in GLES 3 / WebGL 2
    }
#endif
}

void GlCommandList::SetBindGroup(const BindGroup& group) {
    for (std::size_t i = 0; i < group.textureCount; ++i) {
        glBindTextureUnit(group.textures[i].unit, group.textures[i].texture);
    }
    for (std::size_t i = 0; i < group.uniformBufferCount; ++i) {
        glBindBufferBase(GL_UNIFORM_BUFFER, group.uniformBuffers[i].binding, group.uniformBuffers[i].buffer);
    }
}

void GlCommandList::DrawFullscreenTriangle(ObjectId emptyVertexArray) {
    glBindVertexArray(emptyVertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace rhi
