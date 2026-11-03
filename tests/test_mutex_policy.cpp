#include <gtest/gtest.h>

#include "task/MutexPolicy.h"
#include "task/TaskType.h"

using task::MutexPolicy;
using task::TaskType;

TEST(MutexPolicyTest, ExportCanRunConcurrentlyWithWorker) {
    EXPECT_TRUE(MutexPolicy::canRunConcurrently(TaskType::EXPORT_SHARE_LINKS, TaskType::TEST_ALL));
    EXPECT_TRUE(MutexPolicy::canRunConcurrently(TaskType::TEST_ALL, TaskType::EXPORT_SHARE_LINKS));
}

TEST(MutexPolicyTest, WorkerTasksMutuallyExclusive) {
    EXPECT_FALSE(MutexPolicy::canRunConcurrently(TaskType::TEST_ALL, TaskType::UPDATE_ALL));
    EXPECT_FALSE(MutexPolicy::canRunConcurrently(TaskType::DEDUP, TaskType::SYNC_DB));
    EXPECT_FALSE(MutexPolicy::canRunConcurrently(TaskType::RESOLVE_REGION, TaskType::FIND_FIRST));
}

TEST(MutexPolicyTest, ProbeVsWorkerJoinOrCancel) {
    auto d1 = MutexPolicy::decide(TaskType::TEST_ONLINE, TaskType::TEST_ALL, true, false);
    EXPECT_EQ(d1, MutexPolicy::ConcurrencyDecision::JOIN_PROBE_THEN_RUN);

    auto d2 = MutexPolicy::decide(TaskType::TEST_ALL, TaskType::TEST_ONLINE, true, false);
    EXPECT_EQ(d2, MutexPolicy::ConcurrencyDecision::CANCEL_PROBE);
}

TEST(MutexPolicyTest, NoneAllowsAll) {
    EXPECT_TRUE(MutexPolicy::canRunConcurrently(TaskType::NONE, TaskType::TEST_ALL));
}