#include "PipelineTracker.h"
#include "RhiTypes.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace rhi {

namespace {

/** boost::hash_combine's mix: enough for a few hundred pipelines per frame. */
void Combine(std::size_t& seed, std::uint64_t value) {
    seed ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

std::uint32_t FloatBits(float value) {
    std::uint32_t bits = 0;
    static_assert(sizeof bits == sizeof value);
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

} // namespace

ResolvedPipelineState ResolvedPipelineState::ApplicationBaseline() {
    ResolvedPipelineState state;
    state.depthTest = true; // glEnable(GL_DEPTH_TEST)
    state.cullFace = true;  // glEnable(GL_CULL_FACE); glCullFace(GL_BACK) is GL's default anyway
    return state;
}

bool ResolvedPipelineState::operator==(const ResolvedPipelineState& other) const {
    return blend == other.blend && blendFunc == other.blendFunc && depthTest == other.depthTest &&
           depthWrite == other.depthWrite && depthFunc == other.depthFunc && cullFace == other.cullFace &&
           frontFace == other.frontFace && polygonOffset == other.polygonOffset &&
           polygonOffsetFactor == other.polygonOffsetFactor && polygonOffsetUnits == other.polygonOffsetUnits &&
           rasterizerDiscard == other.rasterizerDiscard;
}

void PipelineTracker::Apply(const PipelineState& delta) {
    const ResolvedPipelineState before = _current;
    if (delta.blend) {
        _current.blend = *delta.blend;
    }
    if (delta.blendFunc) {
        _current.blendFunc = *delta.blendFunc;
    }
    if (delta.depthTest) {
        _current.depthTest = *delta.depthTest;
    }
    if (delta.depthWrite) {
        _current.depthWrite = *delta.depthWrite;
    }
    if (delta.depthFunc) {
        _current.depthFunc = *delta.depthFunc;
    }
    if (delta.cullFace) {
        _current.cullFace = *delta.cullFace;
    }
    if (delta.frontFace) {
        _current.frontFace = *delta.frontFace;
    }
    if (delta.polygonOffset) {
        _current.polygonOffset = *delta.polygonOffset;
        // GlCommandList calls glPolygonOffset only when it turns the offset on.
        if (*delta.polygonOffset) {
            _current.polygonOffsetFactor = delta.polygonOffsetFactor;
            _current.polygonOffsetUnits = delta.polygonOffsetUnits;
        }
    }
    if (delta.rasterizerDiscard) {
        _current.rasterizerDiscard = *delta.rasterizerDiscard;
    }
    // programPointSize is not pipeline state anywhere but desktop GL: WebGL 2 and WebGPU
    // always take the point size from the vertex stage.
    if (_current != before) {
        ++_revision;
    }
}

void PipelineTracker::Reset(const ResolvedPipelineState& state) {
    if (_current != state) {
        ++_revision;
    }
    _current = state;
}

bool WebGpuFixedFunction::operator==(const WebGpuFixedFunction& other) const {
    return blend == other.blend && srcFactor == other.srcFactor && dstFactor == other.dstFactor &&
           depthCompare == other.depthCompare && depthWrite == other.depthWrite && depthBias == other.depthBias &&
           depthBiasSlopeScale == other.depthBiasSlopeScale && cullBack == other.cullBack &&
           frontFace == other.frontFace && discard == other.discard;
}

WebGpuFixedFunction ToWebGpuFixedFunction(const ResolvedPipelineState& state, bool flipY) {
    WebGpuFixedFunction out;
    out.blend = state.blend;
    if (state.blend) {
        out.srcFactor = state.blendFunc.src;
        out.dstFactor = state.blendFunc.dst;
    }
    if (state.depthTest) {
        out.depthCompare = state.depthFunc;
        out.depthWrite = state.depthWrite;
    }
    if (state.polygonOffset) {
        out.depthBias = static_cast<std::int32_t>(std::lround(state.polygonOffsetUnits));
        out.depthBiasSlopeScale = state.polygonOffsetFactor;
    }
    out.cullBack = state.cullFace;
    out.frontFace = state.frontFace;
    if (flipY) {
        out.frontFace =
            state.frontFace == FrontFace::CounterClockwise ? FrontFace::Clockwise : FrontFace::CounterClockwise;
    }
    out.discard = state.rasterizerDiscard;
    return out;
}

bool PipelineKey::operator==(const PipelineKey& other) const {
    return program == other.program && vertexLayout == other.vertexLayout && topology == other.topology &&
           colorFormat == other.colorFormat && depthFormat == other.depthFormat && fixedFunction == other.fixedFunction;
}

std::size_t PipelineKey::Hash() const {
    std::size_t seed = 0;
    Combine(seed, program);
    Combine(seed, vertexLayout);
    Combine(seed, static_cast<std::uint64_t>(topology));
    Combine(seed, static_cast<std::uint64_t>(colorFormat));
    Combine(seed, static_cast<std::uint64_t>(depthFormat));
    const WebGpuFixedFunction& ff = fixedFunction;
    const std::uint64_t flags =
        (ff.blend ? 1u : 0u) | (ff.depthWrite ? 2u : 0u) | (ff.cullBack ? 4u : 0u) | (ff.discard ? 8u : 0u) |
        (static_cast<std::uint64_t>(ff.srcFactor) << 8) | (static_cast<std::uint64_t>(ff.dstFactor) << 16) |
        (static_cast<std::uint64_t>(ff.depthCompare) << 24) | (static_cast<std::uint64_t>(ff.frontFace) << 32);
    Combine(seed, flags);
    Combine(seed, static_cast<std::uint32_t>(ff.depthBias));
    Combine(seed, FloatBits(ff.depthBiasSlopeScale));
    return seed;
}

} // namespace rhi
