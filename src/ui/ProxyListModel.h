#ifndef UI_PROXY_LIST_MODEL_H
#define UI_PROXY_LIST_MODEL_H

#include <wx/string.h>
#include <wx/variant.h>
#include <wx/dataview.h>

#include <string>
#include <vector>
#include <unordered_map>

#include "Profileitem.h"
#include "ProfileExItem.h"
#include "Utils.h"

// -------------------------------------------------------------------
// Column indices in the data view
// -------------------------------------------------------------------
enum {
    COL_ROWNUM        = 0,
    COL_REGION        = 1,
    COL_DELAY         = 2,
    COL_TYPE          = 3,
    COL_ADDRESS       = 4,
    COL_PORT          = 5,
    COL_FAILURES      = 6,
    COL_REMARKS       = 7,
    COL_MESSAGE       = 8,
    COL_INDEXID       = 9,
    COL_START_COUNT   = 10,
    COL_TOTAL_RUNTIME_MS = 11,
    COL_HEALTH        = 12,
    COL_COUNT         = 13,
};

// -------------------------------------------------------------------
// ProxyListModel — virtual model for wxDataViewCtrl
//
// Replaces wxDataViewListStore with a wxDataViewIndexListModel so the
// view queries data lazily (viewport-only rendering) and sorting is
// handled natively via Compare() without rebuilding a store.
//
// The model holds non-owning pointers to a proxies_ vector and an
// exItems_ vector, both owned by ProxyListPanel.
// -------------------------------------------------------------------
class ProxyListModel : public wxDataViewIndexListModel {
public:
    ProxyListModel();
    ~ProxyListModel() override;

    // Set data pointers — call before Reset() or on data change
    void setData(std::vector<db::models::Profileitem>* proxies,
                 const std::vector<db::models::ProfileExItem>* exItems);

    // Set data pointers without rebuilding maps (use when maps will be
    // set separately via setMaps, e.g. after a background-thread build).
    void setDataWithoutRebuild(std::vector<db::models::Profileitem>* proxies,
                               const std::vector<db::models::ProfileExItem>* exItems);

    // Rebuild lookup maps from exItems_
    void rebuildMaps();

    // Set pre-built maps (built in a background thread).  Skips the
    // O(N) exItems_ iteration on the UI thread.
    void setMaps(const utils::ProxyListMaps& maps);

    // Replace the currently-running standalone sessions (indexId -> elapsed
    // ms from the watch heartbeat) with the given snapshot.  The map is
    // ASSIGNED, not accumulated, so repeated periodic refreshes stay
    // idempotent (the runtime/health columns do not grow every tick).
    // Returns true when the snapshot actually changed — the caller should
    // skip the view redraw when it returns false (e.g. no in-progress
    // session, which makes the periodic evaluation refresh a no-op).
    bool setRunningDurations(const std::unordered_map<std::string, long long>& runningMs);

    // Update the test-result lookup maps for ONE indexId (delay/message/
    // failures columns).  Caller must also keep the panel-owned exItems_
    // vector in sync.  Returns true when any value actually changed (caller
    // may then notify only this row).
    bool updateResultFor(const std::string& indexId,
                         const std::string& delay,
                         const std::string& message,
                         int failures);

    // Notify the view that ONE row's test-result cells changed.  No-op when
    // the indexId is not currently visible (filtered out / not present).
    void notifyTestResultChangedFor(const std::string& indexId);

    // Detect the internal ID offset.
    // Some wxWidgets builds of wxDataViewIndexListModel::Reset(N) populate
    // m_list with 1-based IDs (1..N) instead of 0-based (0..N-1).
    // Call this after every Reset() to compensate.
    void detectIdOffset();

    // Clear all data
    void clear();

    // Find the view row for a given indexId, returns -1 if not found
    int findRowByIndexId(const std::string& indexId) const;

    // Return the data-array index for a given view row
    unsigned int getDataIndex(unsigned int viewRow) const;

    // Return the indexId at a given view row
    std::string getIndexIdAtRow(unsigned int viewRow) const;

    // Query lookup maps
    std::string getDelay(const std::string& indexId) const;
    std::string getMessage(const std::string& indexId) const;
    int getFailures(const std::string& indexId) const;

    // Validity check for standalone proxy start: returns empty string when the
    // proxy is valid (delay > 0), or a non-empty reason string ("untested" or
    // "invalid") when it should not be started.  UI callers can use the
    // returned reason directly for user-facing messages.
    std::string getProxyValidityReason(const std::string& indexId) const;
    // Runtime (ms) shown for an indexId: total_runtime_ms plus any live
    // elapsed time merged by setRunningDurations().
    long long getRuntime(const std::string& indexId) const;
    // Health score shown for an indexId (base + running-time bonus, capped
    // at 1.0) as computed by rebuildMaps()/setRunningDurations().
    double getHealth(const std::string& indexId) const;

    // Retrieve a typed pointer to the profile item at the given view row
    const db::models::Profileitem* getProfileAtRow(unsigned int viewRow) const;

    // Notify the view that delay/message/failures values changed for all rows.
    // Model must call this after rebuildMaps() to trigger view redraw.
    void notifyTestResultChanged();

    // Notify the view that history (start_count/total_runtime_ms/health) values
    // changed for all rows with standalone history.  Call this after
    // rebuildMaps() when those columns are updated.
    void notifyHistoryChanged();

    // Notify the view that only the in-progress (running) rows changed.
    // Used by the periodic 3s refresh so idle proxies stay completely still.
    void notifyRunningChanged();

    // wxDataViewIndexListModel overrides
    unsigned int GetCount() const override;
    wxString GetColumnType(unsigned int col) const override;
    void GetValueByRow(wxVariant& variant, unsigned int row,
                       unsigned int col) const override;
    bool SetValueByRow(const wxVariant& variant, unsigned int row,
                       unsigned int col) override;
    int Compare(const wxDataViewItem& item1, const wxDataViewItem& item2,
                unsigned int col, bool ascending) const override;

private:
    // Non-owning pointers to the data owned by ProxyListPanel
    std::vector<db::models::Profileitem>* proxies_ = nullptr;
    const std::vector<db::models::ProfileExItem>* exItems_ = nullptr;

    // Lookup maps built from exItems_ — rebuilt on every data change
    std::unordered_map<std::string, std::string> delayMap_;
    std::unordered_map<std::string, std::string> messageMap_;
    std::unordered_map<std::string, int> failuresMap_;

    // History lookup maps built from exItems_
    std::unordered_map<std::string, int> startCountMap_;
    std::unordered_map<std::string, long long> runtimeMap_;
    std::unordered_map<std::string, double> healthMap_;

    // Current in-progress standalone sessions (indexId -> elapsed ms),
    // assigned by setRunningDurations().  Kept separate from runtimeMap_
    // (cumulated totals) so the same snapshot can be re-applied idempotently
    // on every periodic refresh without growing the Runtime column.
    std::unordered_map<std::string, long long> runningDurations_;

    // Internal ID offset compensation.
    // 0 = IDs are 0-based (correct), 1 = IDs are 1-based (buggy wx build).
    unsigned int idOffset_ = 0;
};

#endif // UI_PROXY_LIST_MODEL_H
