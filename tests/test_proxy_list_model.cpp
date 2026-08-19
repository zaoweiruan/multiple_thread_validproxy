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
