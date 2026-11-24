// tests/test_task_stats.cpp — task::TaskStats 定义 + TaskContext::addStats 统计累积
// (task-report-hover Task 1; brief: TaskStats 结构 + TaskContext 统计累积)
#include <gtest/gtest.h>

#include "task/TaskQueue.h"
#include "task/TaskType.h"

namespace {

// Fixture providing a fresh TaskContext per test (TEST_F per project brief).
class TaskStatsTest : public ::testing::Test {
protected:
    task::TaskContext ctx;
};

TEST_F(TaskStatsTest, AccumulatesFields) {
    // 容器任务多次调用 addStats，逐字段求和。
    ctx.addStats(task::TaskStats{10, 8, 1, 1});
    ctx.addStats(task::TaskStats{5, 4, 1, 0});
    EXPECT_EQ(ctx.stats.total, 15);
    EXPECT_EQ(ctx.stats.success, 12);
    EXPECT_EQ(ctx.stats.failed, 2);
    EXPECT_EQ(ctx.stats.skipped, 1);
}

TEST_F(TaskStatsTest, DefaultsToZero) {
    // 默认构造：全 0。
    EXPECT_EQ(ctx.stats.total, 0);
    EXPECT_EQ(ctx.stats.success, 0);
    EXPECT_EQ(ctx.stats.failed, 0);
    EXPECT_EQ(ctx.stats.skipped, 0);
}

} // namespace
