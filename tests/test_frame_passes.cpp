#include <gtest/gtest.h>

#include "FramePasses.h"

#include <string>
#include <vector>

namespace {

// Records every hook call as a short token, e.g. "S1" = shadow pass of component 1,
// "C0:1" = view 0 draws component 1.
struct Recorder {
    std::vector<std::string> log;
    int updates = 0;
    int shadows = 0;
    int componentDraws = 0;

    FramePasses::Hooks Hooks(std::size_t components) {
        FramePasses::Hooks hooks;
        hooks.updateScene = [this, components]() {
            ++updates;
            log.push_back("U");
            return components;
        };
        hooks.beginView = [this](int view) { log.push_back("V" + std::to_string(view)); };
        hooks.background = [this](int view) { log.push_back("B" + std::to_string(view)); };
        hooks.shadowPass = [this](std::size_t c) {
            ++shadows;
            log.push_back("S" + std::to_string(c));
        };
        hooks.drawComponent = [this](int view, std::size_t c) {
            ++componentDraws;
            log.push_back("C" + std::to_string(view) + ":" + std::to_string(c));
        };
        hooks.foreground = [this](int view) { log.push_back("F" + std::to_string(view)); };
        return hooks;
    }

    int IndexOf(const std::string& token) const {
        for (std::size_t i = 0; i < log.size(); ++i) {
            if (log[i] == token) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

} // namespace

TEST(FramePasses, XrFrameSimulatesOnceAndRendersEachShadowMapOnce) {
    Recorder recorder;
    FramePasses::Run(2, recorder.Hooks(4));

    EXPECT_EQ(recorder.updates, 1);
    EXPECT_EQ(recorder.shadows, 4);
    EXPECT_EQ(recorder.componentDraws, 8);
    EXPECT_EQ(recorder.log.front(), "U");
}

TEST(FramePasses, MonoFrameMatchesTheOldSinglePassOrder) {
    Recorder recorder;
    FramePasses::Run(1, recorder.Hooks(2));

    const std::vector<std::string> expected = {"U",  "V0", "B0", "S0", "V0", "C0:0",
                                               "S1", "V0", "C0:1", "V0", "F0"};
    EXPECT_EQ(recorder.log, expected);
}

TEST(FramePasses, ShadowMapIsConsumedByEveryViewBeforeTheNextOneOverwritesIt) {
    // One shadow FBO is shared by all components: every view must draw component N after
    // S(N) and before S(N+1).
    Recorder recorder;
    FramePasses::Run(2, recorder.Hooks(3));

    for (int c = 0; c < 3; ++c) {
        const int shadow = recorder.IndexOf("S" + std::to_string(c));
        const int nextShadow = c + 1 < 3 ? recorder.IndexOf("S" + std::to_string(c + 1))
                                         : static_cast<int>(recorder.log.size());
        for (int view = 0; view < 2; ++view) {
            const int draw = recorder.IndexOf("C" + std::to_string(view) + ":" + std::to_string(c));
            EXPECT_GT(draw, shadow) << "view " << view << " component " << c;
            EXPECT_LT(draw, nextShadow) << "view " << view << " component " << c;
        }
    }
    // Backgrounds come before any component and foregrounds after all of them.
    EXPECT_LT(recorder.IndexOf("B1"), recorder.IndexOf("S0"));
    EXPECT_GT(recorder.IndexOf("F0"), recorder.IndexOf("C1:2"));
}

TEST(FramePasses, NoViewsStillSimulates) {
    // An XR frame whose eye count has not arrived yet: the clock and LOD keep running.
    Recorder recorder;
    FramePasses::Run(0, recorder.Hooks(5));

    EXPECT_EQ(recorder.updates, 1);
    EXPECT_EQ(recorder.shadows, 0);
    EXPECT_EQ(recorder.componentDraws, 0);
}
