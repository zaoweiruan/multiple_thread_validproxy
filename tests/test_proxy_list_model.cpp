// Unit tests for ProxyListModel evaluation maps:
//   - setRunningDurations() merges live elapsed time into the Runtime column
//   - Health gains a running-time bonus (ramp 30 min, weight 0.3, capped 1.0)
//   - updateResultFor() incrementally patches delay/message/failures maps
//   - notifyTestResultChangedFor() emits ItemChanged for exactly one row
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

// wxDataViewModelNotifier that records ItemChanged deliveries so the tests
// can observe what notifyTestResultChangedFor() actually sends to the view.
// AddNotifier() transfers ownership to the model, so the instance must be
// heap-allocated and is deleted by wxDataViewModel's dtor — never call
// RemoveNotifier() on it.
class RecordingNotifier : public wxDataViewModelNotifier {
public:
    int itemChangedCount = 0;
    wxDataViewItem lastChangedItem;

    bool ItemAdded(const wxDataViewItem&, const wxDataViewItem&) override { return true; }
    bool ItemDeleted(const wxDataViewItem&, const wxDataViewItem&) override { return true; }
    bool ItemChanged(const wxDataViewItem& item) override {
        ++itemChangedCount;
        lastChangedItem = item;
        return true;
    }
    bool ValueChanged(const wxDataViewItem&, unsigned int) override { return true; }
    bool Cleared() override { return true; }
    void Resort() override {}
};

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

// -------------------------------------------------------------------
// updateResultFor: incremental one-row patch of the delay/message/
// failures maps.  Returns true only when at least one value changed.
// -------------------------------------------------------------------
TEST_F(ProxyListModelTest, UpdateResultFor_FirstSeenIndexId_StoresAndReturnsTrue) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;  // no entry: "A" is new

    ProxyListModel model;
    model.setData(&proxies, &exItems);

    // First sight of the indexId: every field is stored, change reported.
    EXPECT_TRUE(model.updateResultFor("A", "120", "ok", 2));
    EXPECT_EQ(model.getDelay("A"), "120");
    EXPECT_EQ(model.getMessage("A"), "ok");
    EXPECT_EQ(model.getFailures("A"), 2);

    // Re-applying the identical result must report "no change" so the
    // caller can skip notifying the view.
    EXPECT_FALSE(model.updateResultFor("A", "120", "ok", 2));
}

TEST_F(ProxyListModelTest, UpdateResultFor_SameValuesAgain_ReturnsFalse) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    db::models::ProfileExItem ex;
    ex.indexid = "A";
    ex.delay = "120";
    ex.message = "ok";
    ex.consecutive_failures = 2;
    exItems.push_back(ex);

    ProxyListModel model;
    model.setData(&proxies, &exItems);

    // Maps already carry these values -> identical update is a no-op.
    EXPECT_FALSE(model.updateResultFor("A", "120", "ok", 2));
    EXPECT_FALSE(model.updateResultFor("A", "120", "ok", 2));

    EXPECT_EQ(model.getDelay("A"), "120");
    EXPECT_EQ(model.getMessage("A"), "ok");
    EXPECT_EQ(model.getFailures("A"), 2);
}

TEST_F(ProxyListModelTest, UpdateResultFor_PartialChange_UpdatesOnlyChangedField) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    db::models::ProfileExItem ex;
    ex.indexid = "A";
    ex.delay = "100";
    ex.message = "msg1";
    ex.consecutive_failures = 0;
    exItems.push_back(ex);

    ProxyListModel model;
    model.setData(&proxies, &exItems);

    // Only delay changes: message/failures must survive untouched.
    EXPECT_TRUE(model.updateResultFor("A", "200", "msg1", 0));
    EXPECT_EQ(model.getDelay("A"), "200");
    EXPECT_EQ(model.getMessage("A"), "msg1");
    EXPECT_EQ(model.getFailures("A"), 0);

    // Only failures changes.
    EXPECT_TRUE(model.updateResultFor("A", "200", "msg1", 3));
    EXPECT_EQ(model.getDelay("A"), "200");
    EXPECT_EQ(model.getMessage("A"), "msg1");
    EXPECT_EQ(model.getFailures("A"), 3);

    // Only message changes.
    EXPECT_TRUE(model.updateResultFor("A", "200", "msg2", 3));
    EXPECT_EQ(model.getDelay("A"), "200");
    EXPECT_EQ(model.getMessage("A"), "msg2");
    EXPECT_EQ(model.getFailures("A"), 3);

    // Fully converged again: no change reported.
    EXPECT_FALSE(model.updateResultFor("A", "200", "msg2", 3));
}

// -------------------------------------------------------------------
// notifyTestResultChangedFor: must emit ItemChanged for exactly the row
// matching the indexId, and be a safe no-op otherwise.  The notification
// is observed through a wxDataViewModelNotifier attached to the model
// (AddNotifier transfers ownership; the model dtor deletes it).
// -------------------------------------------------------------------
TEST_F(ProxyListModelTest, NotifyTestResultChangedFor_ExistingIndexId_NotifiesThatRow) {
    std::vector<db::models::Profileitem> proxies(2);
    proxies[0].indexid = "A";
    proxies[1].indexid = "B";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 1, 0, 10));
    exItems.push_back(makeEx("B", 1, 0, 20));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    // Mirror ProxyListPanel: rebuild the internal row-ID table, then
    // calibrate the 1-based-ID offset this wxWidgets build uses.
    model.Reset(0);
    model.Reset(static_cast<unsigned int>(proxies.size()));
    model.detectIdOffset();

    RecordingNotifier* notifier = new RecordingNotifier();
    model.AddNotifier(notifier);

    model.notifyTestResultChangedFor("B");

    // Exactly one ItemChanged, targeting B's row (view row 1).
    EXPECT_EQ(notifier->itemChangedCount, 1);
    EXPECT_TRUE(notifier->lastChangedItem.IsOk());
    EXPECT_EQ(notifier->lastChangedItem.GetID(), model.GetItem(1).GetID());
}

TEST_F(ProxyListModelTest, NotifyTestResultChangedFor_MissingIndexId_DoesNotNotify) {
    std::vector<db::models::Profileitem> proxies(2);
    proxies[0].indexid = "A";
    proxies[1].indexid = "B";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 1, 0, 10));
    exItems.push_back(makeEx("B", 1, 0, 20));

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    model.Reset(0);
    model.Reset(static_cast<unsigned int>(proxies.size()));
    model.detectIdOffset();

    RecordingNotifier* notifier = new RecordingNotifier();
    model.AddNotifier(notifier);

    // findRowByIndexId() returns -1 -> early return, no ItemChanged at all.
    model.notifyTestResultChangedFor("does-not-exist");
    EXPECT_EQ(notifier->itemChangedCount, 0);
}

TEST_F(ProxyListModelTest, NotifyTestResultChangedFor_NoData_DoesNotCrash) {
    ProxyListModel model;  // no proxies_ / exItems_ attached

    RecordingNotifier* notifier = new RecordingNotifier();
    model.AddNotifier(notifier);

    // proxies_ == nullptr -> findRowByIndexId() short-circuits to -1.
    model.notifyTestResultChangedFor("A");
    EXPECT_EQ(notifier->itemChangedCount, 0);
}

// -------------------------------------------------------------------
// syncHistoryForIndexId: incremental one-row refresh of the history
// maps (Starts / Runtime / Health).  Mirrors the rebuildMaps() formulas
// so a probe-triggered incremental refresh produces the same values a
// full rebuild would.  Missing indexId / no exItems_ is a safe no-op.
// -------------------------------------------------------------------
TEST_F(ProxyListModelTest, SyncHistoryForIndexId_FirstSeenIndexId_ReturnsTrue) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 3, 1, 1234));

    ProxyListModel model;
    model.setData(&proxies, &exItems);  // rebuildMaps already ran
    model.clear();                       // wipe maps AND null exItems_
    model.setDataWithoutRebuild(&proxies, &exItems);  // restore pointer, keep empty maps

    // First sight of the indexId: every history field is stored -> true.
    EXPECT_TRUE(model.syncHistoryForIndexId("A"));
    EXPECT_NEAR(model.getHealth("A"), 3.0 / 5.0, 1e-9);   // (3-1+1)/(3+2)
    EXPECT_EQ(model.getRuntime("A"), 1234LL);

    // No exItems_ attached: safe no-op, returns false.
    ProxyListModel empty;
    EXPECT_FALSE(empty.syncHistoryForIndexId("A"));
}

TEST_F(ProxyListModelTest, SyncHistoryForIndexId_SameValuesAgain_ReturnsFalse) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 3, 1, 1234));

    ProxyListModel model;
    model.setData(&proxies, &exItems);

    // Maps already carry these values -> identical sync is a no-op.
    EXPECT_FALSE(model.syncHistoryForIndexId("A"));
    EXPECT_FALSE(model.syncHistoryForIndexId("A"));
    EXPECT_NEAR(model.getHealth("A"), 0.6, 1e-9);
    EXPECT_EQ(model.getRuntime("A"), 1234LL);
}

TEST_F(ProxyListModelTest, SyncHistoryForIndexId_MissingIndexId_ReturnsFalse) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 100));

    ProxyListModel model;
    model.setData(&proxies, &exItems);

    // exItems_ has no entry for "B": no maps to touch, no change reported.
    EXPECT_FALSE(model.syncHistoryForIndexId("B"));
    EXPECT_EQ(model.getHealth("B"), 0.0);
    EXPECT_EQ(model.getRuntime("B"), 0LL);

    // Existing "A" entry stays untouched by the missing-indexId sync.
    EXPECT_NEAR(model.getHealth("A"), 0.75, 1e-9);
}

TEST_F(ProxyListModelTest, SyncHistoryForIndexId_PartialChange_UpdatesOnlyChangedField) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));  // health = (2+1)/(2+2) = 0.75

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    // Reset maps to empty so syncHistoryForIndexId reports the first write
    // as a change (exItems_ is preserved by setDataWithoutRebuild).
    model.clear();
    model.setDataWithoutRebuild(&proxies, &exItems);

    // Probe failed: DB reset the history counters -> 0/0/0.
    // NOTE: total_runtime_ms is 0 here (finalizeStop never wrote it back),
    // so the historical base stays at 0 (no prior entry in runtimeMap_).
    exItems[0] = makeEx("A", 0, 0, 0);
    EXPECT_TRUE(model.syncHistoryForIndexId("A"));
    EXPECT_NEAR(model.getHealth("A"), 0.0, 1e-9);   // cold start -> 0.0
    EXPECT_EQ(model.getRuntime("A"), 0LL);

    // A new run with a crash: start=5, crash=3, runtime=5000.
    exItems[0] = makeEx("A", 5, 3, 5000);
    EXPECT_TRUE(model.syncHistoryForIndexId("A"));
    EXPECT_NEAR(model.getHealth("A"), 3.0 / 7.0, 1e-9);  // (5-3+1)/(5+2)
    EXPECT_EQ(model.getRuntime("A"), 5000LL);

    // Re-sync the identical exItem: no change reported.
    EXPECT_FALSE(model.syncHistoryForIndexId("A"));
}

TEST_F(ProxyListModelTest, SyncHistoryForIndexId_HistoryMapsPreserveNonZeroBase) {
    // Mirrors the rebuildMaps() comment: total_runtime_ms is only written
    // back to ProfileExItem when finalizeStop runs.  While a session is
    // in progress the row still carries 0; syncHistoryForIndexId must
    // NOT overwrite a non-zero historical runtimeMap_ entry with 0.
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;
    exItems.push_back(makeEx("A", 2, 0, 1000));  // historical runtime = 1s

    ProxyListModel model;
    model.setData(&proxies, &exItems);
    EXPECT_EQ(model.getRuntime("A"), 1000LL);

    // Simulate a probe-triggered incremental refresh with total_runtime_ms
    // still 0 (session not finalized).  Historical base must survive.
    exItems[0] = makeEx("A", 2, 0, 0);
    // start_count / health are unchanged; runtime base is preserved.
    EXPECT_FALSE(model.syncHistoryForIndexId("A"));
    EXPECT_EQ(model.getRuntime("A"), 1000LL);

    // finalizeStop commits the new total (e.g. 12s).  Now the runtime is
    // overwritten.
    exItems[0] = makeEx("A", 3, 0, 12000);
    EXPECT_TRUE(model.syncHistoryForIndexId("A"));
    EXPECT_EQ(model.getRuntime("A"), 12000LL);
    EXPECT_NEAR(model.getHealth("A"), 4.0 / 5.0, 1e-9);  // (3+1)/(3+2)
}

// Full-rebuild vs incremental-sync parity: for a range of (start, crash,
// runtime) tuples the health value produced by syncHistoryForIndexId on a
// cleared model must equal the value produced by rebuildMaps on a fresh
// model.  This pins the syncHistoryForIndexId formula to rebuildMaps().
TEST_F(ProxyListModelTest, SyncHistoryForIndexId_HistoryFormulaMatchesRebuildMaps) {
    std::vector<db::models::Profileitem> proxies(1);
    proxies[0].indexid = "A";
    std::vector<db::models::ProfileExItem> exItems;

    const int starts[]   = {0, 1, 2, 5, 10};
    const int crashes[]  = {0, 1, 3};
    const long long runtimes[] = {0LL, 500LL, 5000LL};

    for (int si = 0; si < 5; ++si) {
        for (int ci = 0; ci < 3; ++ci) {
            for (int ri = 0; ri < 3; ++ri) {
                exItems.clear();
                exItems.push_back(makeEx("A", starts[si], crashes[ci], runtimes[ri]));

                // Model A: full rebuild via setData (fresh maps, then filled).
                ProxyListModel full;
                full.setData(&proxies, &exItems);

                // Model B: syncHistoryForIndexId on an empty-map model whose
                // exItems_ pointer is still valid.  clear() nulls the
                // pointers, so setDataWithoutRebuild restores them without
                // repopulating the maps.
                ProxyListModel sync;
                sync.setData(&proxies, &exItems);
                sync.clear();
                sync.setDataWithoutRebuild(&proxies, &exItems);

                EXPECT_TRUE(sync.syncHistoryForIndexId("A"))
                    << "start=" << starts[si]
                    << " crash=" << crashes[ci]
                    << " runtime=" << runtimes[ri];

                EXPECT_NEAR(sync.getHealth("A"), full.getHealth("A"), 1e-12)
                    << "start=" << starts[si]
                    << " crash=" << crashes[ci]
                    << " runtime=" << runtimes[ri];
                EXPECT_EQ(sync.getRuntime("A"), full.getRuntime("A"))
                    << "start=" << starts[si]
                    << " crash=" << crashes[ci]
                    << " runtime=" << runtimes[ri];
            }
        }
    }
}
