#include <gtest/gtest.h>

#include <vector>

#include "Auxiliary_Modules/TextureLoadingQueue.h"
#include "Auxiliary_Modules/TextureImage2D.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"

namespace {

struct DeferredDownload {
    std::string path;
    std::function<void(bool)> callback;
};

class TextureLoadingQueueTest : public ::testing::Test {
protected:
    void SetUp() override {
        deferredDownloads_.clear();
        WebResourceFetcher::SetTestDownloadHandler(
            [this](const std::string&, const std::string& virtualPath, std::function<void(bool)> callback) {
                deferredDownloads_.push_back({virtualPath, std::move(callback)});
            });
        TextureLoadingQueue::GetInstance().ResetForTests();
    }

    void TearDown() override {
        WebResourceFetcher::ClearTestDownloadHandler();
        TextureLoadingQueue::GetInstance().ResetForTests();
    }

    void CompleteDownload(std::size_t index, bool success) {
        ASSERT_LT(index, deferredDownloads_.size());
        auto callback = std::move(deferredDownloads_[index].callback);
        deferredDownloads_.erase(deferredDownloads_.begin() + static_cast<std::ptrdiff_t>(index));
        callback(success);
    }

    std::vector<DeferredDownload> deferredDownloads_;
};

} // namespace

TEST_F(TextureLoadingQueueTest, DedupesDuplicatePaths) {
    auto& queue = TextureLoadingQueue::GetInstance();
    queue.SetMaxConcurrentLoads(1);

    EXPECT_TRUE(queue.QueueTextureLoad("resource/a.dds", "A", nullptr, nullptr));
    EXPECT_FALSE(queue.QueueTextureLoad("resource/a.dds", "A", nullptr, nullptr));
    EXPECT_EQ(queue.GetTotalQueued(), 1);
}

TEST_F(TextureLoadingQueueTest, LimitsConcurrentInFlightLoads) {
    auto& queue = TextureLoadingQueue::GetInstance();
    queue.SetMaxConcurrentLoads(1);

    EXPECT_TRUE(queue.QueueTextureLoad("resource/a.dds", "A", nullptr, nullptr));
    EXPECT_TRUE(queue.QueueTextureLoad("resource/b.dds", "B", nullptr, nullptr));

    EXPECT_EQ(queue.GetActiveLoadCount(), 1);
    EXPECT_EQ(deferredDownloads_.size(), 1u);
    EXPECT_EQ(deferredDownloads_[0].path, "resource/a.dds");
}

TEST_F(TextureLoadingQueueTest, CancelPendingLoadBeforeStart) {
    auto& queue = TextureLoadingQueue::GetInstance();
    queue.SetMaxConcurrentLoads(1);

    TextureImage2D firstTexture;
    bool firstCallbackInvoked = false;
    bool secondCallbackInvoked = false;

    EXPECT_TRUE(queue.QueueTextureLoad(
        "resource/a.dds", "A", &firstTexture, [&firstCallbackInvoked](bool success) {
            firstCallbackInvoked = true;
            EXPECT_TRUE(success);
        }));
    EXPECT_TRUE(queue.QueueTextureLoad(
        "resource/b.dds", "B", nullptr, [&secondCallbackInvoked](bool success) {
            secondCallbackInvoked = true;
            EXPECT_FALSE(success);
        }));

    queue.CancelLoad("resource/b.dds");
    CompleteDownload(0, true);

    EXPECT_TRUE(firstCallbackInvoked);
    EXPECT_TRUE(secondCallbackInvoked);
    EXPECT_EQ(queue.GetTotalCancelled(), 1);
}

TEST_F(TextureLoadingQueueTest, CancelInFlightLoadSuppressesSuccess) {
    auto& queue = TextureLoadingQueue::GetInstance();
    queue.SetMaxConcurrentLoads(2);

    bool callbackInvoked = false;
    EXPECT_TRUE(queue.QueueTextureLoad(
        "resource/a.dds", "A", nullptr, [&callbackInvoked](bool success) {
            callbackInvoked = true;
            EXPECT_FALSE(success);
        }));

    queue.CancelLoad("resource/a.dds");
    CompleteDownload(0, true);

    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(queue.GetTotalCancelled(), 1);
    EXPECT_EQ(queue.GetTotalCompleted(), 0);
}
