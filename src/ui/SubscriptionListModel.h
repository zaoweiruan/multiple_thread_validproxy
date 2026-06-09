#ifndef UI_SUBSCRIPTION_LIST_MODEL_H
#define UI_SUBSCRIPTION_LIST_MODEL_H

#include <wx/string.h>
#include <wx/variant.h>
#include <wx/dataview.h>

#include <string>
#include <vector>
#include <unordered_map>

#include "Subitem.h"

// -------------------------------------------------------------------
// Column indices in the subscription data view (scoped to avoid conflicts with ProxyListModel)
// -------------------------------------------------------------------
enum {
    SUB_COL_ROWNUM = 0,
    SUB_COL_ENABLED = 1,
    SUB_COL_NAME = 2,
    SUB_COL_VALID = 3,
    SUB_COL_PROXIES = 4,
    SUB_COL_UPDATE = 5
};

enum class SortDirection { None, Asc, Desc };

struct SortState {
    int column = -1;
    SortDirection direction = SortDirection::None;
};

// -------------------------------------------------------------------
// SubscriptionListModel — virtual model for wxDataViewCtrl
//
// Replaces wxDataViewListStore with a wxDataViewIndexListModel so the
// view queries data lazily (viewport-only rendering) and sorting is
// handled natively via Compare() without rebuilding a store.
// -------------------------------------------------------------------
class SubscriptionListModel : public wxDataViewIndexListModel {
public:
    SubscriptionListModel();
    ~SubscriptionListModel() override = default;

    // Set data pointers — call before Reset() or on data change
    void setData(std::vector<db::models::Subitem>* subs,
                 std::unordered_map<std::string, int>* proxyCounts,
                 std::unordered_map<std::string, int>* validProxyCounts = nullptr);

    // Clear all data
    void clear();

    // Detect the internal ID offset (same pattern as ProxyListModel)
    void detectIdOffset();

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
    // Non-owning pointers to the data owned by SubscriptionPanel
    std::vector<db::models::Subitem>* subscriptions_ = nullptr;
    std::unordered_map<std::string, int>* proxyCounts_ = nullptr;
    std::unordered_map<std::string, int>* validProxyCounts_ = nullptr;

    // Internal ID offset compensation
    unsigned int idOffset_ = 0;
};

#endif // UI_SUBSCRIPTION_LIST_MODEL_H