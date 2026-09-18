#ifndef UI_SUBSCRIPTION_PANEL_H
#define UI_SUBSCRIPTION_PANEL_H

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/menu.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "Subitem.h"
#include "SubscriptionListModel.h"

class AppController;

// ---------------------------------------------------------------
// SubscriptionPanel — left panel showing subscription list
// ---------------------------------------------------------------
class SubscriptionPanel : public wxPanel {
public:
    SubscriptionPanel(wxWindow* parent, AppController* controller);

    void loadSubscriptions();
    void loadSubscriptions(const std::vector<db::models::Subitem>& subs,
                           const std::unordered_map<std::string, int>& proxyCounts);
    std::string getSelectedSubId() const;
    void selectSubBySubId(const std::string& subId);
    const std::vector<db::models::Subitem>& getSubscriptions() const { return subs_; }
    void RefreshContextMenu();
    void filterBySearch(const wxString& query);

private:
    void onSelectionChanged(wxDataViewEvent& event);
    void onContextMenu(wxDataViewEvent& event);
    void onRefreshSubscription(wxCommandEvent& event);
    void onEditSubscription(wxCommandEvent& event);
    void onDeleteSubscription(wxCommandEvent& event);
    void onDeleteProxies(wxCommandEvent& event);
    void onUpdateSubscription(wxCommandEvent& event);
    void onTestSubscription(wxCommandEvent& event);
    void onImportSubscription(wxCommandEvent& event);
    void onColumnHeaderClick(wxDataViewEvent& event);
    // Resolve a model column index (as returned by wxDataViewEvent::GetColumn)
    // to the actual wxDataViewColumn*, scanning visual positions.  See
    // ProxyListPanel::resolveColumnByModel for the rationale.
    wxDataViewColumn* resolveColumnByModel(int modelCol) const;

    void showEditDialog(const db::models::Subitem& sub);
    bool confirmDelete(const std::string& id, const std::string& remarks);
    static std::string formatUpdateTime(const std::string& updatetime);

    void updateSubscriptionList(const std::vector<db::models::Subitem>& subs,
                                const std::unordered_map<std::string, int>& proxyCounts);

    AppController* controller_;
    wxDataViewCtrl* listCtrl_;
    SubscriptionListModel* model_;
    std::vector<db::models::Subitem> subs_;
    std::unordered_map<std::string, int> proxyCounts_;
    std::unordered_map<std::string, int> validProxyCounts_;

    // Unfiltered originals for search filtering
    std::vector<db::models::Subitem> allSubs_;
    std::unordered_map<std::string, int> allProxyCounts_;
    std::unordered_map<std::string, int> allValidProxyCounts_;
    SortState sortState_;

    // Double-click detection (MSW wxDataViewMainWindow lacks CS_DBLCLKS)
    wxDataViewItem lastSelItem_;
    wxLongLong lastSelTime_{0};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_SUBSCRIPTION_PANEL_H
