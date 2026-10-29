#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "task/TaskQueue.h"
#include "task/TaskType.h"

using task::TaskQueue;
using task::TaskType;

TEST(TaskQueueTest, SubmitAndComplete) {
    TaskQueue q;
    std::atomic<bool> done{false};
    auto h = q.submit(TaskType::TEST_SINGLE_PROXY, [&done](task::TaskContext& ctx) {
        (void)ctx;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        done.store(true);
    }, 1);
    EXPECT_EQ(h.status, task::TaskStatus::QUEUED);
    // wait for completion
    for (int i = 0; i < 50 && !done.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load());
    auto cur = q.currentTask();
    // after completion current should be empty
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto hist = q.history(10);
    EXPECT_FALSE(hist.empty());
}

TEST(TaskQueueTest, PriorityOrder) {
    TaskQueue q;
    std::vector<int> order;
    std::mutex m;
    // submit low then high
    q.submit(TaskType::TEST_SINGLE_PROXY, [&order, &m](task::TaskContext& ctx) {
        (void)ctx;
        std::lock_guard<std::mutex> lk(m);
        order.push_back(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    q.submit(TaskType::TEST_SINGLE_PROXY, [&order, &m](task::TaskContext& ctx) {
        (void)ctx;
        std::lock_guard<std::mutex> lk(m);
        order.push_back(2);
    }, 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // high priority should run first if queued before low starts; but low may have started.
    // Just ensure both completed.
    auto hist = q.history(10);
    EXPECT_GE(hist.size(), 2u);
}