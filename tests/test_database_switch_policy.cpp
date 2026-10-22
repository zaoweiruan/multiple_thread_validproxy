// Unit tests for the pure database-switch policy helper.
//
// The policy answers one question: what should happen when the user asks to
// switch the live database while background work may still be using the
// current sqlite3 handle?
//
//   Allow               -> nothing is running, switch immediately
//   QuiesceProbe        -> only the periodic silent probe runs; stop it first
//   RefuseUserOperation -> a user-initiated operation runs; refuse
//
// Pure/unit-testable: no process, no socket, no database, no wxWidgets.

#include <gtest/gtest.h>

#include "DatabaseSwitchPolicy.h"

namespace {

using db_switch_policy::Decision;
using db_switch_policy::decide;
using db_switch_policy::decisionLabel;

// ---------------------------------------------------------------------------
// decide
// ---------------------------------------------------------------------------
TEST(DatabaseSwitchPolicyTest, Decide_AllowsWhenNothingIsRunning) {
    EXPECT_EQ(decide(false, false), Decision::Allow);
}

TEST(DatabaseSwitchPolicyTest, Decide_QuiescesWhenOnlyTheProbeRuns) {
    EXPECT_EQ(decide(false, true), Decision::QuiesceProbe);
}

TEST(DatabaseSwitchPolicyTest, Decide_RefusesWhenAUserOperationRuns) {
    EXPECT_EQ(decide(true, false), Decision::RefuseUserOperation);
}

TEST(DatabaseSwitchPolicyTest, Decide_RefusesWhenBothUserOperationAndProbeRun) {
    // A user-initiated operation may be mutating the old handle and cannot
    // be cancelled from the UI thread: it always wins over the probe.
    EXPECT_EQ(decide(true, true), Decision::RefuseUserOperation);
}

// ---------------------------------------------------------------------------
// decisionLabel
// ---------------------------------------------------------------------------
TEST(DatabaseSwitchPolicyTest, DecisionLabel_AllValues) {
    EXPECT_STREQ(decisionLabel(Decision::Allow), "allow");
    EXPECT_STREQ(decisionLabel(Decision::QuiesceProbe), "quiesce-probe");
    EXPECT_STREQ(decisionLabel(Decision::RefuseUserOperation), "refuse-user-operation");
}

} // namespace
