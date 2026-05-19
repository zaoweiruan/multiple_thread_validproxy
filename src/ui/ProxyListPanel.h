#ifndef UI_PROXY_LIST_PANEL_H
#define UI_PROXY_LIST_PANEL_H

#include <wx/wx.h>
#include <wx/dataview.h>

#include <string>
#include <vector>

#include "Profileitem.h"
#include "ProfileExItem.h"
#include "AppController.h"
#include "Events.h"

// Forward declarations
class TestPanel;

// ---------------------------------------------------------------
// ProxyListPanel — right panel showing the proxy list with Delay
// column populated from ProfileExItem.
//
// Two-phase store pattern:
//   allProxies_ / exItems_  — source of truth (stays in DB)
//   model_                   — shallow copy used by wxDataViewCtrl
// ---------------------------------------------------------------
class ProxyListPanel : public wxPanel {
public:
    ProxyListPanel(wxWindow* parent, AppController* controller, sqlite3* db,
                   TestPanel* testPanel);
    ~ProxyListPanel() override;

    // Reload proxies_ + exItems_ from DB and refresh the list control.
    // Called on initial load and when a subscription is selected.
    void loadProxies(const std::string& subId = "");

    // Reload exItems_ (test results / delay) from DB and push into the
    // store without touching proxies_ — preserves scroll / selection.
    // Called from MainFrame when a test operation completes.
    void refreshResults();

    // Find the row whose indexId matches and select + reveal it.
    void selectProxyByIndexId(const std::string& indexId);

private:
    void onContextMenu(wxDataViewEvent& event);
    void onTestProxy(wxCommandEvent& event);
    void onGenerateConfig(wxCommandEvent& event);
    void onProxyTestProgress(ProxyTestProgressEvent& event);

    AppController* controller_;
    sqlite3* db_;
    TestPanel* testPanel_;

    wxDataViewCtrl* listCtrl_;
    wxDataViewListStore* store_;

    // Source data
    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_PROXY_LIST_PANEL_H
