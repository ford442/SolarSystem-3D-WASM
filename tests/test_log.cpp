#include <gtest/gtest.h>

#include "core/Log.h"

#include <string>
#include <utility>
#include <vector>

namespace {

class LogCapture {
public:
    LogCapture() : _previous(Log::CurrentLevel()) {
        Log::SetSinkForTesting([this](Log::Level level, const std::string& line) { lines.emplace_back(level, line); });
    }
    ~LogCapture() {
        Log::SetSinkForTesting({});
        Log::SetLevel(_previous);
    }

    std::vector<std::pair<Log::Level, std::string>> lines;

private:
    Log::Level _previous;
};

} // namespace

TEST(Log, ParsesLevelNamesCaseInsensitively) {
    EXPECT_EQ(Log::ParseLevel("debug"), Log::Level::Debug);
    EXPECT_EQ(Log::ParseLevel("DEBUG"), Log::Level::Debug);
    EXPECT_EQ(Log::ParseLevel("Info"), Log::Level::Info);
    EXPECT_EQ(Log::ParseLevel("warn"), Log::Level::Warn);
    EXPECT_EQ(Log::ParseLevel("warning"), Log::Level::Warn);
    EXPECT_EQ(Log::ParseLevel("error"), Log::Level::Error);
    EXPECT_EQ(Log::ParseLevel("off"), Log::Level::Off);
    EXPECT_EQ(Log::ParseLevel("none"), Log::Level::Off);
    EXPECT_EQ(Log::ParseLevel("verbose"), std::nullopt);
    EXPECT_EQ(Log::ParseLevel(""), std::nullopt);
}

TEST(Log, PrefixesTheCategoryInBrackets) {
    // Smoke tests and CI grep for "[StagedLoading] ..." / "[ShaderSelfTest] ...".
    EXPECT_EQ(Log::FormatLine("StagedLoading", "Loaded manifest v3"), "[StagedLoading] Loaded manifest v3");
}

TEST(Log, DropsLinesBelowTheLevelWithoutFormattingThem) {
    LogCapture capture;
    Log::SetLevel(Log::Level::Warn);
    int formatted = 0;
    const auto count = [&formatted] { return ++formatted; };
    LOG_DEBUG("Test", "debug %d", count());
    LOG_INFO("Test", "info %d", count());
    LOG_WARN("Test", "warn %d", count());
    LOG_ERROR("Test", "error %s", "x");
    EXPECT_EQ(formatted, 1); // arguments of a disabled line are not even evaluated
    ASSERT_EQ(capture.lines.size(), 2u);
    EXPECT_EQ(capture.lines[0].first, Log::Level::Warn);
    EXPECT_EQ(capture.lines[0].second, "[Test] warn 1");
    EXPECT_EQ(capture.lines[1].first, Log::Level::Error);
    EXPECT_EQ(capture.lines[1].second, "[Test] error x");
}

TEST(Log, OffSilencesEverything) {
    LogCapture capture;
    Log::SetLevel(Log::Level::Off);
    LOG_ERROR("Test", "error");
    EXPECT_TRUE(capture.lines.empty());
}

TEST(Log, FormatsLinesLongerThanTheStackBuffer) {
    LogCapture capture;
    Log::SetLevel(Log::Level::Debug);
    const std::string longText(2000, 'x');
    LOG_DEBUG("Test", "%s|%d", longText.c_str(), 7);
    ASSERT_EQ(capture.lines.size(), 1u);
    EXPECT_EQ(capture.lines[0].second, "[Test] " + longText + "|7");
}
