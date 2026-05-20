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
    ProxyListPanel(wxWindow* parent, AppController* controller, sqlite3* db);
    ~ProxyListPanel() override;

    void loadProxies(const std::string& subId = "");
    void refreshResults();
    void selectProxyByIndexId(const std::string& indexId);

private:
    enum class SortDirection { None, Asc, Desc };

    void onContextMenu(wxDataViewEvent& event);
    void onTestProxy(wxCommandEvent& event);
    void onGenerateConfig(wxCommandEvent& event);
    void onProxyTestProgress(ProxyTestProgressEvent& event);
    void onColumnHeaderClick(wxDataViewEvent& event);
    void onSelectionChanged(wxDataViewEvent& event);
    void sortProxiesByColumn(int col, SortDirection dir);
    void resetSort();

    AppController* controller_;
    sqlite3* db_;

    wxDataViewCtrl* listCtrl_;
    wxDataViewListStore* store_;

    std::vector<db::models::Profileitem> proxies_;
    std::vector<db::models::ProfileExItem> exItems_;

    struct SortState {
        int column = -1;
        SortDirection direction = SortDirection::None;
    };
    SortState sortState_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_PROXY_LIST_PANEL_H
