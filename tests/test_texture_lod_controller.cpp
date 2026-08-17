#include <gtest/gtest.h>

#include <vector>

#include "Auxiliary_Modules/TextureLODController.h"
#include "Auxiliary_Modules/TextureImage2D.h"
#include "Auxiliary_Modules/WebResourceFetcher.h"
#include "QualitySettings.h"

namespace {

struct DeferredDownload {
    std::string path;
    std::function<void(bool)> callback;
};

class TextureLODControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        deferredDownloads_.clear();
        WebResourceFetcher::SetTestDownloadHandler(
            [this](const std::string&, const std::string& virtualPath, std::function<void(bool)> callback) {
                deferredDownloads_.push_back({virtualPath, std::move(callback)});
            });
        TextureLoadingQueue::GetInstance().ResetForTests();
        TextureLoadingQueue::GetInstance().SetMaxConcurrentLoads(4);
        g_qualityPreset = 2;
        g_isMobileWeb = false;
        controller_.ResetForTests();
        texture_ = TextureImage2D{};
        controller_.Configure(texture_,
                              "resource/textures_low/Earth_Day_Diffuse_Low.dds",
                              "resource/textures_mid/Earth_Day_Diffuse_Mid.dds",
                              "resource/textures/Earth_Day_Diffuse.dds",
                              "Earth_Day_Diffuse",
                              TextureLoadCategory::Planet);
    }

    void TearDown() override {
        WebResourceFetcher::ClearTestDownloadHandler();
        TextureLoadingQueue::GetInstance().ResetForTests();
        g_qualityPreset = 2;
    }

    void CompleteDownload(std::size_t index, bool success) {
        ASSERT_LT(index, deferredDownloads_.size());
        auto callback = std::move(deferredDownloads_[index].callback);
        deferredDownloads_.erase(deferredDownloads_.begin() + static_cast<std::ptrdiff_t>(index));
        callback(success);
    }

    void TickNear(float distance, float threshold = 50.0f) {
        const glm::vec3 object{0.0f, 0.0f, 0.0f};
        const glm::vec3 camera{distance, 0.0f, 0.0f};
        controller_.Update(camera, object, threshold);
        TextureLoadingQueue::GetInstance().ProcessQueue();
    }

    std::vector<DeferredDownload> deferredDownloads_;
    TextureImage2D texture_;
    TextureLODController controller_;
};

} // namespace

TEST_F(TextureLODControllerTest, MediumPresetQueuesMidNeverHigh) {
    g_qualityPreset = 1;
    TickNear(20.0f);

    ASSERT_EQ(deferredDownloads_.size(), 1u);
    EXPECT_NE(deferredDownloads_[0].path.find("textures_mid"), std::string::npos);
    EXPECT_EQ(controller_.GetInFlightTier(), TextureLodTier::Mid);

    CompleteDownload(0, true);
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Mid);

    // Closer still must not request full-res on medium.
    TickNear(5.0f);
    EXPECT_TRUE(deferredDownloads_.empty());
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Mid);
}

TEST_F(TextureLODControllerTest, FullPresetStagesMidThenHigh) {
    g_qualityPreset = 2;
    TickNear(20.0f);
    ASSERT_EQ(deferredDownloads_.size(), 1u);
    EXPECT_NE(deferredDownloads_[0].path.find("textures_mid"), std::string::npos);
    CompleteDownload(0, true);
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Mid);

    TickNear(10.0f); // < 0.5 * 50
    ASSERT_EQ(deferredDownloads_.size(), 1u);
    EXPECT_NE(deferredDownloads_[0].path.find("textures/Earth"), std::string::npos);
    CompleteDownload(0, true);
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::High);
}

TEST_F(TextureLODControllerTest, DowngradesHighToMidToLow) {
    g_qualityPreset = 2;
    TickNear(10.0f);
    CompleteDownload(0, true); // mid
    TickNear(10.0f);
    CompleteDownload(0, true); // high
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::High);

    TickNear(60.0f); // > T → high to mid
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Mid);

    TickNear(120.0f); // > 2T → mid to low
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Low);
}

TEST_F(TextureLODControllerTest, CancelsHighUpgradeWhenCameraRetreats) {
    g_qualityPreset = 2;
    TickNear(10.0f);
    CompleteDownload(0, true); // mid
    TickNear(10.0f);
    ASSERT_EQ(deferredDownloads_.size(), 1u);
    EXPECT_EQ(controller_.GetInFlightTier(), TextureLodTier::High);

    TickNear(50.0f); // > 0.9 * T cancels high in-flight
    EXPECT_EQ(controller_.GetInFlightTier(), TextureLodTier::Low);
    CompleteDownload(0, true); // late completion suppressed by generation / cancel
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Mid);
}

TEST_F(TextureLODControllerTest, LowPresetNeverUpgrades) {
    g_qualityPreset = 0;
    TextureLoadingQueue::GetInstance().SetMaxConcurrentLoads(0);
    TickNear(5.0f);
    EXPECT_TRUE(deferredDownloads_.empty());
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Low);
}

TEST_F(TextureLODControllerTest, QualityCapDowngradesResidentHigh) {
    g_qualityPreset = 2;
    TickNear(10.0f);
    CompleteDownload(0, true);
    TickNear(10.0f);
    CompleteDownload(0, true);
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::High);

    g_qualityPreset = 1;
    TickNear(10.0f);
    EXPECT_EQ(controller_.GetResidentTier(), TextureLodTier::Mid);
}
