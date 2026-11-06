#pragma once

#include <string>

namespace task {

// Unified task type for all batch/long-running operations.
enum class TaskType {
    NONE,
    UPDATE_SUBSCRIPTION,   // single subscription update
    UPDATE_ALL,            // all subscriptions update
    TEST_SUBSCRIPTION,     // single subscription test
    TEST_SINGLE_PROXY,     // single proxy test
    TEST_ALL,              // all proxies test (user entry)
    TEST_BATCH,            // batch test container (internal)
    TEST_ONLINE,           // online proxy probe
    FIND_FIRST,            // find first working proxy
    FIND_BEST,             // find best proxy
    FIND_BY_INDEX,         // find by indexId
    DEDUP,                 // deduplication
    SYNC_DB,               // database sync
    EXPORT_SHARE_LINKS,    // export share links
    RESOLVE_REGION,        // region resolution (batch)
    RESOLVE_SINGLE_REGION, // single proxy region resolution
    GENERATE_CONFIG,       // config generation
    AUTO_TASK,             // auto task pipeline
    PROBE_POOL_MEMBERS,    // proxy pool member health probe (batch)
    PROBE_MEMBER,          // single pool member probe
};

enum class TaskStatus {
    PENDING,
    QUEUED,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED,
    REJECTED,
    SKIPPED,    // monitoring child only: within update interval, not touched
};

// Human-readable label for a TaskType: "<中文描述> (<TaskType 枚举名>)",
// e.g. "批量测试 (TEST_ALL)". The English enumerator is embedded on purpose so
// the monitoring panel, the logs and the code stay readable against each other.
// Every enumerator must be listed; unlisted values return "未知任务 (UNKNOWN)".
const char* taskTypeToString(TaskType t);

struct TaskHandle {
    std::string taskId;
    TaskType type = TaskType::NONE;
    TaskStatus status = TaskStatus::PENDING;
    int priority = 1; // 0=low, 1=normal, 2=high

    std::string createdAt;
    std::string startedAt;
    std::string completedAt;
    std::string error;

    // container/subtask tracking (Scheme A)
    std::string parentTaskId;
    int childTotal = 0;
    int childCompleted = 0;
    int childFailed = 0;

    // observation
    int progressCurrent = 0;
    int progressTotal = 0;
    std::string progressMessage;

    // Target identifier for the task (e.g. subscription id for TEST_SUBSCRIPTION).
    // Empty when the task has no single target (e.g. TEST_ALL, UPDATE_ALL).
    std::string targetId;
};

} // namespace task