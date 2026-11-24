#include "RenderPassPlanner.h"
#include "RhiTypes.h"
#include <cstdint>

namespace rhi {

void RenderPassPlanner::BeginGraphPass(int timerSlot) {
    FlushPendingClear();
    Close();
    _timerSlot = timerSlot;
}

void RenderPassPlanner::EndGraphPass() {
    FlushPendingClear();
    Close();
    _timerSlot = -1;
}

void RenderPassPlanner::BindFramebuffer(FramebufferId framebuffer) {
    if (framebuffer == _framebuffer) {
        return; // rebinding the bound framebuffer changes nothing in GL either
    }
    FlushPendingClear();
    Close();
    _framebuffer = framebuffer;
}

void RenderPassPlanner::SetViewport(const Viewport& viewport) {
    if (viewport == _viewport) {
        return;
    }
    _viewport = viewport;
    if (_open) {
        _sink->SetPassViewport(viewport);
    }
}

void RenderPassPlanner::Clear(ClearFlags flags, const ClearValue& value, bool depthWritable) {
    const auto bits = static_cast<std::uint8_t>(flags);
    const bool color = (bits & static_cast<std::uint8_t>(ClearFlags::Color)) != 0;
    const bool depth = depthWritable && (bits & static_cast<std::uint8_t>(ClearFlags::Depth)) != 0;
    if (!color && !depth) {
        return;
    }
    // Drawn content is in the open pass; the clear must land after it, so it starts the next one.
    Close();
    if (color) {
        _pendingColor = true;
        _pendingValue.color = value.color;
    }
    if (depth) {
        _pendingDepth = true;
        _pendingValue.depth = value.depth;
    }
}

void RenderPassPlanner::BeforeDraw() {
    if (!_open) {
        Open();
    }
}

void RenderPassPlanner::Open() {
    RenderPassBegin begin;
    begin.framebuffer = _framebuffer;
    begin.colorLoad = _pendingColor ? LoadOp::Clear : LoadOp::Load;
    begin.depthLoad = _pendingDepth ? LoadOp::Clear : LoadOp::Load;
    begin.clear = _pendingValue;
    begin.viewport = _viewport;
    begin.timerSlot = _timerSlot;
    _pendingColor = false;
    _pendingDepth = false;
    _open = true;
    ++_renderPasses;
    _sink->BeginRenderPass(begin);
}

void RenderPassPlanner::Close() {
    if (_open) {
        _open = false;
        _sink->EndRenderPass();
    }
}

void RenderPassPlanner::FlushPendingClear() {
    if (_open || (!_pendingColor && !_pendingDepth)) {
        return;
    }
    Open();
    Close();
}

} // namespace rhi
