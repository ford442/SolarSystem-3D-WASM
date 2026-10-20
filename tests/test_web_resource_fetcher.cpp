#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "assets/WebResourceFetcher.h"

namespace {

struct StartedTransfer {
    std::string url;
    std::string virtualPath;
    std::function<void(bool)> complete;
};

class WebResourceFetcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() / "solarsystem_web_resource_fetcher_test";
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        WebResourceFetcher::SetTestDownloadHandler(
            [this](const std::string& url, const std::string& virtualPath, std::function<void(bool)> complete) {
                started_.push_back({url, virtualPath, std::move(complete)});
            });
    }

    void TearDown() override {
        WebResourceFetcher::ClearTestDownloadHandler();
        std::filesystem::remove_all(root_);
    }

    std::string PathFor(const std::string& name) const { return (root_ / name).string(); }

    // Moved out before the call: completing may start another transfer, and the push_back
    // could otherwise reallocate the std::function that is running.
    void Complete(std::size_t index, bool success) {
        ASSERT_LT(index, started_.size());
        auto complete = std::move(started_[index].complete);
        complete(success);
    }

    std::filesystem::path root_;
    std::vector<StartedTransfer> started_;
};

} // namespace

TEST_F(WebResourceFetcherTest, ResidentFileIsStillFetched) {
    // The preloaded tier is 4x4 placeholders; the deployed copy is what replaces them.
    const std::string path = PathFor("PositiveX.dds");
    std::ofstream(path) << "placeholder";

    std::vector<bool> results;
    WebResourceFetcher::DownloadFile("resource/textures_low/Main_SkyBox/PositiveX.dds", path,
                                     [&](bool success) { results.push_back(success); });
    ASSERT_EQ(started_.size(), 1u);
    Complete(0, false);
    EXPECT_EQ(results, (std::vector<bool>{false})) << "DownloadFile reports the transfer's own result";
}

TEST_F(WebResourceFetcherTest, FallbackKeepsTheResidentCopyOnlyAfterEveryUrlFails) {
    const std::string path = PathFor("NegativeY.dds");
    std::ofstream(path) << "placeholder";
    std::vector<WebResourceFetcher::FetchOutcome> outcomes;

    WebResourceFetcher::DownloadWithFallback({"low/NegativeY.dds", "high/NegativeY.dds"}, path,
                                             [&](WebResourceFetcher::FetchOutcome outcome) { outcomes.push_back(outcome); });
    ASSERT_EQ(started_.size(), 1u);
    EXPECT_EQ(started_[0].url, "low/NegativeY.dds");
    Complete(0, false);

    ASSERT_EQ(started_.size(), 2u) << "the second URL is tried before settling for the resident copy";
    EXPECT_EQ(started_[1].url, "high/NegativeY.dds");
    EXPECT_TRUE(outcomes.empty());
    Complete(1, false);

    EXPECT_EQ(outcomes, (std::vector<WebResourceFetcher::FetchOutcome>{WebResourceFetcher::FetchOutcome::KeptResident}));
}

TEST_F(WebResourceFetcherTest, FallbackReportsDownloadedFromTheFirstUrlThatArrives) {
    const std::string path = PathFor("PositiveZ.dds");
    std::ofstream(path) << "placeholder";
    std::vector<WebResourceFetcher::FetchOutcome> outcomes;

    WebResourceFetcher::DownloadWithFallback({"low/PositiveZ.dds", "high/PositiveZ.dds"}, path,
                                             [&](WebResourceFetcher::FetchOutcome outcome) { outcomes.push_back(outcome); });
    Complete(0, false);
    ASSERT_EQ(started_.size(), 2u);
    Complete(1, true);

    EXPECT_EQ(outcomes, (std::vector<WebResourceFetcher::FetchOutcome>{WebResourceFetcher::FetchOutcome::Downloaded}));
}

TEST_F(WebResourceFetcherTest, FallbackFailsWhenNothingIsResident) {
    const std::string path = PathFor("phobos.obj");
    std::vector<WebResourceFetcher::FetchOutcome> outcomes;

    WebResourceFetcher::DownloadWithFallback({"resource/models/phobos.obj"}, path,
                                             [&](WebResourceFetcher::FetchOutcome outcome) { outcomes.push_back(outcome); });
    ASSERT_EQ(started_.size(), 1u);
    Complete(0, false);

    EXPECT_EQ(outcomes, (std::vector<WebResourceFetcher::FetchOutcome>{WebResourceFetcher::FetchOutcome::Failed}));
}

TEST_F(WebResourceFetcherTest, DuplicateRequestsShareOneTransferAndItsResult) {
    const std::string path = PathFor("mars.dds");
    std::vector<bool> results;

    WebResourceFetcher::DownloadFile("mars.dds", path, [&](bool success) { results.push_back(success); });
    WebResourceFetcher::DownloadFile("mars.dds", path, [&](bool success) { results.push_back(success); });
    WebResourceFetcher::DownloadFile("mars.dds", path, [&](bool success) { results.push_back(success); });

    ASSERT_EQ(started_.size(), 1u);
    EXPECT_TRUE(results.empty()) << "a duplicate must wait for the transfer, not be told it failed";

    Complete(0, true);
    EXPECT_EQ(results, (std::vector<bool>{true, true, true}));
}

TEST_F(WebResourceFetcherTest, FailureReachesEveryWaiterAndAllowsARetry) {
    const std::string path = PathFor("deimos.obj");
    std::vector<bool> results;

    WebResourceFetcher::DownloadFile("a/deimos.obj", path, [&](bool success) { results.push_back(success); });
    WebResourceFetcher::DownloadFile("a/deimos.obj", path, [&](bool success) { results.push_back(success); });
    ASSERT_EQ(started_.size(), 1u);
    Complete(0, false);
    EXPECT_EQ(results, (std::vector<bool>{false, false}));

    // A fallback URL into the same virtual path is a new transfer, not a joined one.
    WebResourceFetcher::DownloadFile("b/deimos.obj", path, [&](bool success) { results.push_back(success); });
    ASSERT_EQ(started_.size(), 2u);
    EXPECT_EQ(started_[1].url, "b/deimos.obj");
    Complete(1, true);
    EXPECT_EQ(results, (std::vector<bool>{false, false, true}));
}

TEST_F(WebResourceFetcherTest, CallbackMayRequestTheSamePathAgain) {
    // LoadCoreResources retries a failed face from inside the completion callback.
    const std::string path = PathFor("NegativeZ.dds");
    bool retryResult = false;

    WebResourceFetcher::DownloadFile("low/NegativeZ.dds", path, [&](bool success) {
        ASSERT_FALSE(success);
        WebResourceFetcher::DownloadFile("high/NegativeZ.dds", path, [&](bool ok) { retryResult = ok; });
    });
    ASSERT_EQ(started_.size(), 1u);
    Complete(0, false);

    ASSERT_EQ(started_.size(), 2u);
    Complete(1, true);
    EXPECT_TRUE(retryResult);
}
