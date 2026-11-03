#include "task/MutexPolicy.h"

namespace task {

namespace {

bool isWorkerTask(TaskType t) {
    switch (t) {
        case TaskType::TEST_SUBSCRIPTION:
        case TaskType::TEST_SINGLE_PROXY:
        case TaskType::TEST_ALL:
        case TaskType::TEST_BATCH:
        case TaskType::UPDATE_SUBSCRIPTION:
        case TaskType::UPDATE_ALL:
        case TaskType::DEDUP:
        case TaskType::SYNC_DB:
        case TaskType::RESOLVE_REGION:
        case TaskType::RESOLVE_SINGLE_REGION:
        case TaskType::FIND_FIRST:
        case TaskType::FIND_BEST:
        case TaskType::FIND_BY_INDEX:
        case TaskType::AUTO_TASK:
        case TaskType::PROBE_POOL_MEMBERS:
        case TaskType::PROBE_MEMBER:
            return true;
        default:
            return false;
    }
}

bool isExportTask(TaskType t) {
    return t == TaskType::EXPORT_SHARE_LINKS || t == TaskType::GENERATE_CONFIG;
}

} // namespace

MutexPolicy::ConcurrencyDecision MutexPolicy::decide(TaskType a, TaskType b,
                                                    bool probeRunning, bool userOpRunning) {
    if (a == TaskType::NONE || b == TaskType::NONE) {
        return ConcurrencyDecision::ALLOW;
    }

    // Export-like tasks are read-only and can run concurrently.
    if (isExportTask(a) || isExportTask(b)) {
        return ConcurrencyDecision::ALLOW;
    }

    const bool aProbe = (a == TaskType::TEST_ONLINE);
    const bool bProbe = (b == TaskType::TEST_ONLINE);

    // Probe vs probe: coexist (single instance guarded elsewhere).
    if (aProbe && bProbe) {
        return ConcurrencyDecision::ALLOW;
    }

    // Probe vs user operation: probe is independent; user op may join probe first.
    if (aProbe && isWorkerTask(b)) {
        return ConcurrencyDecision::JOIN_PROBE_THEN_RUN;
    }
    if (isWorkerTask(a) && bProbe) {
        // If probe is running, skip/cancel it; otherwise allow.
        if (probeRunning) {
            return ConcurrencyDecision::CANCEL_PROBE;
        }
        return ConcurrencyDecision::REJECT;
    }

    // Worker tasks are generally mutually exclusive on the shared channel.
    // Exception: TEST_SINGLE_PROXY can coexist with TEST_ALL/TEST_BATCH
    // (fine-grained mode: single-proxy tests interleave with batch subtasks
    // via TaskQueue priority scheduling, serialized by the worker thread).
    if (isWorkerTask(a) && isWorkerTask(b)) {
        if ((a == TaskType::TEST_SINGLE_PROXY &&
             (b == TaskType::TEST_ALL || b == TaskType::TEST_BATCH)) ||
            (b == TaskType::TEST_SINGLE_PROXY &&
             (a == TaskType::TEST_ALL || a == TaskType::TEST_BATCH))) {
            return ConcurrencyDecision::ALLOW;
        }
        return ConcurrencyDecision::REJECT;
    }

    return ConcurrencyDecision::ALLOW;
}

bool MutexPolicy::canRunConcurrently(TaskType a, TaskType b) {
    // Default probe/user state = false; conservative for static checks.
    return decide(a, b, false, false) != ConcurrencyDecision::REJECT;
}

} // namespace task