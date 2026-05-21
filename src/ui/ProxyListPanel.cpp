#include "ProxyListPanel.h"
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
    COL_ROWNUM   = 0,  // Display row number (1,2,3...)
    COL_TYPE     = 1,  // Protocol type (SS/VMess/VLess/etc.)
    COL_ADDRESS  = 2,
    COL_PORT     = 3,
    COL_DELAY    = 4,
    COL_FAILURES = 5,
    COL_REMARKS  = 6,
    COL_MESSAGE  = 7,
    COL_INDEXID  = 8,  // Hidden column - actual indexId for lookups (moved to last)
    COL_COUNT    = 9,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ProxyListPanel, wxPanel)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, ProxyListPanel::onContextMenu)
    EVT_MENU(wxID_ANY, ProxyListPanel::onTestProxy)
    EVT_MENU(wxID_ANY, ProxyListPanel::onGenerateConfig)
    EVT_DATAVIEW_SELECTION_CHANGED(wxID_ANY, ProxyListPanel::onSelectionChanged)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
ProxyListPanel::ProxyListPanel(wxWindow* parent, AppController* controller,
                                sqlite3* db)
    : wxPanel(parent, wxID_ANY),
      controller_(controller),
      db_(db),
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

    // Columns matching main-layout.svg: Row# | IndexId (hidden) | Host (100) | Port (70) | Latency (80) | Failures (80) | Remarks (160) | Message (160)
    listCtrl_->AppendTextColumn("#", COL_ROWNUM, wxDATAVIEW_CELL_INERT, 40);
    listCtrl_->AppendTextColumn("Type", COL_TYPE, wxDATAVIEW_CELL_INERT, 80);
    listCtrl_->AppendTextColumn("Host ↕",  COL_ADDRESS,  wxDATAVIEW_CELL_INERT, 100);
    listCtrl_->AppendTextColumn("Port",     COL_PORT,     wxDATAVIEW_CELL_INERT,  70);
    listCtrl_->AppendTextColumn("Latency ↕", COL_DELAY,  wxDATAVIEW_CELL_INERT, 80);
    listCtrl_->AppendTextColumn("Failures ↕", COL_FAILURES, wxDATAVIEW_CELL_INERT, 80);
    listCtrl_->AppendTextColumn("Remarks",  COL_REMARKS,  wxDATAVIEW_CELL_EDITABLE, 160);
    listCtrl_->AppendTextColumn("Message",  COL_MESSAGE,  wxDATAVIEW_CELL_INERT, 160);
    listCtrl_->AppendTextColumn("IndexId", COL_INDEXID, wxDATAVIEW_CELL_INERT, 120); // Hidden later if needed

    sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);
    SetSizer(sizer);

    // Bind custom events for completion handling
    Bind(wxEVT_PROXY_TEST_PROGRESS, &ProxyListPanel::onProxyTestProgress, this);
    Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ProxyListPanel::onColumnHeaderClick, this);
}

ProxyListPanel::~ProxyListPanel() = default;

// -------------------------------------------------------------------
// Helper: Convert ConfigType number to protocol name
// -------------------------------------------------------------------
static wxString configTypeToName(const wxString& type) {
    int t = 0;
    try { t = std::stoi(type.ToStdString()); } catch (...) { return "Unknown"; }
    switch (t) {
        case 1:  return "VMess";
        case 2:  return "Custom";
        case 3:  return "Shadowsocks";
        case 4:  return "SOCKS";
        case 5:  return "VLESS";
        case 6:  return "Trojan";
        case 7:  return "Hysteria2";
        case 8:  return "TUIC";
        case 9:  return "WireGuard";
        case 10: return "HTTP";
        case 11: return "Anytls";
        case 12: return "Naive";
        default: return "Unknown";
    }
}

// -------------------------------------------------------------------
// Load both proxy list and test results from DB.
// Rebuilds the entire store so the view is consistent.
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(const std::string& subId) {
    // If subId changed, we're loading a completely new subscription - reset sort
    // If subId is same (called from sortProxiesByColumn), preserve sort for reload
    bool subIdChanged = (subId != currentSubId_);
    
    currentSubId_ = subId;
    allProxies_ = controller_->loadProxies(subId);
    
    if (subIdChanged) {
        // New subscription - reset sort state
        sortState_.column = -1;
        sortState_.direction = SortDirection::None;
        proxies_ = allProxies_;
    }
    // else: keep existing sorted proxies_ (called from sortProxiesByColumn)
    
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
    int rowNum = 1;
    for (const auto& p : proxies_) {
        wxVector<wxVariant> row;
        row.push_back(wxVariant(wxString::Format("%d", rowNum++))); // COL_ROWNUM: display row number
        row.push_back(wxVariant(configTypeToName(p.configtype)));    // COL_TYPE: protocol name
        row.push_back(wxVariant(p.address));
        row.push_back(wxVariant(p.port));
        row.push_back(wxVariant(delayMap.count(p.indexid) ? delayMap[p.indexid] : "-"));
        row.push_back(wxVariant(std::to_string(failuresMap.count(p.indexid) ? failuresMap[p.indexid] : 0)));
        row.push_back(wxVariant(p.remarks));
        row.push_back(wxVariant(messageMap.count(p.indexid) ? messageMap[p.indexid] : ""));
        row.push_back(wxVariant(p.indexid));                  // COL_INDEXID: actual indexId for lookups (now last)
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
    
    // Debug output
    std::cout << "onColumnHeaderClick: column=" << col << std::endl;
    
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
    
    std::cout << "onColumnHeaderClick done: col=" << sortState_.column << ", dir=" << (int)sortState_.direction << std::endl;
}

void ProxyListPanel::sortProxiesByColumn(int col, SortDirection dir) {
    std::cout << "sortProxiesByColumn: col=" << col << ", dir=" << (int)dir << ", proxies_ size=" << proxies_.size() << std::endl;
    
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
            case COL_ROWNUM:
                cmp = a.indexid.compare(b.indexid);
                break;
            case COL_TYPE:
                cmp = a.configtype.compare(b.configtype);
                break;
            case COL_ADDRESS:
                cmp = a.address.compare(b.address);
                break;
            case COL_PORT:
                cmp = a.port.compare(b.port);
                break;
            case COL_DELAY: {
                int delayA = getDelay(a);
                int delayB = getDelay(b);
                cmp = (delayA > delayB) - (delayA < delayB);
                break;
            }
            case COL_FAILURES: {
                int failA = getFailures(a);
                int failB = getFailures(b);
                cmp = (failA > failB) - (failA < failB);
                break;
            }
            case COL_MESSAGE: {
                const std::string& msgA = getMessage(a);
                const std::string& msgB = getMessage(b);
                cmp = msgA.compare(msgB);
                break;
            }
case COL_REMARKS:
                cmp = a.remarks.compare(b.remarks);
                break;
            case COL_INDEXID:
                cmp = a.indexid.compare(b.indexid);
                break;
            default:
                cmp = a.indexid.compare(b.indexid);
                break;
        }
        return dir == SortDirection::Asc ? cmp < 0 : cmp > 0;
    });

    loadProxies(currentSubId_);
}

void ProxyListPanel::resetSort() {
    loadProxies(currentSubId_);
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

    // Send initial "Testing..." event to MainFrame for Delay column refresh
    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this) {
        wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    controller_->testSingleProxyAsync(indexId, this);
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

// -------------------------------------------------------------------
void ProxyListPanel::onSelectionChanged(wxDataViewEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    // Get the actual indexId from the hidden column
    wxVariant idxVar;
    store_->GetValue(idxVar, item, COL_INDEXID);
    std::string indexId = idxVar.GetString().ToStdString();

    // Find proxy by indexId
    db::models::Profileitem* proxy = nullptr;
    for (auto& p : proxies_) {
        if (p.indexid == indexId) {
            proxy = &p;
            break;
        }
    }

    // Build lookup from exItems_
    std::unordered_map<std::string, std::string> delayMap;
    std::unordered_map<std::string, std::string> messageMap;
    std::unordered_map<std::string, int> failuresMap;
    for (const auto& ex : exItems_) {
        delayMap[ex.indexid] = ex.delay;
        messageMap[ex.indexid] = ex.message;
        failuresMap[ex.indexid] = ex.consecutive_failures;
    }

    // Get proxy info
    std::string host = "";
    std::string port = "";
    std::string delay = "";
    std::string message = "";
    int failures = 0;
    std::string remarks = "";

    if (proxy) {
        host = proxy->address;
        port = proxy->port;
        delay = delayMap.count(indexId) ? delayMap[indexId] : "";
        message = messageMap.count(indexId) ? messageMap[indexId] : "";
        failures = failuresMap.count(indexId) ? failuresMap[indexId] : 0;
        remarks = proxy->remarks;
    }

// Notify parent via event
    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this && proxy) {
        ProxySelectionEvent selEvt(indexId, host, port,
                                  delay, message,
                                  failures, remarks);
        wxQueueEvent(topLevel, selEvt.Clone());
    }
    (void)event;
 }

 // -------------------------------------------------------------------
 // Filter proxies by search query (case-insensitive match on address, remark, indexId)
 // -------------------------------------------------------------------
void ProxyListPanel::filterBySearch(const wxString& query) {
    if (query.IsEmpty()) {
        proxies_ = allProxies_;  // Restore unfiltered list
    } else {
        std::string q = query.Lower().ToStdString();
        proxies_.clear();
        for (const auto& p : allProxies_) {
            if (p.address.find(q) != std::string::npos ||
                p.remarks.find(q) != std::string::npos ||
                p.indexid.find(q) != std::string::npos) {
                proxies_.push_back(p);
            }
        }
    }
    loadProxies(currentSubId_);
}
