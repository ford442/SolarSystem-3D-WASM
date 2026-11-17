#include <gtest/gtest.h>

#include "core/JobSystem.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

// Each test owns the pool's size; the rest of the suite runs with it stopped (inline).
class JobSystemTest : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        Jobs::Stop();
        Jobs::Start(GetParam());
    }
    void TearDown() override { Jobs::Stop(); }
};

} // namespace

TEST_P(JobSystemTest, StartsTheRequestedWorkerCount) {
    EXPECT_EQ(Jobs::WorkerCount(), GetParam());
    EXPECT_EQ(Jobs::Threaded(), GetParam() > 0);
    EXPECT_TRUE(Jobs::IsMainThread());
}

TEST_P(JobSystemTest, FutureReturnsTheJobsValue) {
    Jobs::Future<int> answer = Jobs::Submit([] { return 6 * 7; });
    EXPECT_TRUE(answer.Valid());
    EXPECT_EQ(answer.Get(), 42);
}

TEST_P(JobSystemTest, MoveOnlyResultsComeBackWhole) {
    auto future = Jobs::Submit([] { return std::vector<std::string>(1000, "texel"); });
    const std::vector<std::string> payload = future.Get();
    ASSERT_EQ(payload.size(), 1000u);
    EXPECT_EQ(payload.back(), "texel");
}

TEST_P(JobSystemTest, AnExceptionReachesTheFutureNotTheWorker) {
    Jobs::Future<int> failing = Jobs::Submit([]() -> int { throw std::runtime_error("cannot open a.dds"); });
    try {
        failing.Get();
        FAIL() << "Get() should rethrow";
    } catch (const std::runtime_error& error) {
        EXPECT_STREQ(error.what(), "cannot open a.dds");
    }
    // The pool survives a throwing job.
    EXPECT_EQ(Jobs::Submit([] { return 1; }).Get(), 1);
}

TEST_P(JobSystemTest, DrainWaitsForEveryJob) {
    std::atomic<int> done{0};
    for (int i = 0; i < 64; ++i) {
        Jobs::Submit([&done] {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            done.fetch_add(1);
        });
    }
    Jobs::Drain();
    EXPECT_EQ(done.load(), 64);
    EXPECT_EQ(Jobs::InFlight(), 0u);
    EXPECT_EQ(Jobs::QueueDepth(), 0u);
}

TEST_P(JobSystemTest, OneWorkerRunsJobsInSubmissionOrder) {
    // FIFO: with one consumer, jobs run in the order they were submitted. The main thread is
    // a second consumer whenever it waits in Get()/Drain(), so this waits on a counter instead.
    if (GetParam() > 1) {
        GTEST_SKIP() << "ordering is only defined for a single consumer";
    }
    std::mutex mutex;
    std::vector<int> order;
    std::atomic<int> finished{0};
    for (int i = 0; i < 32; ++i) {
        Jobs::Submit([&, i] {
            std::lock_guard<std::mutex> lock(mutex);
            order.push_back(i);
            finished.fetch_add(1);
        });
    }
    while (finished.load() < 32) {
        std::this_thread::yield();
    }
    ASSERT_EQ(order.size(), 32u);
    for (int i = 0; i < 32; ++i) {
        EXPECT_EQ(order[static_cast<size_t>(i)], i);
    }
}

TEST_P(JobSystemTest, InlineModeFinishesBeforeSubmitReturns) {
    if (GetParam() != 0) {
        GTEST_SKIP() << "inline only";
    }
    bool ran = false;
    auto future = Jobs::Submit([&ran] { ran = true; });
    EXPECT_TRUE(ran);
    EXPECT_TRUE(future.Ready());
}

TEST_P(JobSystemTest, WorkersRunOffTheMainThread) {
    if (GetParam() == 0) {
        GTEST_SKIP() << "threaded only";
    }
    // Block the worker until the main thread has checked, so the main thread cannot have
    // picked the job up itself.
    std::atomic<bool> release{false};
    auto onWorker = Jobs::Submit([&release] {
        while (!release.load()) {
            std::this_thread::yield();
        }
        return Jobs::IsMainThread();
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_FALSE(onWorker.Ready());
    release = true;
    EXPECT_FALSE(onWorker.Get());
}

TEST_P(JobSystemTest, AJobMayWaitOnAnotherJob) {
    // A worker that Get()s a nested job helps run the queue instead of deadlocking, even with
    // a single worker.
    auto outer = Jobs::Submit([] {
        auto inner = Jobs::Submit([] { return 20; });
        return inner.Get() + 1;
    });
    EXPECT_EQ(outer.Get(), 21);
}

TEST_P(JobSystemTest, StopDrainsWhatIsQueued) {
    std::atomic<int> done{0};
    for (int i = 0; i < 16; ++i) {
        Jobs::Submit([&done] { done.fetch_add(1); });
    }
    Jobs::Stop();
    EXPECT_EQ(done.load(), 16);
    EXPECT_EQ(Jobs::WorkerCount(), 0);
    // After Stop, Submit runs inline.
    EXPECT_TRUE(Jobs::Submit([] {}).Ready());
}

INSTANTIATE_TEST_SUITE_P(PoolSizes, JobSystemTest, ::testing::Values(0, 1, 3),
                         [](const ::testing::TestParamInfo<int>& info) {
                             return info.param == 0 ? std::string("Inline") : std::to_string(info.param) + "Workers";
                         });
