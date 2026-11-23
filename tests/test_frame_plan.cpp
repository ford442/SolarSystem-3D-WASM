#include <gtest/gtest.h>

#include "core/FrameProfile.h"
#include "gfx/rhi/CommandList.h"
#include "recording_command_list.h"
#include "render/graph/FrameGraph.h"
#include "render/graph/FramePlan.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace {

// Records every sink call as a short token, e.g. "S1" = shadow map of component 1,
// "C0:1" = view 0 draws component 1, "V0" = view 0 bound.
class RecorderSink : public FramePlan::PassSink {
public:
    std::vector<std::string> log;
    std::vector<std::uint32_t> bloomSlots;

    void BeginView(rhi::CommandList&, int view) override { log.push_back("V" + std::to_string(view)); }
    void SolarWindUpdate(rhi::CommandList&) override { log.emplace_back("advect"); }
    void ObserveSkyPass(rhi::CommandList&, int view) override { log.push_back("observe" + std::to_string(view)); }
    void SkyBoxPass(rhi::CommandList&, int view) override { log.push_back("B" + std::to_string(view)); }
    void OrbitPaths(rhi::CommandList&, int view) override { log.push_back("orbits" + std::to_string(view)); }
    void MissionPaths(rhi::CommandList&, int view) override { log.push_back("missions" + std::to_string(view)); }
    void XrPointers(rhi::CommandList&, int view) override { log.push_back("pointers" + std::to_string(view)); }
    void FlatCorona(rhi::CommandList&, int view) override { log.push_back("corona" + std::to_string(view)); }
    void ShadowMap(rhi::CommandList&, std::size_t component) override {
        log.push_back("S" + std::to_string(component));
    }
    void ClearShadowMap(rhi::CommandList&) override { log.emplace_back("S-clear"); }
    void SceneComponent(rhi::CommandList&, int view, std::size_t component) override {
        log.push_back("C" + std::to_string(view) + ":" + std::to_string(component));
    }
    void CoronaVolume(rhi::CommandList&, int view) override { log.push_back("volume" + std::to_string(view)); }
    void Asteroids(rhi::CommandList&, int view) override { log.push_back("belts" + std::to_string(view)); }
    void SolarWind(rhi::CommandList&, int view) override { log.push_back("wind" + std::to_string(view)); }
    void MagneticFields(rhi::CommandList&, int view, bool restore) override {
        log.push_back("fields" + std::to_string(view) + (restore ? "+restore" : ""));
    }
    void MagneticBloom(rhi::CommandList&, int view, std::uint32_t slot) override {
        log.push_back("field-bloom" + std::to_string(view));
        bloomSlots.push_back(slot);
    }
    void MagneticBloomComposite(rhi::CommandList&, int view, std::uint32_t) override {
        log.push_back("field-composite" + std::to_string(view));
    }
    void AuroraBloom(rhi::CommandList&, int view, std::uint32_t slot) override {
        log.push_back("aurora-bloom" + std::to_string(view));
        bloomSlots.push_back(slot);
    }
    void AuroraBloomComposite(rhi::CommandList&, int view, std::uint32_t) override {
        log.push_back("aurora-composite" + std::to_string(view));
    }
    void ProxyLabels(rhi::CommandList&) override { log.emplace_back("labels"); }
    void StarGlow(rhi::CommandList&) override { log.emplace_back("glow"); }
    void HdrComposite(rhi::CommandList&) override { log.emplace_back("hdr"); }
    void LensFlarePass(rhi::CommandList&) override { log.emplace_back("flare"); }
    void ObserveHud(rhi::CommandList&) override { log.emplace_back("observe-hud"); }
    void Distances(rhi::CommandList&) override { log.emplace_back("distances"); }
    void Hints(rhi::CommandList&) override { log.emplace_back("hints"); }
    void ScreenFade(rhi::CommandList&) override { log.emplace_back("fade"); }
    void Uploads(rhi::CommandList&) override { log.emplace_back("uploads"); }
    void LoadingProgress(rhi::CommandList&) override { log.emplace_back("progress"); }

    int IndexOf(const std::string& token) const {
        for (std::size_t i = 0; i < log.size(); ++i) {
            if (log[i] == token) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
    int Count(const std::string& prefix) const {
        int count = 0;
        for (const std::string& token : log) {
            count += token.rfind(prefix, 0) == 0 ? 1 : 0;
        }
        return count;
    }
};

FramePlan::Features Explore(int views, std::size_t components) {
    FramePlan::Features f;
    f.views = views;
    f.xr = views > 1;
    for (std::size_t c = 0; c < components; ++c) {
        f.components.push_back({"P" + std::to_string(c), true});
    }
    f.display.width = 1280;
    f.display.height = 720;
    f.display.format = rhi::TextureFormat::Rgba8;
    f.shadowMap.width = f.shadowMap.height = 2048;
    f.shadowMap.format = rhi::TextureFormat::Depth32F;
    return f;
}

/** Build, compile and run `f`; returns the sink's log (BeginView calls dropped unless asked). */
struct Frame {
    FrameGraph::Graph graph;
    RecorderSink sink;
    RecordingCommandList commands;
    FramePlan::Resources resources;

    explicit Frame(const FramePlan::Features& f, FrameProfile::PassTimings* timings = nullptr) {
        resources = FramePlan::Build(graph, f, sink, timings);
        EXPECT_TRUE(graph.Compile()) << (graph.Errors().empty() ? "" : graph.Errors().front());
        graph.Execute(commands);
    }
    std::vector<std::string> Draws() const {
        std::vector<std::string> out;
        for (const std::string& token : sink.log) {
            if (token[0] != 'V') {
                out.push_back(token);
            }
        }
        return out;
    }
    bool Has(const std::string& pass) const { return graph.FindLive(pass) != nullptr; }
};

} // namespace

TEST(FramePlan, MonoFrameKeepsTheHandWrittenOrder) {
    FramePlan::Features f = Explore(1, 2);
    f.orbitPaths = f.flatCorona = f.asteroids = true;
    f.starEffects = f.hdr = true;
    f.distances = f.hints = f.loadingProgress = true;
    Frame frame(f);
    const std::vector<std::string> expected = {"B0",    "orbits0",   "corona0", "S0",      "C0:0",
                                               "S1",    "C0:1",      "belts0",  "glow",    "hdr",
                                               "flare", "distances", "hints",   "uploads", "progress"};
    EXPECT_EQ(frame.Draws(), expected);
    // Every per-view pass binds its view first.
    EXPECT_EQ(frame.sink.log[0], "V0");
    EXPECT_EQ(frame.sink.log[frame.sink.IndexOf("C0:1") - 1], "V0");
}

TEST(FramePlan, XrFrameRendersEachShadowMapOnceForBothEyes) {
    Frame frame(Explore(2, 4));
    EXPECT_EQ(frame.sink.Count("S"), 4);
    EXPECT_EQ(frame.sink.Count("C"), 8);
    EXPECT_EQ(frame.graph.CountLive("shadow"), 4u);
    EXPECT_EQ(frame.graph.CountLive("scene"), 8u);
}

TEST(FramePlan, ShadowMapIsConsumedByEveryViewBeforeTheNextOneOverwritesIt) {
    Frame frame(Explore(2, 3));
    const RecorderSink& log = frame.sink;
    for (int c = 0; c < 3; ++c) {
        const int shadow = log.IndexOf("S" + std::to_string(c));
        const int nextShadow = c + 1 < 3 ? log.IndexOf("S" + std::to_string(c + 1)) : static_cast<int>(log.log.size());
        for (int view = 0; view < 2; ++view) {
            const int draw = log.IndexOf("C" + std::to_string(view) + ":" + std::to_string(c));
            EXPECT_GT(draw, shadow) << "view " << view << " component " << c;
            EXPECT_LT(draw, nextShadow) << "view " << view << " component " << c;
        }
    }
    EXPECT_LT(log.IndexOf("B1"), log.IndexOf("S0"));
}

TEST(FramePlan, XrFrameHasNoMonoEffectsOrHud) {
    FramePlan::Features f = Explore(2, 1);
    f.xrPointers = true;
    Frame frame(f);
    EXPECT_TRUE(frame.Has("xr-pointers/0"));
    EXPECT_TRUE(frame.Has("xr-pointers/1"));
    EXPECT_FALSE(frame.Has("star-glow"));
    EXPECT_FALSE(frame.Has("hints"));
    EXPECT_TRUE(frame.Has("uploads"));
}

TEST(FramePlan, NoViewsStillUploads) {
    // An XR frame whose eye count has not arrived yet: uploads keep running.
    Frame frame(Explore(0, 5));
    EXPECT_EQ(frame.Draws(), std::vector<std::string>{"uploads"});
}

TEST(FramePlan, ShadowsOffClearTheMapOnceInsteadOfDrawingIt) {
    FramePlan::Features f = Explore(1, 3);
    f.shadows = false;
    Frame frame(f);
    EXPECT_EQ(frame.sink.Count("S-clear"), 1);
    EXPECT_EQ(frame.graph.CountLive("shadow"), 1u);
    EXPECT_LT(frame.sink.IndexOf("S-clear"), frame.sink.IndexOf("C0:0"));
}

TEST(FramePlan, HiddenComponentsDrawButCastNoShadow) {
    FramePlan::Features f = Explore(1, 2);
    f.components[1].castsShadow = false; // the nearest component, drawn only for the star
    Frame frame(f);
    EXPECT_TRUE(frame.Has("shadow:P0"));
    EXPECT_FALSE(frame.Has("shadow:P1"));
    EXPECT_TRUE(frame.Has("scene:P1/0"));
}

TEST(FramePlan, NoLayerFramebufferMeansNoShadowPassAtAll) {
    FramePlan::Features f = Explore(2, 2);
    f.shadowTarget = false;
    Frame frame(f);
    EXPECT_EQ(frame.graph.CountLive("shadow"), 0u);
    EXPECT_EQ(frame.graph.CountLive("scene"), 4u);
}

TEST(FramePlan, ObserveDrawsOnlyTheSkyAndItsHud) {
    FramePlan::Features f = Explore(1, 4);
    f.observe = true;
    f.observeHud = true;
    Frame frame(f);
    EXPECT_EQ(frame.Draws(), (std::vector<std::string>{"observe0", "observe-hud", "uploads"}));
}

TEST(FramePlan, SolarWindOffCullsTheAdvectionPass) {
    // The particle system exists, but nothing draws it this frame: the graph drops the
    // transform-feedback pass (and with it the MagnetosphereUBO upload) by itself.
    FramePlan::Features f = Explore(1, 1);
    f.solarWindSystem = true;
    f.solarWind = false;
    Frame off(f);
    EXPECT_FALSE(off.Has("solar-wind-update"));
    EXPECT_EQ(off.sink.Count("advect"), 0);
    ASSERT_EQ(off.graph.CulledCount(), 1u);

    f.solarWind = true;
    Frame on(f);
    EXPECT_TRUE(on.Has("solar-wind-update"));
    EXPECT_TRUE(on.Has("solar-wind/0"));
    EXPECT_LT(on.sink.IndexOf("advect"), on.sink.IndexOf("wind0"));
    EXPECT_EQ(on.graph.CulledCount(), 0u);
}

TEST(FramePlan, TurningAFeatureOffRemovesItsPasses) {
    struct Toggle {
        const char* name;
        std::function<void(FramePlan::Features&)> enable;
        std::vector<const char*> passes;
    };
    const std::vector<Toggle> toggles = {
        {"orbit lines", [](FramePlan::Features& f) { f.orbitPaths = true; }, {"orbit-paths/0"}},
        {"mission paths", [](FramePlan::Features& f) { f.missionPaths = true; }, {"mission-paths/0"}},
        {"flat corona", [](FramePlan::Features& f) { f.flatCorona = true; }, {"corona/0"}},
        {"volumetric corona", [](FramePlan::Features& f) { f.coronaVolume = true; }, {"corona-volume/0"}},
        {"asteroid belts", [](FramePlan::Features& f) { f.asteroids = true; }, {"asteroids/0"}},
        {"solar wind",
         [](FramePlan::Features& f) { f.solarWindSystem = f.solarWind = true; },
         {"solar-wind-update", "solar-wind/0"}},
        {"magnetic fields", [](FramePlan::Features& f) { f.magneticFields = true; }, {"magnetic-fields/0"}},
        {"field bloom",
         [](FramePlan::Features& f) { f.magneticFields = f.magneticBloom = true; },
         {"magnetic-fields/0", "magnetic-bloom/0", "magnetic-bloom-composite/0"}},
        {"aurora bloom",
         [](FramePlan::Features& f) { f.auroraBloom = true; },
         {"aurora-bloom/0", "aurora-bloom-composite/0"}},
        {"star effects", [](FramePlan::Features& f) { f.starEffects = true; }, {"star-glow", "lens-flare"}},
        {"HDR",
         [](FramePlan::Features& f) { f.starEffects = f.hdr = true; },
         {"star-glow", "hdr-composite", "lens-flare"}},
        {"staging labels", [](FramePlan::Features& f) { f.proxyLabels = true; }, {"proxy-labels"}},
        {"distances", [](FramePlan::Features& f) { f.distances = true; }, {"distances"}},
        {"hints", [](FramePlan::Features& f) { f.hints = true; }, {"hints"}},
        {"screen fade", [](FramePlan::Features& f) { f.screenFade = true; }, {"screen-fade"}},
        {"loading progress", [](FramePlan::Features& f) { f.loadingProgress = true; }, {"loading-progress"}},
    };
    const FramePlan::Features base = Explore(1, 2);
    const Frame baseline(base);
    for (const Toggle& toggle : toggles) {
        FramePlan::Features on = base;
        toggle.enable(on);
        const Frame enabled(on);
        for (const char* pass : toggle.passes) {
            EXPECT_TRUE(enabled.Has(pass)) << toggle.name << " on should run " << pass;
            EXPECT_FALSE(baseline.Has(pass)) << toggle.name << " off should not run " << pass;
        }
        // Nothing else changes: the feature's passes are the whole difference.
        EXPECT_EQ(enabled.graph.Order().size(), baseline.graph.Order().size() + toggle.passes.size()) << toggle.name;
    }
}

TEST(FramePlan, TheTwoBloomChainsAliasOntoOnePhysicalChain) {
    FramePlan::Features f = Explore(1, 1);
    f.magneticFields = f.magneticBloom = f.auroraBloom = true;
    Frame frame(f);
    EXPECT_EQ(frame.graph.PhysicalCount(), 1u);
    EXPECT_EQ(frame.sink.bloomSlots, (std::vector<std::uint32_t>{0u, 0u}));
    // The ribbons leave their GL state for the bloom pass, which restores it.
    EXPECT_EQ(frame.sink.Count("fields0+restore"), 0);
    EXPECT_LT(frame.sink.IndexOf("field-composite0"), frame.sink.IndexOf("aurora-bloom0"));

    f.magneticBloom = false;
    Frame noBloom(f);
    EXPECT_EQ(noBloom.sink.Count("fields0+restore"), 1);
}

TEST(FramePlan, PassesOfAKindShareATimerSlot) {
    FrameProfile::PassTimings timings;
    Frame frame(Explore(2, 3), &timings);
    int sceneSlot = -2;
    for (const rhi::PassInfo& pass : frame.commands.passes) {
        EXPECT_GE(pass.timerSlot, 0) << pass.name;
        if (std::string(pass.kind) == "scene") {
            if (sceneSlot == -2) {
                sceneSlot = pass.timerSlot;
            }
            EXPECT_EQ(pass.timerSlot, sceneSlot);
            EXPECT_EQ(pass.stage, static_cast<int>(FrameProfile::Stage::Scene));
        }
    }
    EXPECT_EQ(timings.Kind(static_cast<std::size_t>(sceneSlot)), "scene");
}
