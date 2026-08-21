// Unit tests for ProxyListModel evaluation maps:
//   - setRunningDurations() merges live elapsed time into the Runtime column
//   - Health gains a running-time bonus (ramp 30 min, weight 0.3, capped 1.0)
#include <gtest/gtest.h>
#include <wx/wx.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "ProxyListModel.h"
#include "Profileexitem.h"
#include "Profileitem.h"

namespace {

// wxDataViewIndexListModel needs a wxWidgets runtime, so initialize it
// per-test.  No wxApp / event loop is required for pure model logic.
class ProxyListModelTest : public ::testing::Test {
protected:
    void SetUp() override { wxInitialize(); }
    void TearDown() override { wxUninitialize(); }
};

db::models::ProfileExItem makeEx(const std::string& indexId, int start,
                                 int crash, long long runtime) {
    db::models::ProfileExItem ex;
    ex.indexid = indexId;
    ex.start_count = start;
    ex.crash_count = crash;
    ex.total_runtime_ms = runtime;
    return ex;
}

}  // namespace

TEST_F(ProxyListModelTest, BaselineWithoutRunningSessions) {
    std::vector<db::models::Profileitem> proxies(2);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));
    exItems.push_back(makeEx("B", 5, 3, 5000));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    // Runtime equals the cumulated total when nothing is running.
    EXPECT_EQ(model.getRuntime("A"), 1000LL);
    EXPECT_EQ(model.getRuntime("B"), 5000LL);

    // Health is the Bayesian-smoothed base score.
    EXPECT_NEAR(model.getHealth("A"), 0.75, 1e-9);      // (2+1)/(2+2)
    EXPECT_NEAR(model.getHealth("B"), 3.0 / 7.0, 1e-9); // (2+1)/(5+2)
}

TEST_F(ProxyListModelTest, RunningSessionsMergeRuntimeAndHealthBonus) {
    std::vector<db::models::Profileitem> proxies(2);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));
    exItems.push_back(makeEx("B", 5, 3, 5000));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    // A is running with 120000 ms (2 min) elapsed per heartbeat.
    std::unordered_map<std::string, long long> running;
    running["A"] = 120000LL;
    model.setRunningDurations(running);

    // Runtime column: total_runtime_ms + live elapsed time.
    EXPECT_EQ(model.getRuntime("A"), 1000LL + 120000LL);
    // B untouched.
    EXPECT_EQ(model.getRuntime("B"), 5000LL);

    // Health bonus = min(120000/1800000, 1) * 0.3 = 0.02 -> 0.77.
    EXPECT_NEAR(model.getHealth("A"), 0.75 + 0.02, 1e-9);
    // B unchanged.
    EXPECT_NEAR(model.getHealth("B"), 3.0 / 7.0, 1e-9);
}

TEST_F(ProxyListModelTest, LongRunningHealthCappedAtOne) {
    std::vector<db::models::Profileitem> proxies(1);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("C", 10, 0, 0));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    // 60 minutes elapsed > 30-minute ramp -> bonus saturates at 0.3.
    std::unordered_map<std::string, long long> running;
    running["C"] = 3600000LL;
    model.setRunningDurations(running);

    // Runtime = cumulated total (0) + live elapsed (3600000).
    EXPECT_EQ(model.getRuntime("C"), 3600000LL);
    // base = (10+1)/(10+2) = 0.91667, plus 0.3 -> capped at 1.0.
    EXPECT_NEAR(model.getHealth("C"), 1.0, 1e-9);
}

TEST_F(ProxyListModelTest, UnknownIndexIdReturnsZero) {
    std::vector<db::models::Profileitem> proxies(1);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 1, 0, 42));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getRuntime("missing"), 0LL);
    EXPECT_EQ(model.getHealth("missing"), 0.0);
}

// Cold start (start_count=0, crash_count=0) should yield health=0.0
// instead of the previous Bayesian prior of 0.5.
TEST_F(ProxyListModelTest, ColdStartProxyHasZeroHealth) {
    std::vector<db::models::Profileitem> proxies(1);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 0, 0, 0));  // never tested

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getRuntime("A"), 0LL);
    EXPECT_EQ(model.getHealth("A"), 0.0);

    // Even with a running session, health must stay 0.0 for cold start.
    std::unordered_map<std::string, long long> running;
    running["A"] = 120000LL;  // 2 min elapsed
    model.setRunningDurations(running);
    EXPECT_EQ(model.getHealth("A"), 0.0);
}

// Regression: the periodic 3s refresh passes the same absolute heartbeat
// snapshot; setRunningDurations() must REPLACE the running map instead of
// accumulating into runtimeMap_, otherwise the Runtime column grows on
// every tick.
TEST_F(ProxyListModelTest, RepeatedRefreshDoesNotAccumulateRuntime) {
    std::vector<db::models::Profileitem> proxies(1);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    std::unordered_map<std::string, long long> running;
    running["A"] = 120000LL;
    EXPECT_TRUE(model.setRunningDurations(running));
    EXPECT_EQ(model.getRuntime("A"), 1000LL + 120000LL);

    // Second tick with the same snapshot must not double the value.
    EXPECT_FALSE(model.setRunningDurations(running));
    EXPECT_EQ(model.getRuntime("A"), 1000LL + 120000LL);

    // Session ended (empty snapshot) resets to the cumulated total.
    std::unordered_map<std::string, long long> none;
    EXPECT_TRUE(model.setRunningDurations(none));
    EXPECT_EQ(model.getRuntime("A"), 1000LL);
}

// Regression: when no standalone proxy is running the background poll
// returns an empty map; setRunningDurations() must report "no change" so
// the panel can skip the DataViewCtrl repaint (idle UI stays idle).
TEST_F(ProxyListModelTest, EmptySnapshotReportsNoChange) {
    std::vector<db::models::Profileitem> proxies(1);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    std::unordered_map<std::string, long long> none;
    EXPECT_FALSE(model.setRunningDurations(none));
    EXPECT_FALSE(model.setRunningDurations(none));
    EXPECT_EQ(model.getRuntime("A"), 1000LL);
}

// Regression: rebuildMaps() during an in-progress session must NOT wipe
// the historical runtime base.  finalizeStop back-fills total_runtime_ms
// only when the session ends, so while running the DB row still carries 0.
// refreshResults() calls rebuildMaps() then setRunningDurations(); if the
// base is lost the Runtime column resets to the live heartbeat value.
TEST_F(ProxyListModelTest, RebuildMapsPreservesRuntimeDuringRunningSession) {
    std::vector<db::models::Profileitem> proxies(2);
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));  // historical runtime = 1s
    exItems.push_back(makeEx("B", 5, 3, 5000));  // historical runtime = 5s

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    // Start a session on A: 120000 ms (2 min) elapsed.
    std::unordered_map<std::string, long long> running;
    running["A"] = 120000LL;
    model.setRunningDurations(running);
    EXPECT_EQ(model.getRuntime("A"), 1000LL + 120000LL);
    EXPECT_EQ(model.getRuntime("B"), 5000LL);
    EXPECT_NEAR(model.getHealth("A"), 0.75 + 0.02, 1e-9);
    EXPECT_NEAR(model.getHealth("B"), 3.0 / 7.0, 1e-9);

    // Simulate refreshResults(): reload exItems (A still has total_runtime_ms=0
    // because finalizeStop hasn't run) and rebuild maps.
    exItems.clear();
    exItems.push_back(makeEx("A", 2, 0, 0));  // DB row not yet updated
    exItems.push_back(makeEx("B", 5, 3, 5000));
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    // Historical base for A must survive the rebuild.
    EXPECT_EQ(model.getRuntime("A"), 1000LL + 120000LL);
    EXPECT_EQ(model.getRuntime("B"), 5000LL);

    // Health is recomputed from exItems_ (no running bonus yet); re-apply
    // the running snapshot to restore the bonus.
    model.setRunningDurations(running);
    EXPECT_NEAR(model.getHealth("A"), 0.75 + 0.02, 1e-9);
    EXPECT_NEAR(model.getHealth("B"), 3.0 / 7.0, 1e-9);
}

// Runtime column sorting must include the live running duration so the
// sort order matches the displayed value (getRuntime = runtimeMap_ +
// runningDurations_).  Before the fix Compare only looked at the
// historical part, so two proxies with identical history compared equal
// even when one was running and the other was not.
TEST_F(ProxyListModelTest, CompareRuntimeIncludesRunningDuration) {
    std::vector<db::models::Profileitem> proxies(2);
    proxies[0].indexid = "A";
    proxies[1].indexid = "B";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 60000));  // historical 60s
    exItems.push_back(makeEx("B", 2, 0, 60000));  // historical 60s, stopped

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    // A is running with 30s elapsed -> displayed total 90s.
    std::unordered_map<std::string, long long> running;
    running["A"] = 30000LL;
    model.setRunningDurations(running);

    EXPECT_EQ(model.getRuntime("A"), 90000LL);
    EXPECT_EQ(model.getRuntime("B"), 60000LL);

    // idOffset_ defaults to 0 in tests (no attached wxDataViewCtrl), so
    // raw data indices 0/1 map directly to proxies_[0] / proxies_[1].
    wxDataViewItem itemA(reinterpret_cast<void*>(static_cast<wxUIntPtr>(0)));
    wxDataViewItem itemB(reinterpret_cast<void*>(static_cast<wxUIntPtr>(1)));

    // Ascending: A (90s) > B (60s) -> A sorts after B -> Compare > 0.
    EXPECT_GT(model.Compare(itemA, itemB, COL_TOTAL_RUNTIME_MS, true), 0);
    // Descending: A (90s) > B (60s) -> A sorts before B -> Compare < 0.
    EXPECT_LT(model.Compare(itemA, itemB, COL_TOTAL_RUNTIME_MS, false), 0);
}

// When displayed totals are equal, Compare must return 0 even if the
// historical parts differ (A: 5s history + 1s running = 6s; B: 6s history).
TEST_F(ProxyListModelTest, CompareRuntimeEqualWhenTotalsEqual) {
    std::vector<db::models::Profileitem> proxies(2);
    proxies[0].indexid = "A";
    proxies[1].indexid = "B";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 5000));  // historical 5s
    exItems.push_back(makeEx("B", 2, 0, 6000));  // historical 6s, stopped

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    std::unordered_map<std::string, long long> running;
    running["A"] = 1000LL;  // +1s running -> displayed 6s
    model.setRunningDurations(running);

    EXPECT_EQ(model.getRuntime("A"), 6000LL);
    EXPECT_EQ(model.getRuntime("B"), 6000LL);

    wxDataViewItem itemA(reinterpret_cast<void*>(static_cast<wxUIntPtr>(0)));
    wxDataViewItem itemB(reinterpret_cast<void*>(static_cast<wxUIntPtr>(1)));

    EXPECT_EQ(model.Compare(itemA, itemB, COL_TOTAL_RUNTIME_MS, true), 0);
    EXPECT_EQ(model.Compare(itemA, itemB, COL_TOTAL_RUNTIME_MS, false), 0);
}

// -------------------------------------------------------------------
// getProxyValidityReason: returns "" when delay > 0, "untested" when
// delay is empty/-1, and "invalid" for any other non-positive value.
// -------------------------------------------------------------------
TEST_F(ProxyListModelTest, ValidityReason_PositiveDelay_ReturnsEmpty) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    db::models::ProfileExItem ex;
    ex.indexid = "A";
    ex.delay = "100";
    exItems.push_back(ex);

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getProxyValidityReason("A"), "");
}

TEST_F(ProxyListModelTest, ValidityReason_EmptyDelay_ReturnsUntested) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    db::models::ProfileExItem ex;
    ex.indexid = "A";
    ex.delay = "";
    exItems.push_back(ex);

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getProxyValidityReason("A"), "untested");
}

TEST_F(ProxyListModelTest, ValidityReason_LegacyMinusOne_ReturnsUntested) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    db::models::ProfileExItem ex;
    ex.indexid = "A";
    ex.delay = "-1";
    exItems.push_back(ex);

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getProxyValidityReason("A"), "untested");
}

TEST_F(ProxyListModelTest, ValidityReason_ZeroDelay_ReturnsInvalid) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    db::models::ProfileExItem ex;
    ex.indexid = "A";
    ex.delay = "0";
    exItems.push_back(ex);

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getProxyValidityReason("A"), "invalid");
}

TEST_F(ProxyListModelTest, ValidityReason_MissingExItem_ReturnsUntested) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;  // no entry for "A"

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.rebuildMaps();

    EXPECT_EQ(model.getProxyValidityReason("A"), "untested");
}
