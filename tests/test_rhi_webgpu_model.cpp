#include <gtest/gtest.h>

#include "gfx/rhi/Backend.h"
#include "gfx/rhi/PipelineTracker.h"
#include "gfx/rhi/RenderPassPlanner.h"
#include "gfx/rhi/RhiTypes.h"
#include "gfx/rhi/TimestampPairs.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// The GL-free half of the WebGPU backend (docs/ARCHITECTURE.md §7.9): how GL-style deltas,
// clears and viewports become whole pipelines and render passes.

namespace {

using rhi::BlendFactor;
using rhi::ClearFlags;
using rhi::CompareOp;
using rhi::FrontFace;
using rhi::LoadOp;
using rhi::PipelineState;
using rhi::PipelineTracker;
using rhi::RenderPassPlanner;
using rhi::ResolvedPipelineState;
using rhi::Viewport;

/** Writes each planner decision down as a token. */
class RecordingSink : public RenderPassPlanner::Sink {
public:
    std::vector<std::string> log;
    std::vector<rhi::RenderPassBegin> begins;

    void BeginRenderPass(const rhi::RenderPassBegin& begin) override {
        begins.push_back(begin);
        log.push_back("begin fb" + std::to_string(begin.framebuffer) + " color:" + Op(begin.colorLoad) +
                      " depth:" + Op(begin.depthLoad));
    }
    void SetPassViewport(const Viewport& viewport) override {
        log.push_back("viewport " + std::to_string(viewport.width) + "x" + std::to_string(viewport.height));
    }
    void EndRenderPass() override { log.emplace_back("end"); }

private:
    static std::string Op(LoadOp op) { return op == LoadOp::Clear ? "clear" : "load"; }
};

} // namespace

TEST(RhiBackend, NamesAndParsing) {
    EXPECT_STREQ(rhi::BackendName(rhi::Backend::WebGpu), "WebGPU");
    EXPECT_STREQ(rhi::BackendName(rhi::Backend::WebGl2), "WebGL 2");
    EXPECT_STREQ(rhi::BackendId(rhi::Backend::OpenGl), "opengl");
    EXPECT_EQ(rhi::ParseBackend("webgpu"), rhi::Backend::WebGpu);
    EXPECT_EQ(rhi::ParseBackend("wgpu"), rhi::Backend::WebGpu);
    EXPECT_EQ(rhi::ParseBackend("webgl"), rhi::GlBackendOfThisBuild());
    EXPECT_EQ(rhi::ParseBackend("gl"), rhi::GlBackendOfThisBuild());
    EXPECT_FALSE(rhi::ParseBackend("").has_value());
    EXPECT_FALSE(rhi::ParseBackend("vulkan").has_value());
}

TEST(PipelineTracker, StartsFromTheApplicationsBaseline) {
    const PipelineTracker tracker;
    EXPECT_TRUE(tracker.Current().depthTest);
    EXPECT_TRUE(tracker.Current().cullFace);
    EXPECT_TRUE(tracker.Current().depthWrite);
    EXPECT_FALSE(tracker.Current().blend);
    EXPECT_EQ(tracker.Current().depthFunc, CompareOp::Less);
}

TEST(PipelineTracker, UnsetFieldsKeepWhatThePreviousPassLeft) {
    PipelineTracker tracker;
    tracker.Apply(PipelineState().SetBlend(true).SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::One));
    tracker.Apply(PipelineState().SetDepthWrite(false));
    // The second delta said nothing about blending, so it is still on with its factors.
    EXPECT_TRUE(tracker.Current().blend);
    EXPECT_EQ(tracker.Current().blendFunc.src, BlendFactor::SrcAlpha);
    EXPECT_EQ(tracker.Current().blendFunc.dst, BlendFactor::One);
    EXPECT_FALSE(tracker.Current().depthWrite);
    // Turning blending off keeps glBlendFunc's factors, as GL does.
    tracker.Apply(PipelineState().SetBlend(false));
    EXPECT_FALSE(tracker.Current().blend);
    EXPECT_EQ(tracker.Current().blendFunc.dst, BlendFactor::One);
}

TEST(PipelineTracker, PolygonOffsetValuesChangeOnlyWhenTurnedOn) {
    PipelineTracker tracker;
    tracker.Apply(PipelineState().SetPolygonOffset(true, -1.0f, -2.0f));
    tracker.Apply(PipelineState().SetPolygonOffset(false));
    EXPECT_FALSE(tracker.Current().polygonOffset);
    EXPECT_EQ(tracker.Current().polygonOffsetFactor, -1.0f); // GlCommandList skips glPolygonOffset here
    EXPECT_EQ(tracker.Current().polygonOffsetUnits, -2.0f);
}

TEST(PipelineTracker, RevisionMovesOnlyOnARealChange) {
    PipelineTracker tracker;
    const std::uint64_t start = tracker.Revision();
    tracker.Apply(PipelineState().SetDepthTest(true)); // already on
    EXPECT_EQ(tracker.Revision(), start);
    tracker.Apply(PipelineState().SetDepthTest(false));
    EXPECT_EQ(tracker.Revision(), start + 1);
    tracker.Apply(PipelineState());
    EXPECT_EQ(tracker.Revision(), start + 1);
    tracker.Reset(ResolvedPipelineState::ApplicationBaseline());
    EXPECT_EQ(tracker.Revision(), start + 2);
}

TEST(WebGpuFixedFunction, DepthTestOffAlsoTurnsDepthWritesOff) {
    ResolvedPipelineState state = ResolvedPipelineState::ApplicationBaseline();
    state.depthTest = false;
    state.depthWrite = true; // glDepthMask(GL_TRUE) with the test off writes nothing in GL
    const rhi::WebGpuFixedFunction ff = rhi::ToWebGpuFixedFunction(state, false);
    EXPECT_EQ(ff.depthCompare, CompareOp::Always);
    EXPECT_FALSE(ff.depthWrite);

    state.depthTest = true;
    state.depthFunc = CompareOp::LessEqual;
    const rhi::WebGpuFixedFunction on = rhi::ToWebGpuFixedFunction(state, false);
    EXPECT_EQ(on.depthCompare, CompareOp::LessEqual);
    EXPECT_TRUE(on.depthWrite);
}

TEST(WebGpuFixedFunction, FlippedRenderingSwapsTheFrontFace) {
    ResolvedPipelineState state = ResolvedPipelineState::ApplicationBaseline();
    EXPECT_EQ(rhi::ToWebGpuFixedFunction(state, false).frontFace, FrontFace::CounterClockwise);
    EXPECT_EQ(rhi::ToWebGpuFixedFunction(state, true).frontFace, FrontFace::Clockwise);
    state.frontFace = FrontFace::Clockwise;
    EXPECT_EQ(rhi::ToWebGpuFixedFunction(state, true).frontFace, FrontFace::CounterClockwise);
    EXPECT_TRUE(rhi::ToWebGpuFixedFunction(state, true).cullBack);
}

TEST(WebGpuFixedFunction, PolygonOffsetIsDepthBias) {
    ResolvedPipelineState state;
    state.polygonOffset = true;
    state.polygonOffsetFactor = -1.0f;
    state.polygonOffsetUnits = -1.0f;
    const rhi::WebGpuFixedFunction ff = rhi::ToWebGpuFixedFunction(state, false);
    EXPECT_EQ(ff.depthBias, -1);
    EXPECT_EQ(ff.depthBiasSlopeScale, -1.0f);
    state.polygonOffset = false;
    EXPECT_EQ(rhi::ToWebGpuFixedFunction(state, false).depthBias, 0);
}

TEST(PipelineKey, StatesThatDrawAlikeShareAPipeline) {
    ResolvedPipelineState a = ResolvedPipelineState::ApplicationBaseline();
    ResolvedPipelineState b = a;
    b.blendFunc = rhi::BlendFunc{BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha}; // blending is off
    b.depthTest = a.depthTest;
    rhi::PipelineKey ka;
    ka.program = 7;
    ka.fixedFunction = rhi::ToWebGpuFixedFunction(a, true);
    rhi::PipelineKey kb = ka;
    kb.fixedFunction = rhi::ToWebGpuFixedFunction(b, true);
    EXPECT_EQ(ka, kb);
    EXPECT_EQ(ka.Hash(), kb.Hash());

    rhi::PipelineKey other = ka;
    other.colorFormat = rhi::TextureFormat::Rgba16F;
    EXPECT_NE(ka, other);
    rhi::PipelineKey blended = ka;
    blended.fixedFunction.blend = true;
    std::unordered_set<rhi::PipelineKey, rhi::PipelineKeyHash> set{ka, kb, other, blended};
    EXPECT_EQ(set.size(), 3u);
}

TEST(RenderPassPlanner, ClearsBeforeTheFirstDrawBecomeLoadOps) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(3);
    planner.BindFramebuffer(5);
    planner.Clear(ClearFlags::Color, {});
    planner.Clear(ClearFlags::Depth, {});
    planner.BeforeDraw();
    planner.BeforeDraw(); // a second draw stays in the same pass
    planner.EndGraphPass();
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb5 color:clear depth:clear", "end"}));
    ASSERT_EQ(sink.begins.size(), 1u);
    EXPECT_EQ(sink.begins[0].timerSlot, 3);
}

TEST(RenderPassPlanner, AClearAfterDrawsStartsTheNextPass) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(0);
    planner.BeforeDraw();
    planner.Clear(ClearFlags::Depth, {});
    planner.BeforeDraw();
    planner.EndGraphPass();
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb0 color:load depth:load", "end",
                                                  "begin fb0 color:load depth:clear", "end"}));
}

TEST(RenderPassPlanner, AClearNothingDrawsAfterStillRuns) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(-1);
    planner.BindFramebuffer(9); // the shadow map
    planner.Clear(ClearFlags::Depth, {});
    planner.BindFramebuffer(0); // back to the window before any draw
    planner.BeforeDraw();
    planner.EndGraphPass();
    // The empty shadow-clear pass, then the window's own.
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb9 color:load depth:clear", "end",
                                                  "begin fb0 color:load depth:load", "end"}));

    sink.log.clear();
    planner.BeginGraphPass(-1);
    planner.BindFramebuffer(9);
    planner.Clear(ClearFlags::Depth, {});
    planner.EndGraphPass(); // ClearShadowMap: a whole graph pass that only clears
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb9 color:load depth:clear", "end"}));
}

TEST(RenderPassPlanner, DepthClearsNeedDepthWritesAsInGl) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(-1);
    planner.Clear(ClearFlags::ColorDepth, {}, /*depthWritable=*/false);
    planner.BeforeDraw();
    planner.Clear(ClearFlags::Depth, {}, /*depthWritable=*/false); // a no-op: nothing splits
    planner.BeforeDraw();
    planner.EndGraphPass();
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb0 color:clear depth:load", "end"}));
}

TEST(RenderPassPlanner, TheClearValueInForceIsTheOneUsed) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(-1);
    rhi::ClearValue transparent;
    transparent.color = {0.0f, 0.0f, 0.0f, 0.0f}; // the bloom chain's glClearColor
    planner.Clear(ClearFlags::Color, transparent);
    planner.BeforeDraw();
    planner.EndGraphPass();
    ASSERT_EQ(sink.begins.size(), 1u);
    EXPECT_EQ(sink.begins[0].clear.color[3], 0.0f);
    EXPECT_EQ(sink.begins[0].clear.depth, 1.0f);
}

TEST(RenderPassPlanner, TheViewportIsContextStateAcrossTargetsAndPasses) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(-1);
    planner.SetViewport({0, 0, 640, 480}); // no pass open: remembered
    planner.BeforeDraw();
    planner.SetViewport({0, 0, 320, 240}); // inside the pass: encoded
    planner.SetViewport({0, 0, 320, 240}); // unchanged: nothing
    planner.EndGraphPass();
    planner.BeginGraphPass(-1);
    planner.BindFramebuffer(4); // BindFramebuffer leaves the viewport alone, as in GL
    planner.BeforeDraw();
    planner.EndGraphPass();
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb0 color:load depth:load", "viewport 320x240", "end",
                                                  "begin fb4 color:load depth:load", "end"}));
    ASSERT_EQ(sink.begins.size(), 2u);
    EXPECT_EQ(sink.begins[0].viewport.width, 640);
    EXPECT_EQ(sink.begins[1].viewport.width, 320);
}

TEST(RenderPassPlanner, GraphPassesNeverShareARenderPass) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(1);
    planner.BeforeDraw();
    planner.BeginGraphPass(2); // no EndGraphPass in between still closes the first
    planner.BeforeDraw();
    planner.EndGraphPass();
    ASSERT_EQ(sink.begins.size(), 2u);
    EXPECT_EQ(sink.begins[0].timerSlot, 1);
    EXPECT_EQ(sink.begins[1].timerSlot, 2);
    EXPECT_EQ(planner.RenderPassCount(), 2u);
    EXPECT_FALSE(planner.InRenderPass());
}

TEST(RenderPassPlanner, RebindingTheBoundTargetKeepsThePassOpen) {
    RecordingSink sink;
    RenderPassPlanner planner(sink);
    planner.BeginGraphPass(-1);
    planner.BindFramebuffer(2);
    planner.BeforeDraw();
    planner.BindFramebuffer(2);
    planner.BeforeDraw();
    planner.EndGraphPass();
    EXPECT_EQ(sink.log, (std::vector<std::string>{"begin fb2 color:load depth:load", "end"}));
}

TEST(TimestampPairs, PairsSumPerSlotAndSkipBadPairs) {
    rhi::TimestampPairs pairs(3);
    pairs.BeginFrame();
    EXPECT_EQ(pairs.Reserve(-1), -1); // untimed pass
    EXPECT_EQ(pairs.Reserve(9), 0);
    EXPECT_EQ(pairs.Reserve(9), 1);
    EXPECT_EQ(pairs.Reserve(10), 2);
    EXPECT_EQ(pairs.Reserve(10), -1); // set full
    EXPECT_EQ(pairs.Count(), 3u);
    const std::uint64_t ticks[] = {1'000'000, 3'000'000, 5'000'000, 5'500'000, 9'000'000, 8'000'000};
    std::vector<double> ms;
    pairs.Resolve(ticks, 6, ms);
    ASSERT_EQ(ms.size(), 10u);
    EXPECT_DOUBLE_EQ(ms[9], 2.5);
    EXPECT_DOUBLE_EQ(ms[0], 0.0);
    pairs.Resolve(ticks, 6, ms); // accumulates into what is there
    EXPECT_DOUBLE_EQ(ms[9], 5.0);
    pairs.BeginFrame();
    EXPECT_EQ(pairs.Count(), 0u);
}
