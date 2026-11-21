#include <gtest/gtest.h>

#include "core/FrameProfile.h"

#include <chrono>
#include <string>

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
                            "\"bytes\":4096", "\"frameMs\":16.500", "\"slowFrames\":0", "\"frames\":1"}) {
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
