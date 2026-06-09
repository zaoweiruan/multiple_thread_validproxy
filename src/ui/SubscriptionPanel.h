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
    const std::vector<db::models::Subitem>& getSubscriptions() const { return subs_; }

private:
    void onSelectionChanged(wxDataViewEvent& event);
    void onContextMenu(wxDataViewEvent& event);
    void onRefreshSubscription(wxCommandEvent& event);
    void onEditSubscription(wxCommandEvent& event);
    void onDeleteSubscription(wxCommandEvent& event);
    void onUpdateSubscription(wxCommandEvent& event);
    void onTestSubscription(wxCommandEvent& event);
    void onImportSubscription(wxCommandEvent& event);
    void onColumnHeaderClick(wxDataViewEvent& event);

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
    SortState sortState_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_SUBSCRIPTION_PANEL_H
