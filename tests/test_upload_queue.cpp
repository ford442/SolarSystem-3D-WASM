#include <gtest/gtest.h>

#include "assets/UploadQueue.h"

#include <string>
#include <vector>

namespace {

class UploadQueueTest : public ::testing::Test {
protected:
    void SetUp() override { UploadQueue::Get().Clear(); }
    void TearDown() override { UploadQueue::Get().Clear(); }

    /** A task that uploads `bytes` in one step and records its name when it runs. */
    UploadQueue::Task Once(const std::string& name, std::size_t bytes) {
        return [this, name, bytes](std::size_t) {
            ran.push_back(name);
            return UploadQueue::Step{bytes, true};
        };
    }

    std::vector<std::string> ran;
};

} // namespace

TEST_F(UploadQueueTest, RunsReadyTasksOldestFirstWithinTheBudget) {
    auto& queue = UploadQueue::Get();
    queue.Push("a", nullptr, Once("a", 10));
    queue.Push("b", nullptr, Once("b", 10));
    queue.Push("c", nullptr, Once("c", 10));

    const UploadQueue::PumpResult first = queue.Pump(15);
    EXPECT_EQ(ran, (std::vector<std::string>{"a", "b"})); // b started under budget, so it runs
    EXPECT_EQ(first.bytes, 20u);
    EXPECT_EQ(first.finished, 2);
    EXPECT_EQ(queue.LastPumpBytes(), 20u);
    EXPECT_EQ(queue.Pending(), 1u);

    queue.Pump(15);
    EXPECT_EQ(ran.back(), "c");
    EXPECT_EQ(queue.Pending(), 0u);
}

TEST_F(UploadQueueTest, AnOversizedItemStillLandsAloneInItsFrame) {
    auto& queue = UploadQueue::Get();
    queue.Push("huge", nullptr, Once("huge", 1000));
    queue.Push("small", nullptr, Once("small", 1));
    queue.Pump(10);
    EXPECT_EQ(ran, (std::vector<std::string>{"huge"}));
    queue.Pump(10);
    EXPECT_EQ(ran.back(), "small");
}

TEST_F(UploadQueueTest, AWaitingJobDoesNotBlockTheTasksBehindIt) {
    auto& queue = UploadQueue::Get();
    bool decoded = false;
    queue.Push("slow", [&decoded] { return decoded; }, Once("slow", 1));
    queue.Push("fast", nullptr, Once("fast", 1));
    queue.Pump(100);
    EXPECT_EQ(ran, (std::vector<std::string>{"fast"}));
    EXPECT_EQ(queue.Pending(), 1u);
    decoded = true;
    queue.Pump(100);
    EXPECT_EQ(ran.back(), "slow");
}

TEST_F(UploadQueueTest, AMultiStepTaskContinuesOverFramesInOrder) {
    auto& queue = UploadQueue::Get();
    int bandsLeft = 3;
    queue.Push("banded", nullptr, [&](std::size_t budget) {
        ran.push_back("band");
        --bandsLeft;
        return UploadQueue::Step{budget, bandsLeft == 0};
    });
    queue.Push("next", nullptr, Once("next", 1));
    for (int frame = 0; frame < 3; ++frame) {
        queue.Pump(8);
    }
    EXPECT_EQ(ran, (std::vector<std::string>{"band", "band", "band"})); // "next" waits its turn
    queue.Pump(8);
    EXPECT_EQ(ran.back(), "next");
}

TEST_F(UploadQueueTest, AStepMayPushOrClear) {
    auto& queue = UploadQueue::Get();
    queue.Push("parent", nullptr, [&](std::size_t) {
        ran.push_back("parent");
        UploadQueue::Get().Push("child", nullptr, Once("child", 1));
        return UploadQueue::Step{1, true};
    });
    queue.Pump(100);
    EXPECT_EQ(ran, (std::vector<std::string>{"parent", "child"}));

    queue.Push("clearer", nullptr, [&](std::size_t) {
        UploadQueue::Get().Clear();
        return UploadQueue::Step{1, true};
    });
    queue.Push("dropped", nullptr, Once("dropped", 1));
    queue.Pump(100);
    EXPECT_EQ(queue.Pending(), 0u);
    EXPECT_EQ(ran.back(), "child");
}
