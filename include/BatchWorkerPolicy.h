#pragma once

// Batch worker lifecycle policy — pure, dependency-free helpers extracted from
// ProxyBatchTester so the worker-control decisions are unit-testable without
// Xray, network access or SQLite.
//
// Bugfix lineage: docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md
// Root-cause RCA: docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md
// Contract mirror: tests/test_batch_worker_policy.cpp
//
// NOTE: this project forbids `auto` type deduction — keep explicit types here.

#include <string>

namespace batch_policy {

// ---------------------------------------------------------------------------
// D3: three-state outcome of a throttled instance-health evaluation
// ---------------------------------------------------------------------------
enum class InstanceHealthOutcome {
    Throttled,       // inside the 30s per-apiPort cooldown: another reporter already ran
    Relaunched,      // the instance was restarted successfully and is ready
    RelaunchFailed   // a restart was attempted and failed; its ports were freed
};

enum class InstanceDeadDecision {
    ClearAndContinueRelaunched,  // instance relaunched -> clear the flag, keep serving
    ClearAndContinueThrottled,   // cooldown hit -> clear the flag, keep serving (no restart needed)
    StopWorkerAndLog             // relaunch really failed -> stop this worker, with a log line
};

inline InstanceDeadDecision decideInstanceDead(InstanceHealthOutcome outcome) {
    if (outcome == InstanceHealthOutcome::Relaunched) {
        return InstanceDeadDecision::ClearAndContinueRelaunched;
    }
    if (outcome == InstanceHealthOutcome::Throttled) {
        return InstanceDeadDecision::ClearAndContinueThrottled;
    }
    return InstanceDeadDecision::StopWorkerAndLog;
}

// ---------------------------------------------------------------------------
// F5: a leftover/duplicate proxy tag is NOT an instance-health failure
// ---------------------------------------------------------------------------
// A duplicate/leftover tag is not an instance-health failure: the gRPC handler
// answered with a semantic error, which proves the instance is alive and
// reachable. Restarting it would be pure churn.
//
// Scope note (2026-10-19, empirical): only the gRPC "duplicate tag" family is
// matched. The separate "output=adding: proxy" text seen in the field log comes
// from the `xray api ado` fallback SUBPROCESS stdout and proves nothing about
// the instance, so it is deliberately NOT treated as stale-tag noise.
inline bool isStaleTagInjectionError(const std::string& error) {
    return error.find("existing tag found") != std::string::npos ||
           error.find("already exists") != std::string::npos ||
           error.find("duplicate") != std::string::npos;
}

// retryCount: injection attempts already consumed (3 == the retry loop is exhausted)
inline bool shouldReportEndpointUnreachable(const std::string& lastInjectionError, int retryCount) {
    if (retryCount < 3) {
        return false;
    }
    if (isStaleTagInjectionError(lastInjectionError)) {
        return false;  // the instance is healthy; a leftover tag caused the failure
    }
    return true;
}

// ---------------------------------------------------------------------------
// D5: log a per-monitor probe failure only when the streak first crosses it
// ---------------------------------------------------------------------------
// priorConsecutiveFailures: consecutive_failures read from the DB BEFORE this bump
inline bool shouldLogProbeFailureCrossing(int priorConsecutiveFailures, int probeFailStreak) {
    if (probeFailStreak <= 0) {
        return false;
    }
    return priorConsecutiveFailures + 1 == probeFailStreak;
}

}  // namespace batch_policy
