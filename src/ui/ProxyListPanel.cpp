#include "ProxyListPanel.h"
#include "TestPanel.h"
#include "AppController.h"
#include "Events.h"

#include <wx/sizer.h>
#include <wx/dataview.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <unordered_map>

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
    COL_MESSAGE  = 6,
    COL_FAILURES = 7,
    COL_COUNT    = 8,
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
    listCtrl_->AppendTextColumn("Message",       COL_MESSAGE,  wxDATAVIEW_CELL_INERT, 150);
    listCtrl_->AppendTextColumn("Failures",      COL_FAILURES, wxDATAVIEW_CELL_INERT,  80, wxALIGN_RIGHT);

sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);
     SetSizer(sizer);

     // Bind custom events for completion handling
     Bind(wxEVT_PROXY_TEST_PROGRESS, &ProxyListPanel::onProxyTestProgress, this);
     Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ProxyListPanel::onColumnHeaderClick, this);
}

ProxyListPanel::~ProxyListPanel() = default;

// -------------------------------------------------------------------
// Load both proxy list and test results from DB.
// Rebuilds the entire store so the view is consistent.
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(const std::string& subId) {
    proxies_ = controller_->loadProxies(subId);
    exItems_ = controller_->loadProxyResults();

    // Build lookups: indexId -> delay_str, message, consecutive_failures
    std::unordered_map<std::string, std::string> delayMap;
    std::unordered_map<std::string, std::string> messageMap;
    std::unordered_map<std::string, int> failuresMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
        messageMap[ex.indexid] = ex.message;
        failuresMap[ex.indexid] = ex.consecutive_failures;
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
        row.push_back(wxVariant(messageMap.count(p.indexid) ? messageMap[p.indexid] : ""));
        row.push_back(wxVariant(std::to_string(failuresMap.count(p.indexid) ? failuresMap[p.indexid] : 0)));
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

    // Build lookups from freshly-loaded exItems_
    std::unordered_map<std::string, std::string> delayMap;
    std::unordered_map<std::string, std::string> messageMap;
    std::unordered_map<std::string, int> failuresMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
        messageMap[ex.indexid] = ex.message;
        failuresMap[ex.indexid] = ex.consecutive_failures;
    }

    // Update existing rows in-place
    for (unsigned row = 0; row < store_->GetCount(); ++row) {
        wxDataViewItem item = store_->GetItem(row);
        wxVariant idxVar;
        store_->GetValue(idxVar, item, COL_INDEXID);
        std::string indexId = idxVar.GetString().ToStdString();

        wxString currentDelay = delayMap.count(indexId)
                                     ? wxString(delayMap[indexId])
                                     : "-";
        store_->SetValue(wxVariant(currentDelay), item, COL_DELAY);

        store_->SetValue(wxVariant(messageMap.count(indexId) ? messageMap[indexId] : ""), item, COL_MESSAGE);
        store_->SetValue(wxVariant(std::to_string(failuresMap.count(indexId) ? failuresMap[indexId] : 0)), item, COL_FAILURES);
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
// Column header click handler + sorting logic
// -------------------------------------------------------------------
void ProxyListPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();
    
    // Cycle direction: None -> Asc -> Desc -> None
    if (sortState_.column == col) {
        sortState_.direction = (sortState_.direction == SortDirection::Asc)
            ? SortDirection::Desc
            : (sortState_.direction == SortDirection::Desc
                ? SortDirection::None
                : SortDirection::Asc);
    } else {
        sortState_.column = col;
        sortState_.direction = SortDirection::Asc;
    }
    
    if (sortState_.direction != SortDirection::None) {
        sortProxiesByColumn(sortState_.column, sortState_.direction);
    } else {
        resetSort();
    }
}

void ProxyListPanel::sortProxiesByColumn(int col, SortDirection dir) {
    auto getDelay = [this](const db::models::Profileitem& p) -> int {
        for (const auto& ex : exItems_) {
            if (ex.indexid == p.indexid) {
                try { return std::stoi(ex.delay); } catch (...) { return 0; }
            }
        }
        return 0;
    };
    
    auto getMessage = [this](const db::models::Profileitem& p) -> const std::string& {
        for (const auto& ex : exItems_) {
            if (ex.indexid == p.indexid) {
                return ex.message;
            }
        }
        static const std::string empty;
        return empty;
    };
    
    auto getFailures = [this](const db::models::Profileitem& p) -> int {
        for (const auto& ex : exItems_) {
            if (ex.indexid == p.indexid) {
                return ex.consecutive_failures;
            }
        }
        return 0;
    };
    
    std::sort(proxies_.begin(), proxies_.end(), [col, dir, &getDelay, &getMessage, &getFailures](const auto& a, const auto& b) {
        int cmp = 0;
        switch (col) {
            case COL_ADDRESS:
                cmp = a.address.compare(b.address);
                break;
            case COL_DELAY: {
                int delayA = getDelay(a);
                int delayB = getDelay(b);
                cmp = (delayA > delayB) - (delayA < delayB);
                break;
            }
            case COL_SPEED:
                cmp = a.configtype.compare(b.configtype);
                break;
            case COL_MESSAGE: {
                const std::string& msgA = getMessage(a);
                const std::string& msgB = getMessage(b);
                cmp = msgA.compare(msgB);
                break;
            }
            case COL_FAILURES: {
                int failA = getFailures(a);
                int failB = getFailures(b);
                cmp = (failA > failB) - (failA < failB);
                break;
            }
            default:
                cmp = a.indexid.compare(b.indexid);
                break;
        }
        return dir == SortDirection::Asc ? cmp < 0 : cmp > 0;
    });
    
    loadProxies();
}

void ProxyListPanel::resetSort() {
    loadProxies();
}

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

    // Send initial "Testing..." event to TestPanel
    if (testPanel_) {
        wxQueueEvent(testPanel_, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    // FIX: Also send to MainFrame so it can refresh Delay column on completion
    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this) {
        wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    controller_->testSingleProxyAsync(indexId, testPanel_);
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
