#include <gtest/gtest.h>

#include "core/FrameProfile.h"

#include <chrono>
#include <string>

TEST(FrameProfile, JsonCarriesTheBackendAndAnyWebGpuDevice) {
    FrameProfile::Sample sample;
    sample.backend = "webgl2";
    sample.backendName = "WebGL 2";
    const FrameProfile::Totals totals;
    std::string json = FrameProfile::ToJson(sample, totals);
    EXPECT_NE(json.find("\"backend\":\"webgl2\",\"backendName\":\"WebGL 2\",\"webgpu\":null,"), std::string::npos)
        << json;
    sample.webgpuJson = "{\"device\":\"SwiftShader\"}";
    json = FrameProfile::ToJson(sample, totals);
    EXPECT_NE(json.find("\"webgpu\":{\"device\":\"SwiftShader\"},\"frameMs\""), std::string::npos) << json;
}

TEST(FrameProfile, JsonCarriesEveryStageAndNullGpuUntilValid) {
    FrameProfile::Sample sample;
    sample.frameMs = 16.5;
    sample.cpuMs[static_cast<std::size_t>(FrameProfile::Stage::Scene)] = 4.25;
    sample.jobWorkers = 3;
    sample.uploadBytes = 4096;
    FrameProfile::Totals totals;
    totals.Add(sample);
    const std::string json = FrameProfile::ToJson(sample, totals);
    for (const char* key : {"\"update\":", "\"background\":", "\"shadow\":", "\"scene\":4.250",
                            "\"foreground\":", "\"hud\":", "\"uploads\":", "\"gpu\":null", "\"workers\":3",
                            "\"bytes\":4096", "\"frameMs\":16.500", "\"slowFrames\":0", "\"frames\":1",
                            "\"backend\":\"opengl\"", "\"backendName\":\"OpenGL 4.6\""}) {
        EXPECT_NE(json.find(key), std::string::npos) << key << " in " << json;
    }
    sample.gpuValid = true;
    sample.gpuMs[static_cast<std::size_t>(FrameProfile::Stage::Shadow)] = 1.5;
    EXPECT_NE(
        FrameProfile::ToJson(sample, totals).find("\"gpu\":{\"update\":0.000,\"background\":0.000,\"shadow\":1.500"),
        std::string::npos);
}

TEST(FrameProfile, TotalsCountSlowFramesAndTheWorstSinceReset) {
    FrameProfile::Totals totals;
    FrameProfile::Sample sample;
    for (const double ms : {16.0, 40.0, 20.0, FrameProfile::kSlowFrameMs + 0.1}) {
        sample.frameMs = ms;
        totals.Add(sample);
    }
    EXPECT_EQ(totals.frames, 4u);
    EXPECT_EQ(totals.slowFrames, 2u);
    EXPECT_DOUBLE_EQ(totals.worstFrameMs, 40.0);
    totals.ResetWorst();
    sample.frameMs = 12.0;
    totals.Add(sample);
    EXPECT_DOUBLE_EQ(totals.worstFrameMs, 12.0);
}

TEST(FrameProfile, CpuStagesSumRepeatedBrackets) {
    FrameProfile::CpuStages stages;
    stages.Add(FrameProfile::Stage::Shadow, std::chrono::microseconds(1500));
    stages.Add(FrameProfile::Stage::Shadow, std::chrono::microseconds(500));
    EXPECT_DOUBLE_EQ(stages.Ms()[static_cast<std::size_t>(FrameProfile::Stage::Shadow)], 2.0);
    stages.Reset();
    EXPECT_DOUBLE_EQ(stages.Ms()[static_cast<std::size_t>(FrameProfile::Stage::Shadow)], 0.0);
}

TEST(FrameProfile, PassTimingsKeepSlotsAndSumRunsPerKind) {
    FrameProfile::PassTimings timings;
    const int scene = timings.SlotFor("scene", FrameProfile::Stage::Scene);
    const int shadow = timings.SlotFor("shadow", FrameProfile::Stage::Shadow);
    EXPECT_EQ(scene, 0);
    EXPECT_EQ(shadow, 1);
    EXPECT_EQ(timings.SlotFor("scene", FrameProfile::Stage::Scene), scene);
    timings.Add(scene, std::chrono::microseconds(250));
    timings.Add(scene, std::chrono::microseconds(750));
    EXPECT_EQ(timings.Runs(static_cast<std::size_t>(scene)), 2);
    EXPECT_DOUBLE_EQ(timings.CpuMs(static_cast<std::size_t>(scene)), 1.0);
    EXPECT_EQ(timings.StageOf(static_cast<std::size_t>(shadow)), FrameProfile::Stage::Shadow);
    timings.Reset();
    EXPECT_EQ(timings.Runs(static_cast<std::size_t>(scene)), 0);
    EXPECT_EQ(timings.Count(), 2u);                // a new frame keeps the slots
    timings.Add(-1, std::chrono::microseconds(5)); // an untimed pass is ignored
}

TEST(FrameProfile, PassTimingsShareTheLastSlotPastTheLimit) {
    FrameProfile::PassTimings timings;
    for (std::size_t i = 0; i < FrameProfile::kMaxPassKinds; ++i) {
        timings.SlotFor("kind" + std::to_string(i), FrameProfile::Stage::Scene);
    }
    EXPECT_EQ(timings.SlotFor("one-too-many", FrameProfile::Stage::Scene),
              static_cast<int>(FrameProfile::kMaxPassKinds - 1));
    EXPECT_EQ(timings.Count(), FrameProfile::kMaxPassKinds);
}

TEST(FrameProfile, JsonListsPassesWithNullGpuUntilValid) {
    FrameProfile::Sample sample;
    sample.passes.push_back({"scene", FrameProfile::Stage::Scene, 3, 1.25, 2.5});
    FrameProfile::Totals totals;
    std::string json = FrameProfile::ToJson(sample, totals);
    EXPECT_NE(json.find(R"("passes":[{"kind":"scene","stage":"scene","count":3,"cpu":1.250,"gpu":null}])"),
              std::string::npos)
        << json;
    sample.gpuValid = true;
    json = FrameProfile::ToJson(sample, totals);
    EXPECT_NE(json.find(R"("gpu":2.500})"), std::string::npos) << json;
}
