// tests/test_task_queue_report.cpp — TaskQueue 完成 sink(handle, stats, updateMethods) + stats/updateMethods 传递
// (task-report-format Task 2; brief: sink 三参 + ctx.updateMethods 传至 sink；无 lines/日志捕获)
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

#include "task/TaskQueue.h"
#include "task/TaskType.h"

namespace {

// Fixture providing a live TaskQueue per test (TEST_F per project brief).
// The sink writes into fixture members; q.stop(true) joins the worker, so by
// the time an assertion reads them the sink has completed (happens-before via
// the join) — no data race.
class TaskQueueReportTest : public ::testing::Test {
protected:
    task::TaskQueue q;
    task::TaskHandle gotH;
    task::TaskStats gotSt;
    std::string gotUm;
    int failedSinkCalls = 0;
};

// The brief's literal test submits a task then immediately q.stop(true), which
// races the worker's dequeue: stop() drains any task still in the queue_ as
// CANCELLED (it never runs, the sink never fires) — a flaky test. To reliably
// verify real capture the test waits until the task body has STARTED (an atomic
// set as its first statement), guaranteeing the worker already popped it from
// the queue so stop() cannot drain it. stop(true) then joins, and the sink has
// fired before any assertion reads the fixture members.
TEST_F(TaskQueueReportTest, SinkCalledWithHandleStatsAndUpdateMethods) {
    q.setTaskFinishedSink([&](const task::TaskHandle& h,
                              const task::TaskStats& s,
                              const std::string& um) {
        gotH = h;
        gotSt = s;
        gotUm = um;
    });
    std::atomic<bool> started{false};
    q.submit(task::TaskType::DEDUP, [&started](task::TaskContext& ctx) {
        started.store(true);
        ctx.addStats(task::TaskStats{10, 8, 1, 1});
        ctx.updateMethods = "direct";
    });
    for (int i = 0; i < 200 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(started.load());   // task dequeued and running — stop won't drain it
    q.stop(true);

    EXPECT_EQ(gotH.type, task::TaskType::DEDUP);
    EXPECT_EQ(gotSt.total, 10);
    EXPECT_EQ(gotSt.success, 8);
    EXPECT_EQ(gotSt.failed, 1);
    EXPECT_EQ(gotSt.skipped, 1);
    EXPECT_EQ(gotUm, "direct");    // ctx.updateMethods propagated to the sink
}

TEST_F(TaskQueueReportTest, FailedTaskStillEmitsSink) {
    q.setTaskFinishedSink([&](const task::TaskHandle& h,
                              const task::TaskStats&,
                              const std::string&) {
        if (h.status == task::TaskStatus::FAILED) ++failedSinkCalls;
    });
    std::atomic<bool> started{false};
    q.submit(task::TaskType::DEDUP, [&started](task::TaskContext& ctx) {
        (void)ctx;
        started.store(true);
        throw std::runtime_error("x");
    });
    for (int i = 0; i < 200 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(started.load());   // task dequeued and running — stop won't drain it
    q.stop(true);

    EXPECT_EQ(failedSinkCalls, 1);
}

} // namespace
