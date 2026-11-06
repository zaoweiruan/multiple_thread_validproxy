// tests/test_task_type.cpp — taskTypeToString label mapping
// (spec 2026-11-03 v1.2 §3.1 / §5.2 T6-T7)
#include <gtest/gtest.h>

#include <string>

#include "task/TaskType.h"

namespace {

const task::TaskType kAllTypes[] = {
    task::TaskType::NONE,
    task::TaskType::UPDATE_SUBSCRIPTION,
    task::TaskType::UPDATE_ALL,
    task::TaskType::TEST_SUBSCRIPTION,
    task::TaskType::TEST_SINGLE_PROXY,
    task::TaskType::TEST_ALL,
    task::TaskType::TEST_BATCH,
    task::TaskType::TEST_ONLINE,
    task::TaskType::FIND_FIRST,
    task::TaskType::FIND_BEST,
    task::TaskType::FIND_BY_INDEX,
    task::TaskType::DEDUP,
    task::TaskType::SYNC_DB,
    task::TaskType::EXPORT_SHARE_LINKS,
    task::TaskType::RESOLVE_REGION,
    task::TaskType::RESOLVE_SINGLE_REGION,
    task::TaskType::GENERATE_CONFIG,
    task::TaskType::AUTO_TASK,
    task::TaskType::PROBE_POOL_MEMBERS,
    task::TaskType::PROBE_MEMBER,
};

// Enum-name strings must match the enumerator spellings in task/TaskType.h.
const char* enumName(task::TaskType t) {
    switch (t) {
    case task::TaskType::NONE: return "NONE";
    case task::TaskType::UPDATE_SUBSCRIPTION: return "UPDATE_SUBSCRIPTION";
    case task::TaskType::UPDATE_ALL: return "UPDATE_ALL";
    case task::TaskType::TEST_SUBSCRIPTION: return "TEST_SUBSCRIPTION";
    case task::TaskType::TEST_SINGLE_PROXY: return "TEST_SINGLE_PROXY";
    case task::TaskType::TEST_ALL: return "TEST_ALL";
    case task::TaskType::TEST_BATCH: return "TEST_BATCH";
    case task::TaskType::TEST_ONLINE: return "TEST_ONLINE";
    case task::TaskType::FIND_FIRST: return "FIND_FIRST";
    case task::TaskType::FIND_BEST: return "FIND_BEST";
    case task::TaskType::FIND_BY_INDEX: return "FIND_BY_INDEX";
    case task::TaskType::DEDUP: return "DEDUP";
    case task::TaskType::SYNC_DB: return "SYNC_DB";
    case task::TaskType::EXPORT_SHARE_LINKS: return "EXPORT_SHARE_LINKS";
    case task::TaskType::RESOLVE_REGION: return "RESOLVE_REGION";
    case task::TaskType::RESOLVE_SINGLE_REGION: return "RESOLVE_SINGLE_REGION";
    case task::TaskType::GENERATE_CONFIG: return "GENERATE_CONFIG";
    case task::TaskType::AUTO_TASK: return "AUTO_TASK";
    case task::TaskType::PROBE_POOL_MEMBERS: return "PROBE_POOL_MEMBERS";
    case task::TaskType::PROBE_MEMBER: return "PROBE_MEMBER";
    }
    return "";
}

} // namespace

TEST(TaskTypeLabelTest, CoversEveryEnumerator) {
    for (const task::TaskType t : kAllTypes) {
        const std::string label = task::taskTypeToString(t);
        EXPECT_FALSE(label.empty()) << "empty label for enumerator " << enumName(t);
        // Spec §3.1: "<中文描述> (<TaskType 枚举名>)" — the English name keeps the
        // panel in sync with logs and code.
        EXPECT_NE(label.find(enumName(t)), std::string::npos)
            << "label must embed the English TaskType name: " << label;
        EXPECT_EQ(label.find("未知任务 (UNKNOWN)"), std::string::npos)
            << "known enumerator fell through to the fallback: " << label;
    }
}

TEST(TaskTypeLabelTest, TestAllLabelMatchesSpecExample) {
    EXPECT_STREQ(task::taskTypeToString(task::TaskType::TEST_ALL), "批量测试 (TEST_ALL)");
}

TEST(TaskTypeLabelTest, UnknownEnumeratorFallsBack) {
    EXPECT_STREQ(task::taskTypeToString(static_cast<task::TaskType>(9999)),
                 "未知任务 (UNKNOWN)");
}
