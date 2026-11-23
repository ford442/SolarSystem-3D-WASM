#include <gtest/gtest.h>

#include "gfx/rhi/CommandList.h"
#include "gfx/rhi/RhiTypes.h"
#include "recording_command_list.h"
#include "render/graph/FrameGraph.h"

#include <cstddef>
#include <string>
#include <vector>

namespace {

using FrameGraph::Graph;
using FrameGraph::ResourceHandle;

rhi::TextureDesc Desc(std::uint32_t width, std::uint32_t height,
                      rhi::TextureFormat format = rhi::TextureFormat::Rgba16F) {
    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    return desc;
}

FrameGraph::PassDesc Named(const std::string& name, const std::string& kind = "test") {
    FrameGraph::PassDesc desc;
    desc.name = name;
    desc.kind = kind;
    return desc;
}

/** Names of the compiled passes, in order. */
std::vector<std::string> OrderNames(const Graph& graph) {
    std::vector<std::string> names;
    for (const FrameGraph::PassId id : graph.Order()) {
        names.push_back(graph.Passes()[id].desc.name);
    }
    return names;
}

} // namespace

TEST(FrameGraph, PassesWithNoConsumerAreCulled) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle particles = graph.Import("Particles", Desc(0, 0, rhi::TextureFormat::Buffer));
    auto update = graph.AddPass(Named("advect"), {});
    particles = update.Write(particles);
    auto draw = graph.AddPass(Named("draw"), {});
    color = draw.Write(color);
    ASSERT_TRUE(graph.Compile()) << graph.Errors().front();

    EXPECT_EQ(OrderNames(graph), std::vector<std::string>{"draw"});
    EXPECT_TRUE(graph.Passes()[update.Id()].culled);
    EXPECT_EQ(graph.CulledCount(), 1u);
}

TEST(FrameGraph, AConsumerKeepsItsProducerAlive) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle particles = graph.Import("Particles", Desc(0, 0, rhi::TextureFormat::Buffer));
    particles = graph.AddPass(Named("advect"), {}).Write(particles);
    auto draw = graph.AddPass(Named("draw"), {});
    draw.Read(particles);
    color = draw.Write(color);
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(OrderNames(graph), (std::vector<std::string>{"advect", "draw"}));
}

TEST(FrameGraph, SideEffectsSurviveWithoutAConsumer) {
    Graph graph;
    graph.AddPass(Named("uploads"), {}).SideEffect();
    graph.AddPass(Named("orphan"), {});
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(OrderNames(graph), std::vector<std::string>{"uploads"});
}

TEST(FrameGraph, ALoadingWriteNeedsThePreviousVersionButAnOverwriteDoesNot) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle shadow = graph.Import("Shadow", Desc(32, 32, rhi::TextureFormat::Depth32F));
    // Two shadow passes, the second overwriting the first; only the second is read.
    shadow = graph.AddPass(Named("shadow-a"), {}).Overwrite(shadow);
    shadow = graph.AddPass(Named("shadow-b"), {}).Overwrite(shadow);
    auto scene = graph.AddPass(Named("scene"), {});
    scene.Read(shadow);
    color = scene.Write(color);
    // Drawing over the scene keeps it, because a Write loads what was there.
    color = graph.AddPass(Named("hud"), {}).Write(color);
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(OrderNames(graph), (std::vector<std::string>{"shadow-b", "scene", "hud"}));
}

TEST(FrameGraph, ReadersOfAVersionRunBeforeItIsOverwritten) {
    // One shadow map shared by two components and two views: every view's colour pass of
    // component 0 must land between shadow 0 and shadow 1 (write-after-read).
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle shadow = graph.Import("Shadow", Desc(32, 32, rhi::TextureFormat::Depth32F));
    for (int component = 0; component < 2; ++component) {
        shadow = graph.AddPass(Named("S" + std::to_string(component)), {}).Overwrite(shadow);
        for (int view = 0; view < 2; ++view) {
            auto pass = graph.AddPass(Named("C" + std::to_string(view) + ":" + std::to_string(component)), {});
            pass.Read(shadow);
            color = pass.Write(color);
        }
    }
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(OrderNames(graph), (std::vector<std::string>{"S0", "C0:0", "C1:0", "S1", "C0:1", "C1:1"}));
}

TEST(FrameGraph, AStaleReadIsMovedBeforeTheOverwrite) {
    // A pass declared late that reads an old version is ordered before the pass that
    // overwrote it, not after: the sort follows the data, not only the declaration order.
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle map = graph.Import("Map", Desc(8, 8));
    const ResourceHandle first = graph.AddPass(Named("write-1"), {}).Overwrite(map);
    (void)graph.AddPass(Named("write-2"), {}).Overwrite(first);
    auto late = graph.AddPass(Named("read-1"), {});
    late.Read(first);
    color = late.Write(color);
    // A reader of write-2's version keeps write-2 alive.
    auto reader = graph.AddPass(Named("read-2"), {});
    reader.Read({first.index, 2});
    color = reader.Write(color);
    ASSERT_TRUE(graph.Compile()) << (graph.Errors().empty() ? "" : graph.Errors().front());
    EXPECT_EQ(OrderNames(graph), (std::vector<std::string>{"write-1", "read-1", "write-2", "read-2"}));
}

TEST(FrameGraph, TransientsWithDisjointLifetimesShareAPhysicalSlot) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    const rhi::TextureDesc half = Desc(32, 32);
    for (const char* name : {"fields", "aurora"}) {
        ResourceHandle bloom = graph.Create(std::string("Bloom:") + name, half);
        bloom = graph.AddPass(Named(std::string(name) + "-capture"), {}).Overwrite(bloom);
        auto composite = graph.AddPass(Named(std::string(name) + "-composite"), {});
        composite.Read(bloom);
        color = composite.Write(color);
    }
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(graph.PhysicalCount(), 1u);
    EXPECT_EQ(graph.PhysicalOf({1, 0}), 0u);
    EXPECT_EQ(graph.PhysicalOf({2, 0}), 0u);
}

TEST(FrameGraph, OverlappingOrDifferentTransientsGetTheirOwnSlots) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle a = graph.Create("A", Desc(32, 32));
    ResourceHandle b = graph.Create("B", Desc(32, 32));
    ResourceHandle c = graph.Create("C", Desc(16, 16)); // another size: never shares with A or B
    a = graph.AddPass(Named("write-a"), {}).Overwrite(a);
    b = graph.AddPass(Named("write-b"), {}).Overwrite(b); // while A is still waiting for its reader
    c = graph.AddPass(Named("write-c"), {}).Overwrite(c);
    auto use = graph.AddPass(Named("use"), {});
    use.Read(a).Read(b).Read(c);
    color = use.Write(color);
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(graph.PhysicalCount(), 3u);
    EXPECT_NE(graph.PhysicalOf(a), graph.PhysicalOf(b));
}

TEST(FrameGraph, CulledTransientsTakeNoSlot) {
    Graph graph;
    graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle unused = graph.Create("Unused", Desc(32, 32));
    graph.AddPass(Named("write"), {}).Overwrite(unused);
    ASSERT_TRUE(graph.Compile());
    EXPECT_EQ(graph.PhysicalCount(), 0u);
    EXPECT_EQ(graph.PhysicalOf(unused), FrameGraph::kInvalid);
}

TEST(FrameGraph, InvalidAccessesFailToCompile) {
    {
        Graph graph;
        ResourceHandle transient = graph.Create("T", Desc(8, 8));
        graph.AddPass(Named("reads-garbage"), {}).Read(transient).SideEffect();
        EXPECT_FALSE(graph.Compile());
        EXPECT_FALSE(graph.Errors().empty());
    }
    {
        Graph graph;
        ResourceHandle color = graph.Import("Backbuffer", Desc(8, 8), {true});
        graph.AddPass(Named("a"), {}).Write(color);
        graph.AddPass(Named("b"), {}).Write(color); // version 0 again: not the latest
        EXPECT_FALSE(graph.Compile());
    }
    {
        Graph graph;
        ResourceHandle transient = graph.Create("T", Desc(8, 8));
        graph.AddPass(Named("loads-garbage"), {}).Write(transient);
        EXPECT_FALSE(graph.Compile());
    }
}

TEST(FrameGraph, ExecuteBracketsEveryLivePassInOrder) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    std::vector<std::string> ran;
    FrameGraph::PassDesc first = Named("first", "scene");
    first.timerSlot = 3;
    first.stage = 2;
    color = graph
                .AddPass(first,
                         [&](rhi::CommandList& cmd) {
                             ran.emplace_back("first");
                             cmd.Clear(rhi::ClearFlags::ColorDepth);
                         })
                .Write(color);
    graph.AddPass(Named("culled"), [&](rhi::CommandList&) { ran.emplace_back("culled"); });
    color = graph.AddPass(Named("second"), [&](rhi::CommandList&) { ran.emplace_back("second"); }).Write(color);
    ASSERT_TRUE(graph.Compile());

    RecordingCommandList commands;
    graph.Execute(commands);
    EXPECT_EQ(ran, (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(commands.log, (std::vector<std::string>{"begin first", "clear", "end", "begin second", "end"}));
    ASSERT_EQ(commands.passes.size(), 2u);
    EXPECT_EQ(commands.passes[0].timerSlot, 3);
    EXPECT_EQ(commands.passes[0].stage, 2);
    EXPECT_STREQ(commands.passes[0].kind, "scene");
}

TEST(FrameGraph, DumpsNameEveryPassAndVersion) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    ResourceHandle bloom = graph.Create("Bloom", Desc(32, 32));
    bloom = graph.AddPass(Named("capture"), {}).Overwrite(bloom);
    auto composite = graph.AddPass(Named("composite"), {});
    composite.Read(bloom);
    color = composite.Write(color);
    graph.AddPass(Named("orphan"), {});
    ASSERT_TRUE(graph.Compile());

    const std::string dot = graph.ToDot();
    EXPECT_NE(dot.find("digraph FrameGraph"), std::string::npos);
    EXPECT_NE(dot.find("\"0: capture\""), std::string::npos);
    EXPECT_NE(dot.find("\"orphan\", style=dashed"), std::string::npos);
    EXPECT_NE(dot.find("\"Bloom#1\""), std::string::npos);

    const std::string json = graph.ToJson();
    EXPECT_NE(json.find(R"({"name":"orphan","kind":"test","stage":-1,"culled":true)"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("reads":["Bloom#1"],"writes":["Backbuffer#1"])"), std::string::npos) << json;
    EXPECT_NE(json.find(R"("physical":1,"culled":1})"), std::string::npos) << json;
    EXPECT_EQ(graph.Signature(), "capture,composite");
}

TEST(FrameGraph, ResetForgetsTheLastFrame) {
    Graph graph;
    ResourceHandle color = graph.Import("Backbuffer", Desc(64, 64), {true});
    color = graph.AddPass(Named("draw"), {}).Write(color);
    ASSERT_TRUE(graph.Compile());
    graph.Reset();
    EXPECT_TRUE(graph.Passes().empty());
    EXPECT_TRUE(graph.Resources().empty());
    EXPECT_TRUE(graph.Order().empty());
    ASSERT_TRUE(graph.Compile());
}
