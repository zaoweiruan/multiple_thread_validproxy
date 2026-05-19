#ifndef UI_SUBSCRIPTION_PANEL_H
#define UI_SUBSCRIPTION_PANEL_H

#include <wx/wx.h>
#include <wx/dataview.h>
#include <wx/menu.h>

#include <string>
#include <vector>

#include "Subitem.h"

class AppController;

// ---------------------------------------------------------------
// SubscriptionPanel — left panel showing subscription list
// ---------------------------------------------------------------
class SubscriptionPanel : public wxPanel {
public:
    SubscriptionPanel(wxWindow* parent, AppController* controller);

    void loadSubscriptions();
    std::string getSelectedSubId() const;
    void showAddDialog();

private:
    void onSelectionChanged(wxDataViewEvent& event);
    void onContextMenu(wxDataViewEvent& event);
    void onAddSubscription(wxCommandEvent& event);
    void onEditSubscription(wxCommandEvent& event);
    void onDeleteSubscription(wxCommandEvent& event);
    void onUpdateSubscription(wxCommandEvent& event);
    void onTestSubscription(wxCommandEvent& event);
    void onImportSubscription(wxCommandEvent& event);

    void showEditDialog(const db::models::Subitem& sub);
    bool confirmDelete(const std::string& id, const std::string& remarks);

    AppController* controller_;
    wxDataViewCtrl* listCtrl_;
    wxDataViewListStore* store_;
    std::vector<db::models::Subitem> subs_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_SUBSCRIPTION_PANEL_H
