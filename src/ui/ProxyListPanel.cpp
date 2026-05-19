#include "ProxyListPanel.h"
#include "TestPanel.h"
#include "AppController.h"
#include "Events.h"

#include <wx/sizer.h>
#include <wx/dataview.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

// -------------------------------------------------------------------
// Column indices in the wxDataViewListStore
// -------------------------------------------------------------------
enum {
    COL_INDEXID  = 0,
    COL_TYPE     = 1,
    COL_ADDRESS  = 2,
    COL_DELAY    = 3,
    COL_SPEED    = 4,
    COL_REMARKS  = 5,
    COL_COUNT    = 6,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ProxyListPanel, wxPanel)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, ProxyListPanel::onContextMenu)
    EVT_MENU(wxID_ANY, ProxyListPanel::onTestProxy)
    EVT_MENU(wxID_ANY, ProxyListPanel::onGenerateConfig)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
ProxyListPanel::ProxyListPanel(wxWindow* parent, AppController* controller,
                               sqlite3* db, TestPanel* testPanel)
    : wxPanel(parent, wxID_ANY),
      controller_(controller),
      db_(db),
      testPanel_(testPanel),
      listCtrl_(nullptr),
      store_(nullptr)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    listCtrl_ = new wxDataViewCtrl(this, wxID_ANY,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxDV_ROW_LINES | wxDV_SINGLE);

    store_ = new wxDataViewListStore();
    listCtrl_->AssociateModel(store_);
    store_->DecRef();  // AssociateModel took ownership

    // Columns
    listCtrl_->AppendTextColumn("IndexId",       COL_INDEXID,  wxDATAVIEW_CELL_INERT, 110);
    listCtrl_->AppendTextColumn("Type",          COL_TYPE,     wxDATAVIEW_CELL_INERT,  80);
    listCtrl_->AppendTextColumn("Address",       COL_ADDRESS,  wxDATAVIEW_CELL_INERT, 160);
    listCtrl_->AppendTextColumn("Delay (ms)",    COL_DELAY,    wxDATAVIEW_CELL_INERT, 100, wxALIGN_RIGHT);
    listCtrl_->AppendTextColumn("Speed",         COL_SPEED,    wxDATAVIEW_CELL_INERT,  80, wxALIGN_RIGHT);
    listCtrl_->AppendTextColumn("Remarks",       COL_REMARKS,  wxDATAVIEW_CELL_EDITABLE, 200);

    sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);
    SetSizer(sizer);

    // Bind custom events for completion handling
    Bind(wxEVT_PROXY_TEST_PROGRESS, &ProxyListPanel::onProxyTestProgress, this);
}

ProxyListPanel::~ProxyListPanel() = default;

// -------------------------------------------------------------------
// Load both proxy list and test results from DB.
// Rebuilds the entire store so the view is consistent.
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(const std::string& subId) {
    proxies_ = controller_->loadProxies(subId);
    exItems_ = controller_->loadProxyResults();

    // Build lookup: indexId -> delay_str
    std::unordered_map<std::string, std::string> delayMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
    }

    store_->DeleteAllItems();
    for (const auto& p : proxies_) {
        wxVector<wxVariant> row;
        row.push_back(wxVariant(p.indexid));
        row.push_back(wxVariant(p.configtype));
        row.push_back(wxVariant(p.address + ":" + p.port));
        row.push_back(wxVariant(delayMap.count(p.indexid) ? delayMap[p.indexid] : "-"));
        row.push_back(wxVariant(""));
        row.push_back(wxVariant(p.remarks));
        store_->AppendItem(row);
    }
}

// -------------------------------------------------------------------
// Refresh only the Delay column by reloading exItems_ from DB.
// Proxies list and user selection are preserved.
// Each row is updated in-place by indexId key, then the sink row (if
// any) is removed so the view and the data stay in sync.
// -------------------------------------------------------------------
void ProxyListPanel::refreshResults() {
    exItems_ = controller_->loadProxyResults();

    // Build delay lookup from freshly-loaded exItems_
    std::unordered_map<std::string, std::string> delayMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
    }

    // Update existing rows in-place
    for (unsigned row = 0; row < store_->GetCount(); ++row) {
        wxDataViewItem item = store_->GetItem(row);
        wxVariant idxVar;
        store_->GetValue(idxVar, item, COL_INDEXID);
        wxString currentDelay = delayMap.count(idxVar.GetString().ToStdString())
                                    ? wxString(delayMap[idxVar.GetString().ToStdString()])
                                    : "-";
        store_->SetValue(wxVariant(currentDelay), item, COL_DELAY);
    }
}

// -------------------------------------------------------------------
// Select the row whose indexId matches and scroll it into view.
// Linear scan — acceptable for expected proxy counts.
// -------------------------------------------------------------------
void ProxyListPanel::selectProxyByIndexId(const std::string& indexId) {
    for (unsigned row = 0; row < store_->GetCount(); ++row) {
        wxDataViewItem item = store_->GetItem(row);
        wxVariant idxVar;
        store_->GetValue(idxVar, item, COL_INDEXID);
        if (idxVar.GetString().ToStdString() == indexId) {
            listCtrl_->Select(item);
            listCtrl_->EnsureVisible(item);
            break;
        }
    }
}

// -------------------------------------------------------------------
void ProxyListPanel::onContextMenu(wxDataViewEvent& event) {
    wxDataViewItem item = event.GetItem();
    if (!item.IsOk()) return;

    wxMenu menu;
    menu.Append(wxID_ANY, "Test");
    menu.Append(wxID_ANY, "Generate Config");
    PopupMenu(&menu);
}

// -------------------------------------------------------------------
void ProxyListPanel::onTestProxy(wxCommandEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    wxVariant idxVar;
    store_->GetValue(idxVar, item, COL_INDEXID);
    std::string indexId = idxVar.GetString().ToStdString();

    // Relay test request to TestPanel
    if (testPanel_) {
        wxQueueEvent(testPanel_, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    controller_->testSubscriptionAsync(indexId, testPanel_);
    (void)event; // id dispatched in menu
}

// -------------------------------------------------------------------
void ProxyListPanel::onGenerateConfig(wxCommandEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    wxVariant idxVar;
    store_->GetValue(idxVar, item, COL_INDEXID);
    std::string indexId = idxVar.GetString().ToStdString();

    bool ok = controller_->generateConfig(indexId);
    wxMessageBox(ok ? "Config generated successfully." : "Config generation failed.",
                 "Generate Config", wxOK | (ok ? wxICON_INFORMATION : wxICON_WARNING));
    (void)event;
}

// -------------------------------------------------------------------
void ProxyListPanel::onProxyTestProgress(ProxyTestProgressEvent& event) {
    // Refresh delay column on test completion
    if (event.isCompleted()) {
        refreshResults();
    }
    event.Skip();
}
