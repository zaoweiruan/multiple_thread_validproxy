#include "ProxyListPanel.h"
#include "AppController.h"
#include "Events.h"
#include "Logger.h"

#include <wx/sizer.h>
#include <wx/dataview.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

// Context menu command IDs — must be unique to avoid wxID_ANY collisions
enum {
    ID_CONTEXT_TEST_PROXY   = wxID_HIGHEST + 400,
    ID_CONTEXT_EXPORT_SHARE = wxID_HIGHEST + 401,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ProxyListPanel, wxPanel)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, ProxyListPanel::onContextMenu)
    EVT_MENU(ID_CONTEXT_TEST_PROXY, ProxyListPanel::onTestProxy)
    EVT_MENU(ID_CONTEXT_EXPORT_SHARE, ProxyListPanel::onExportShareLink)
    EVT_DATAVIEW_SELECTION_CHANGED(wxID_ANY, ProxyListPanel::onSelectionChanged)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
ProxyListPanel::ProxyListPanel(wxWindow* parent, AppController* controller,
                                sqlite3* db)
    : wxPanel(parent, wxID_ANY),
      controller_(controller),
      db_(db),
      listCtrl_(nullptr),
      model_(nullptr)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    listCtrl_ = new wxDataViewCtrl(this, wxID_ANY,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxDV_ROW_LINES | wxDV_SINGLE);

    model_ = new ProxyListModel();
    listCtrl_->AssociateModel(model_);
    model_->DecRef();  // AssociateModel took ownership

    // Columns matching main-layout.svg: Row# | IndexId (hidden) | Host (100) |
    // Port (70) | Latency (80) | Failures (80) | Remarks (160) | Message (160)
    listCtrl_->AppendTextColumn("#",        COL_ROWNUM,   wxDATAVIEW_CELL_INERT,  40);
    listCtrl_->AppendTextColumn("Type",     COL_TYPE,     wxDATAVIEW_CELL_INERT,  80);
    listCtrl_->AppendTextColumn("Host ↕",   COL_ADDRESS,  wxDATAVIEW_CELL_INERT, 100);
    listCtrl_->AppendTextColumn("Port",     COL_PORT,     wxDATAVIEW_CELL_INERT,  70);
    listCtrl_->AppendTextColumn("Latency ↕", COL_DELAY,  wxDATAVIEW_CELL_INERT,  80);
    listCtrl_->AppendTextColumn("Failures ↕", COL_FAILURES, wxDATAVIEW_CELL_INERT, 80);
    listCtrl_->AppendTextColumn("Remarks",  COL_REMARKS,  wxDATAVIEW_CELL_EDITABLE, 160);
    listCtrl_->AppendTextColumn("Message",  COL_MESSAGE,  wxDATAVIEW_CELL_INERT, 160);
    listCtrl_->AppendTextColumn("IndexId",  COL_INDEXID,  wxDATAVIEW_CELL_INERT, 120);

    sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);
    SetSizer(sizer);

    // Bind custom events for completion handling
    Bind(wxEVT_PROXY_TEST_PROGRESS, &ProxyListPanel::onProxyTestProgress, this);
    Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ProxyListPanel::onColumnHeaderClick, this);
}

ProxyListPanel::~ProxyListPanel() = default;

// -------------------------------------------------------------------
// Load both proxy list and test results from DB.
// Updates the virtual model's data pointers and resets the view.
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(const std::string& subId) {
    // Always refresh proxy data from the controller (DB).
    // The subIdChanged guard previously kept stale data in proxies_ when the same
    // subscription was reloaded after an update/dedup/DB-switch, causing an empty
    // display even though the DB contained fresh rows.
    currentSubId_ = subId;
    allProxies_ = controller_->loadProxies(subId);
    Logger::write("[DIAG] ProxyListPanel::loadProxies(subId=" + subId + "): allProxies_="
                  + std::to_string(allProxies_.size()), LogLevel::INFO);

    // Reset sort state and copy fresh data unconditionally
    sortState_.column = -1;
    sortState_.direction = SortDirection::None;
    proxies_ = allProxies_;
    Logger::write("[DIAG] ProxyListPanel::loadProxies: proxies_=" + std::to_string(proxies_.size())
                  + " exItems_=" + std::to_string(exItems_.size()), LogLevel::INFO);

    exItems_ = controller_->loadProxyResults();
    Logger::write("[DIAG] ProxyListPanel::loadProxies: after exItems_ reload → exItems_="
                  + std::to_string(exItems_.size()), LogLevel::INFO);

    // Point the model at our data and reset the view
    model_->setData(&proxies_, &exItems_);
    // Workaround: some wxWidgets versions' wxDataViewIndexListModel::Reset(N)
    // does NOT properly clear the internal m_list; old entries (with larger IDs)
    // persist and cause getDataIndex(row) to return stale indices >= proxies_->size().
    // Double-Reset (0 then N) forces a full internal rebuild in all versions.
    Logger::write("[DIAG] ProxyListPanel::loadProxies: calling model_->Reset("
                  + std::to_string(proxies_.size()) + ")", LogLevel::INFO);
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
    model_->detectIdOffset();
    Logger::write("[DIAG] ProxyListPanel::loadProxies: model_->GetCount()="
                  + std::to_string(model_->GetCount()), LogLevel::INFO);

    // When no row is selected (startup, subscription switch), select the first
    // proxy and queue a ProxySelectionEvent directly.  On wxMSW, Select() during
    // initialisation may not fire a selection-changed notification, so we also
    // fire the event directly to guarantee the ProxyDetailPanel gets populated.
    if (!proxies_.empty()) {
        if (!listCtrl_->GetSelection().IsOk()) {
            selectFirstProxy();
        }
    }
}

// -------------------------------------------------------------------
// Refresh only the Delay/Message/Failures columns by reloading exItems_ from DB.
// Proxies list and user selection are preserved.
// Model's lookup maps are rebuilt and the view is notified to redraw.
// -------------------------------------------------------------------
void ProxyListPanel::refreshResults() {
    exItems_ = controller_->loadProxyResults();

    // Rebuild lookup maps inside the model (model has pointer to exItems_)
    model_->rebuildMaps();

    // Notify the view that test-result cells changed
    model_->notifyTestResultChanged();
}

// -------------------------------------------------------------------
// Select the row whose indexId matches and scroll it into view.
// Linear scan — acceptable for expected proxy counts.
// -------------------------------------------------------------------
void ProxyListPanel::selectProxyByIndexId(const std::string& indexId) {
    int row = model_->findRowByIndexId(indexId);
    if (row >= 0) {
        wxDataViewItem item = model_->GetItem(static_cast<unsigned int>(row));
        listCtrl_->Select(item);
        listCtrl_->EnsureVisible(item);
    }
}

// -------------------------------------------------------------------
// Column header click handler + virtual model sorting
// -------------------------------------------------------------------
void ProxyListPanel::onColumnHeaderClick(wxDataViewEvent& event) {
    int col = event.GetColumn();

    Logger::write("[ProxyListPanel] Column header click: column=" + std::to_string(col), LogLevel::DEBUG);

    // Cycle direction: None -> Asc -> Desc -> None
    if (sortState_.column == col) {
        // Same column: cycle direction
        switch (sortState_.direction) {
            case SortDirection::Asc:
                sortState_.direction = SortDirection::Desc;
                break;
            case SortDirection::Desc:
                sortState_.direction = SortDirection::None;
                sortState_.column = -1;
                break;
            default:
                sortState_.direction = SortDirection::Asc;
                break;
        }
    } else {
        sortState_.column = col;
        sortState_.direction = SortDirection::Asc;
    }

    if (sortState_.direction != SortDirection::None) {
        // Set the sort indicator on the column and trigger re-sort.
        // The model's Compare() is called by the view during sorting.
        wxDataViewColumn* dvCol = listCtrl_->GetColumn(col);
        if (dvCol) {
            dvCol->SetSortOrder(sortState_.direction == SortDirection::Asc);
        }
        model_->Resort();
    } else {
        // Clear sort — remove indicator and restore identity order
        wxDataViewColumn* currentSort = listCtrl_->GetSortingColumn();
        if (currentSort) {
            currentSort->UnsetAsSortKey();
        }
        model_->Reset(0);
        model_->Reset(static_cast<unsigned int>(proxies_.size()));
        model_->detectIdOffset();
    }

    Logger::write("[ProxyListPanel] Column header click done: col=" + std::to_string(sortState_.column)
                  + ", dir=" + std::to_string(static_cast<int>(sortState_.direction)), LogLevel::DEBUG);
}

// -------------------------------------------------------------------
void ProxyListPanel::onContextMenu(wxDataViewEvent& event) {
    wxDataViewItem item = event.GetItem();
    if (!item.IsOk()) return;

    wxMenu menu;
    menu.Append(ID_CONTEXT_TEST_PROXY, "测试此代理");
    menu.Append(ID_CONTEXT_EXPORT_SHARE, "有效代理分享");
    PopupMenu(&menu);
}

// -------------------------------------------------------------------
void ProxyListPanel::onTestProxy(wxCommandEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    unsigned int viewRow = model_->GetRow(item);
    if (viewRow == static_cast<unsigned int>(-1)) return;

    std::string indexId = model_->getIndexIdAtRow(viewRow);
    if (indexId.empty()) return;

    // Send initial "Testing..." event to MainFrame for Delay column refresh
    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this) {
        wxQueueEvent(topLevel, new ProxyTestProgressEvent(0, 1, indexId, "", "", "Testing…", false));
    }

    controller_->testSingleProxyAsync(indexId, this);
    (void)event; // id dispatched in menu
}

// -------------------------------------------------------------------
void ProxyListPanel::onExportShareLink(wxCommandEvent& event) {
    auto [ok, count, filename] = controller_->exportShareLinks();
    wxString msg;
    if (ok && count > 0) {
        msg = wxString::Format("导出%d个有效代理至文件%s", count, filename);
    } else if (ok) {
        msg = "没有有效代理可导出。";
    } else {
        msg = "导出分享链接失败。";
    }
    wxMessageBox(msg, "有效代理分享", wxOK | (ok ? wxICON_INFORMATION : wxICON_WARNING));
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

    unsigned int viewRow = model_->GetRow(item);
    if (viewRow == static_cast<unsigned int>(-1)) return;

    const auto* proxy = model_->getProfileAtRow(viewRow);
    if (!proxy) return;

    const std::string& indexId = proxy->indexid;
    std::string delay   = model_->getDelay(indexId);
    std::string message = model_->getMessage(indexId);
    int failures        = model_->getFailures(indexId);

    // Notify parent via event
    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this) {
        ProxySelectionEvent selEvt(indexId, proxy->address, proxy->port,
                                   delay, message, failures, proxy->remarks);
        wxQueueEvent(topLevel, selEvt.Clone());
    }
    (void)event;
}

// -------------------------------------------------------------------
// Filter proxies by search query (case-insensitive match on address,
// remark, indexId)
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

    // Update model and reset view (preserving sort state if active)
    model_->setData(&proxies_, &exItems_);
    if (sortState_.direction != SortDirection::None) {
        // Double-Reset workaround: Reset(N) alone may keep stale m_list entries
        // from a previous state in some wxWidgets versions, causing getDataIndex(row)
        // to return indices >= proxies_->size() — the last record(s) appear blank.
        model_->Reset(0);
        model_->Reset(static_cast<unsigned int>(proxies_.size()));
        model_->detectIdOffset();
        wxDataViewColumn* dvCol = listCtrl_->GetColumn(sortState_.column);
        if (dvCol) {
            dvCol->SetSortOrder(sortState_.direction == SortDirection::Asc);
        }
        model_->Resort();
    } else {
        model_->Reset(0);
        model_->Reset(static_cast<unsigned int>(proxies_.size()));
        model_->detectIdOffset();
    }
}

// -------------------------------------------------------------------
// Helper: select the first proxy in the list and fire a selection event
// -------------------------------------------------------------------
void ProxyListPanel::selectFirstProxy() {
    if (proxies_.empty()) return;

    wxDataViewItem firstItem = model_->GetItem(0);
    listCtrl_->Select(firstItem);

    const auto* proxy = model_->getProfileAtRow(0);
    if (!proxy) return;

    const std::string& indexId = proxy->indexid;
    std::string delay   = model_->getDelay(indexId);
    std::string message = model_->getMessage(indexId);
    int failures        = model_->getFailures(indexId);

    wxWindow* topLevel = wxGetTopLevelParent(this);
    if (topLevel && topLevel != this) {
        ProxySelectionEvent selEvt(indexId, proxy->address, proxy->port,
                                   delay, message, failures, proxy->remarks);
        wxQueueEvent(topLevel, selEvt.Clone());
    }
}
