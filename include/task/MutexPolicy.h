#pragma once

#include "TaskType.h"

namespace task {

// Single source of truth for task mutual-exclusion decisions.
class MutexPolicy {
public:
    enum class ConcurrencyDecision {
        ALLOW,
        REJECT,
        JOIN_PROBE_THEN_RUN,
        CANCEL_PROBE,
    };

    // Decide whether task b may run given task a is running (or probe state).
    // probeRunning: whether periodic online probe is in flight.
    // userOpRunning: whether a user operation is in flight.
    static ConcurrencyDecision decide(TaskType a, TaskType b,
                                     bool probeRunning, bool userOpRunning);

    static bool canRunConcurrently(TaskType a, TaskType b);
};

} // namespace task