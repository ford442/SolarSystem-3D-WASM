#ifndef SOLARSYSTEM_GFX_RHI_PIPELINETRACKER_H
#define SOLARSYSTEM_GFX_RHI_PIPELINETRACKER_H
#include "RhiTypes.h"
#include <cstddef>
#include <cstdint>

/**
 * From PipelineState deltas to whole pipelines (docs/ARCHITECTURE.md §7.9). GL-free and
 * unit-tested (tests/test_rhi_webgpu_model.cpp).
 *
 * The GL backend applies a delta straight to the context, and every unset field keeps what
 * the previous pass left — which some passes rely on (§7.5). A backend with immutable
 * pipeline objects has no "previous state" to lean on, so it folds the same deltas here into
 * the state GL would hold at that point, and builds (or finds) the pipeline for that state.
 * The result is the GL state machine replayed exactly, so a pass ported to WebGPU sees the
 * state it saw on GL without restating it.
 */
namespace rhi {

enum class PrimitiveTopology : std::uint8_t { Triangles, TriangleStrip, Lines, LineStrip, Points };

/** Every fixed-function field, resolved: what the GL context holds after a sequence of deltas. */
struct ResolvedPipelineState {
    bool blend = false;
    BlendFunc blendFunc{BlendFactor::One, BlendFactor::Zero};
    bool depthTest = false;
    bool depthWrite = true;
    CompareOp depthFunc = CompareOp::Less;
    bool cullFace = false;
    FrontFace frontFace = FrontFace::CounterClockwise;
    bool polygonOffset = false;
    // glPolygonOffset keeps its last values while the offset is off, as GL does.
    float polygonOffsetFactor = 0.0f;
    float polygonOffsetUnits = 0.0f;
    bool rasterizerDiscard = false;

    /** A fresh GL context's state (glEnable nothing, glDepthMask true, GL_LESS, CCW). */
    static ResolvedPipelineState GlInitial() { return {}; }
    /** GlInitial plus what Application::ApplyGlobalGlState sets once per context: depth test, back-face culling. */
    static ResolvedPipelineState ApplicationBaseline();

    bool operator==(const ResolvedPipelineState& other) const;
    bool operator!=(const ResolvedPipelineState& other) const { return !(*this == other); }
};

/** Folds PipelineState deltas exactly as GlCommandList::SetPipelineState changes the context. */
class PipelineTracker {
public:
    explicit PipelineTracker(const ResolvedPipelineState& initial = ResolvedPipelineState::ApplicationBaseline())
        : _current(initial) {}
    void Apply(const PipelineState& delta);
    void Reset(const ResolvedPipelineState& state);
    const ResolvedPipelineState& Current() const { return _current; }
    /** Bumped whenever Current() changes, so a pipeline lookup can be skipped when it has not. */
    std::uint64_t Revision() const { return _revision; }

private:
    ResolvedPipelineState _current;
    std::uint64_t _revision = 0;
};

/**
 * WebGPU's view of one resolved state (GPURenderPipelineDescriptor's primitive, depthStencil
 * and blend), with GL's implicit rules made explicit:
 *  - depth test off means no depth writes either (GL disables both), so compare is Always and
 *    write is false;
 *  - glPolygonOffset(factor, units) is depthBiasSlopeScale = factor, depthBias = units;
 *  - the WebGPU backend renders y-flipped (so targets hold GL's row order, §7.9), which turns
 *    every triangle's winding around: `flipY` swaps the front face to keep culling identical.
 */
struct WebGpuFixedFunction {
    bool blend = false;
    BlendFactor srcFactor = BlendFactor::One;
    BlendFactor dstFactor = BlendFactor::Zero;
    CompareOp depthCompare = CompareOp::Always;
    bool depthWrite = false;
    std::int32_t depthBias = 0;
    float depthBiasSlopeScale = 0.0f;
    bool cullBack = false;
    FrontFace frontFace = FrontFace::CounterClockwise;
    /** Rasterizer discard: transform-feedback-only draws, which WebGPU does as compute instead. */
    bool discard = false;

    bool operator==(const WebGpuFixedFunction& other) const;
};

WebGpuFixedFunction ToWebGpuFixedFunction(const ResolvedPipelineState& state, bool flipY);

/**
 * What identifies a pipeline object: program, vertex layout, topology, attachment formats and
 * the WebGPU fixed function (so two GL states that draw identically — blend factors while
 * blending is off, say — share one pipeline).
 */
struct PipelineKey {
    std::uint64_t program = 0;
    std::uint32_t vertexLayout = 0;
    PrimitiveTopology topology = PrimitiveTopology::Triangles;
    TextureFormat colorFormat = TextureFormat::Rgba8; // Unknown: depth-only target
    TextureFormat depthFormat = TextureFormat::Unknown;
    WebGpuFixedFunction fixedFunction;

    bool operator==(const PipelineKey& other) const;
    bool operator!=(const PipelineKey& other) const { return !(*this == other); }
    std::size_t Hash() const;
};

struct PipelineKeyHash {
    std::size_t operator()(const PipelineKey& key) const { return key.Hash(); }
};

} // namespace rhi

#endif // SOLARSYSTEM_GFX_RHI_PIPELINETRACKER_H
