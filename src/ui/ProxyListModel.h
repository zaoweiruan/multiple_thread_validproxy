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

// -------------------------------------------------------------------
// Column indices in the data view
// -------------------------------------------------------------------
enum {
    COL_ROWNUM   = 0,
    COL_TYPE     = 1,
    COL_ADDRESS  = 2,
    COL_PORT     = 3,
    COL_DELAY    = 4,
    COL_FAILURES = 5,
    COL_REMARKS  = 6,
    COL_MESSAGE  = 7,
    COL_INDEXID  = 8,
    COL_COUNT    = 9,
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

    // Rebuild lookup maps from exItems_
    void rebuildMaps();

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

    // Retrieve a typed pointer to the profile item at the given view row
    const db::models::Profileitem* getProfileAtRow(unsigned int viewRow) const;

    // Notify the view that delay/message/failures values changed for all rows.
    // Model must call this after rebuildMaps() to trigger view redraw.
    void notifyTestResultChanged();

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

    // Internal ID offset compensation.
    // 0 = IDs are 0-based (correct), 1 = IDs are 1-based (buggy wx build).
    unsigned int idOffset_ = 0;
};

#endif // UI_PROXY_LIST_MODEL_H
