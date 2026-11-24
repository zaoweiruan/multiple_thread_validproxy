// tests/test_task_report_integration.cpp — 任务完成 → 完成 sink → TaskReportService 落盘
// (task-report-hover Task 5; brief: AppController 接线 TaskReportService + lambda addStats
//  完成即生成 report。集成测试独立跑，不经 wx。)
//
// 全链路（不经 wx）：task::TaskQueue 完成 sink（Task 3）把 (handle, lines, stats) 交给
// task::TaskReportService::publish（Task 2），后者落盘 report_<taskId>.txt 并含统计节。
// 这是 AppController ctor 中 setTaskFinishedSink 注册的同一语义（Task 5 接线），
// 此处直接以 TaskQueue + TaskReportService 验证 sink→publish→文件 的端到端路径。
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "Logger.h"
#include "task/TaskQueue.h"
#include "task/TaskReportService.h"
#include "task/TaskType.h"
#include "test_utils.h"

namespace {

// Fixture providing a live TaskQueue + a report service bound to a fresh temp
// directory per test (TEST_F per project brief). dir_ is declared before svc_
// so the directory exists before the service captures it (same pattern as
// Task 2's fixture). q_ must outlive svc_ only in the sense that the sink
// (registered per test) reads svc_ — destruction order is reverse of
// declaration, so svc_ dies before q_, which is fine: stop(true) already
// joined the worker inside the test body.
class TaskReportIntegrationTest : public ::testing::Test {
protected:
    TempDir dir_;
    task::TaskQueue q_;
    task::TaskReportService svc_{dir_.path()};
};

// The brief's literal "submit then immediate stop(true)" races the worker's
// dequeue: stop() drains any task still in the queue_ as CANCELLED (it never
// runs, the sink never fires). To reliably verify the publish path the test
// waits until the task body has STARTED (an atomic set as its first statement),
// guaranteeing the worker already popped it from the queue so stop() cannot
// drain it. stop(true) then joins, and the sink has fired before any assertion
// reads the report file (happens-before via the join).
TEST_F(TaskReportIntegrationTest, TaskCompletionPublishesReportFileWithStats) {
    // Register the sink BEFORE submit so it is present the moment the task
    // completes (worker reads taskFinishedSink_ after the body returns).
    q_.setTaskFinishedSink([this](const task::TaskHandle& h,
                                  const std::vector<task::LogLine>& l,
                                  const task::TaskStats& s) {
        svc_.publish(h, l, s);
    });

    std::atomic<bool> started{false};
    const task::TaskHandle h = q_.submit(task::TaskType::TEST_ALL,
        [&started](task::TaskContext& ctx) {
            started.store(true);
            Logger::write("done", LogLevel::INFO);
            ctx.addStats(task::TaskStats{10, 9, 1, 0});
        });

    for (int i = 0; i < 200 && !started.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(started.load());   // task dequeued and running — stop won't drain it
    q_.stop(true);

    // publish writes logDir/report_<taskId>.txt (Task 2 semantics).
    const std::string reportPath = dir_.path() + "/report_" + h.taskId + ".txt";
    std::error_code ec;
    ASSERT_TRUE(std::filesystem::exists(reportPath, ec))
        << "report file missing: " << reportPath;

    std::ifstream in(reportPath, std::ios::in | std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(in)),
                              std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("success: 9"), std::string::npos)
        << "report content:\n" << content;
}

} // namespace
