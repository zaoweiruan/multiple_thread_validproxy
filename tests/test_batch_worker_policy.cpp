// Unit tests for include/BatchWorkerPolicy.h.
//
// Bugfix lineage: docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md
// Root-cause RCA:  docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md
//
// CMake registration (mirrors the "ProxyBatchTester components tests" block):
//   add_executable(test_batch_worker_policy tests/test_batch_worker_policy.cpp)
//   target_include_directories(test_batch_worker_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
//   target_link_libraries(test_batch_worker_policy PRIVATE gtest_main gtest)
//   add_test(NAME BatchWorkerPolicyTest COMMAND test_batch_worker_policy)

#include "BatchWorkerPolicy.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using batch_policy::InstanceDeadDecision;
using batch_policy::InstanceHealthOutcome;
}  // namespace

// --- D3: distinguish "cooldown hit" from "relaunch actually failed" --------

TEST(BatchWorkerPolicyTest, InstanceThrottled_ClearsFlagAndKeepsServing) {
    EXPECT_EQ(batch_policy::decideInstanceDead(InstanceHealthOutcome::Throttled),
              InstanceDeadDecision::ClearAndContinueThrottled);
}

TEST(BatchWorkerPolicyTest, InstanceRelaunched_ClearsFlagAndKeepsServing) {
    EXPECT_EQ(batch_policy::decideInstanceDead(InstanceHealthOutcome::Relaunched),
              InstanceDeadDecision::ClearAndContinueRelaunched);
}

TEST(BatchWorkerPolicyTest, InstanceRelaunchFailed_StopsWorker) {
    EXPECT_EQ(batch_policy::decideInstanceDead(InstanceHealthOutcome::RelaunchFailed),
              InstanceDeadDecision::StopWorkerAndLog);
}

// --- F5: a leftover proxy tag is not an instance-health failure ------------

TEST(BatchWorkerPolicyTest, StaleTagError_NotReportedAsEndpointUnreachable) {
    const std::string err =
        "rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: proxy";
    EXPECT_TRUE(batch_policy::isStaleTagInjectionError(err));
    EXPECT_FALSE(batch_policy::shouldReportEndpointUnreachable(err, 3));
    EXPECT_FALSE(batch_policy::shouldReportEndpointUnreachable(err, 5));
}

TEST(BatchWorkerPolicyTest, GenericInjectionError_ReportedAfterThreeRetries) {
    const std::string err = "xray api ado failed with code: 1 output:";
    EXPECT_FALSE(batch_policy::isStaleTagInjectionError(err));
    EXPECT_FALSE(batch_policy::shouldReportEndpointUnreachable(err, 2));
    EXPECT_TRUE(batch_policy::shouldReportEndpointUnreachable(err, 3));
}

// --- D5: probe-failure WARN only when the streak first crosses threshold ---

TEST(BatchWorkerPolicyTest, ProbeFailureBelowThreshold_NoLog) {
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(0, 3));
}

TEST(BatchWorkerPolicyTest, ProbeFailureCrossing_LogsOnce) {
    EXPECT_TRUE(batch_policy::shouldLogProbeFailureCrossing(2, 3));
}

TEST(BatchWorkerPolicyTest, ProbeFailureAlreadyPastThreshold_NoRepeatLog) {
    // Regression for the [OnlineProbe] storm: ~36 identical WARN lines for the same
    // indexId over 3 minutes because the check used ">=" instead of "==".
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(3, 3));
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(9, 3));
}

TEST(BatchWorkerPolicyTest, ProbeFailureDisabledThreshold_NoLog) {
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(-1, 0));
}
