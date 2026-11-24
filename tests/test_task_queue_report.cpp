// tests/test_task_queue_report.cpp — TaskQueue 完成 sink(含stats) + 任务窗口日志捕获
// (task-report-hover Task 3; brief: TaskQueue 完成 sink + 作用域日志捕获 + stats 传递)
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Logger.h"
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
    std::vector<task::LogLine> got;
    task::TaskStats gotSt;
    int failedSinkCalls = 0;
};

// The brief's literal test submits a task then immediately q.stop(true), which
// races the worker's dequeue: stop() drains any task still in the queue_ as
// CANCELLED (it never runs, the sink never fires) — a flaky test. To reliably
// verify real capture the test waits until the task body has STARTED (an atomic
// set as its first statement), guaranteeing the worker already popped it from
// the queue so stop() cannot drain it. stop(true) then joins, and the sink has
// fired before any assertion reads the fixture members.
TEST_F(TaskQueueReportTest, SinkCalledWithHandleLinesAndStats) {
    q.setTaskFinishedSink([&](const task::TaskHandle& h,
                              const std::vector<task::LogLine>& l,
                              const task::TaskStats& s) {
        gotH = h;
        got = l;
        gotSt = s;
    });
    std::atomic<bool> started{false};
    q.submit(task::TaskType::DEDUP, [&started](task::TaskContext& ctx) {
        started.store(true);
        Logger::write("dedup line", LogLevel::INFO);
        ctx.addStats(task::TaskStats{10, 8, 1, 1});
    });
    for (int i = 0; i < 200 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(started.load());   // task dequeued and running — stop won't drain it
    q.stop(true);

    EXPECT_EQ(gotH.type, task::TaskType::DEDUP);
    EXPECT_FALSE(got.empty());
    EXPECT_NE(got[0].line.find("dedup line"), std::string::npos);
    EXPECT_EQ(gotSt.total, 10);
    EXPECT_EQ(gotSt.success, 8);
}

TEST_F(TaskQueueReportTest, FailedTaskStillEmitsSink) {
    q.setTaskFinishedSink([&](const task::TaskHandle& h,
                              const std::vector<task::LogLine>&,
                              const task::TaskStats&) {
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
