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
#include "ProxyListModel.h"

// Forward declarations

// ---------------------------------------------------------------
// ProxyListPanel — right panel showing the proxy list with Delay
// column populated from ProfileExItem.
//
// Virtual model pattern:
//   allProxies_ / exItems_  — source of truth (owned by panel)
//   model_                   — wxDataViewIndexListModel providing
//                               viewport-lazy access via non-owning
//                               pointers to the above vectors.
// ---------------------------------------------------------------
class ProxyListPanel : public wxPanel {
public:
    ProxyListPanel(wxWindow* parent, AppController* controller, sqlite3* db);
    ~ProxyListPanel() override;

    void loadProxies(const std::string& subId = "");
    void refreshResults();
    void selectProxyByIndexId(const std::string& indexId);
    void filterBySearch(const wxString& query);

private:
    enum class SortDirection { None, Asc, Desc };

    void onContextMenu(wxDataViewEvent& event);
    void onTestProxy(wxCommandEvent& event);
    void onExportShareLink(wxCommandEvent& event);
    void onProxyTestProgress(ProxyTestProgressEvent& event);
    void onColumnHeaderClick(wxDataViewEvent& event);
    void onSelectionChanged(wxDataViewEvent& event);

    void selectFirstProxy();

    AppController* controller_;
    sqlite3* db_;

    wxDataViewCtrl* listCtrl_;
    ProxyListModel* model_;

    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;
    std::vector<db::models::Profileitem> allProxies_;  // Unfiltered list for search
    std::string currentSubId_;  // Track current subscription filter for reload

    struct SortState {
        int column = -1;
        SortDirection direction = SortDirection::None;
    };
    SortState sortState_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_PROXY_LIST_PANEL_H
