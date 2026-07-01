#include "ProxyListPanel.h"
#include "AppController.h"
#include "Events.h"
#include "Logger.h"
#include "Utils.h"

#include <wx/sizer.h>
#include <wx/dataview.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

// Double-click timeout for MSW workaround.
// wxDataViewMainWindow registers its window class without CS_DBLCLKS on MSW,
// so wxEVT_DATAVIEW_ITEM_ACTIVATED never fires on double-click.
// We detect double-click via rapid consecutive selection of the same item instead.
#ifdef __WXMSW__
static const long DBLCLICK_TIMEOUT_MS = static_cast<long>(::GetDoubleClickTime());
#else
static const long DBLCLICK_TIMEOUT_MS = 500;
#endif

// Context menu command IDs — must be unique to avoid wxID_ANY collisions
enum {
    ID_CONTEXT_TEST_PROXY    = wxID_HIGHEST + 400,
    ID_CONTEXT_EXPORT_SHARE  = wxID_HIGHEST + 401,
    ID_CONTEXT_START_PROXY   = wxID_HIGHEST + 402,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ProxyListPanel, wxPanel)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, ProxyListPanel::onContextMenu)
    EVT_MENU(ID_CONTEXT_TEST_PROXY, ProxyListPanel::onTestProxy)
    EVT_MENU(ID_CONTEXT_EXPORT_SHARE, ProxyListPanel::onExportShareLink)
    EVT_MENU(ID_CONTEXT_START_PROXY, ProxyListPanel::onStartProxy)
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
    Bind(wxEVT_STANDALONE_PROXY, &ProxyListPanel::onStandaloneProxyEvent, this);
    Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ProxyListPanel::onColumnHeaderClick, this);

    // Double-click to start proxy
    listCtrl_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent&) {
        wxCommandEvent dummy;
        onStartProxy(dummy);
    });
}

ProxyListPanel::~ProxyListPanel() = default;

// -------------------------------------------------------------------
// UI update helper — sets member data, resets model, selects first row.
// Called by both loadProxies overloads.
// -------------------------------------------------------------------
void ProxyListPanel::updateProxyList(const std::vector<db::models::Profileitem>& proxies,
                                      const std::vector<db::models::ProfileExItem>& exItems,
                                      const std::string& subId) {
    currentSubId_ = subId;
    allProxies_ = proxies;

    Logger::write("[DIAG] ProxyListPanel::updateProxyList(subId=" + subId + "): allProxies_="
                  + std::to_string(allProxies_.size()), LogLevel::TRACE);

    sortState_.column = -1;
    sortState_.direction = SortDirection::None;
    proxies_ = proxies;
    exItems_ = exItems;

    Logger::write("[DIAG] ProxyListPanel::updateProxyList: proxies_=" + std::to_string(proxies_.size())
                  + " exItems_=" + std::to_string(exItems_.size()), LogLevel::TRACE);

    model_->setData(&proxies_, &exItems_);
    Logger::write("[DIAG] ProxyListPanel::updateProxyList: calling model_->Reset("
                  + std::to_string(proxies_.size()) + ")", LogLevel::TRACE);
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
    model_->detectIdOffset();
    Logger::write("[DIAG] ProxyListPanel::updateProxyList: model_->GetCount()="
                  + std::to_string(model_->GetCount()), LogLevel::TRACE);

    if (!proxies_.empty()) {
        if (!listCtrl_->GetSelection().IsOk()) {
            selectFirstProxy();
        }
    }
}

// -------------------------------------------------------------------
// Load both proxy list and test results from DB.
// Updates the virtual model's data pointers and resets the view.
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(const std::string& subId) {
    std::vector<db::models::Profileitem> proxies = controller_->loadProxies(subId);
    std::vector<db::models::ProfileExItem> exItems = controller_->loadProxyResults();
    updateProxyList(proxies, exItems, subId);
}

// -------------------------------------------------------------------
// Accept pre-fetched proxy data directly (no DB read).
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(std::vector<db::models::Profileitem> proxies,
                                  std::vector<db::models::ProfileExItem> exItems,
                                  const std::string& subId) {
    updateProxyList(proxies, exItems, subId);
}

// -------------------------------------------------------------------
// Refresh only the Delay/Message/Failures columns by reloading exItems_ from DB.
// Proxies list and user selection are preserved.
// Model's lookup maps are rebuilt and the view is notified to redraw.
// -------------------------------------------------------------------
void ProxyListPanel::refreshResults() {
    exItems_ = controller_->loadProxyResults();

    model_->rebuildMaps();

    listCtrl_->Refresh();
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
    if (!item.IsOk()) {
        event.Skip();
        return;
    }

    // Determine if selected proxy already has a standalone instance running
    wxDataViewItem selItem = listCtrl_->GetSelection();
    std::string selIndexId;
    if (selItem.IsOk()) {
        unsigned int viewRow = model_->GetRow(selItem);
        if (viewRow != static_cast<unsigned int>(-1)) {
            selIndexId = model_->getIndexIdAtRow(viewRow);
        }
    }

    wxMenu menu;
    menu.Append(ID_CONTEXT_TEST_PROXY, "测试此代理");
    menu.Append(ID_CONTEXT_EXPORT_SHARE, "有效代理分享");
    menu.Append(ID_CONTEXT_START_PROXY, "开启代理");
    PopupMenu(&menu);
    event.Skip();
}

// -------------------------------------------------------------------
void ProxyListPanel::onTestProxy(wxCommandEvent& event) {
    // Prevent testing during active operations
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }

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
    // Prevent export during active operations
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }

    std::tuple<bool, int, std::string> exportResult = controller_->exportShareLinks();
    bool ok = std::get<0>(exportResult);
    int count = std::get<1>(exportResult);
    wxString msg;
    if (ok && count > 0) {
        msg = wxString::Format("导出%d个有效代理至文件%s", count, std::get<2>(exportResult));
    } else if (ok) {
        msg = "没有有效代理可导出。";
    } else {
        msg = "导出分享链接失败。";
    }
    wxMessageBox(msg, "有效代理分享", wxOK | (ok ? wxICON_INFORMATION : wxICON_WARNING));
    (void)event;
}

// -------------------------------------------------------------------
void ProxyListPanel::onStartProxy(wxCommandEvent& event) {
    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    unsigned int viewRow = model_->GetRow(item);
    if (viewRow == static_cast<unsigned int>(-1)) return;

    std::string indexId = model_->getIndexIdAtRow(viewRow);
    if (indexId.empty()) return;

    if (!controller_) return;

    // Check if the configured SOCKS port is available
    config::AppConfig cfg = controller_->getConfig();
    int desiredPort = cfg.proxy.socks_base_port;
    Logger::write("[UI] onStartProxy: checking desiredPort=" + std::to_string(desiredPort)
                  + " isPortAvailable=" + (utils::isPortAvailable(desiredPort) ? "true" : "false"),
                  LogLevel::INFO);
    int actualPort = desiredPort;

    if (!utils::isPortAvailable(desiredPort)) {
        // Port occupied — find the next free port
        int freePort = utils::findAvailablePort(desiredPort + 1);
        if (freePort < 0) {
            wxMessageDialog dlg(this,
                wxString::Format("端口 %d 已被占用，且无法找到其他空闲端口。", desiredPort),
                "端口检查", wxOK | wxICON_WARNING);
            dlg.CentreOnScreen();
            dlg.ShowModal();
            Logger::write("[UI] No free port found for standalone proxy (base port " + std::to_string(desiredPort)
                          + " occupied)", LogLevel::ERR);
            return;
        }
        // Ask user whether to use the alternate port
        wxString msg = wxString::Format("端口 %d 已被占用，是否使用端口 %d 开启代理？", desiredPort, freePort);
        wxMessageDialog portDlg(this, msg, "端口占用提示", wxYES_NO | wxICON_QUESTION | wxNO_DEFAULT);
        portDlg.CentreOnScreen();
        if (portDlg.ShowModal() != wxID_YES) {
            Logger::write("[UI] User declined alternate port " + std::to_string(freePort)
                          + " for standalone proxy", LogLevel::INFO);
            return;
        }
        actualPort = freePort;
        Logger::write("[UI] Port " + std::to_string(desiredPort) + " occupied, using alternate port "
                      + std::to_string(freePort) + " for standalone proxy", LogLevel::INFO);
    }

    // Start the standalone proxy with the resolved port
    bool ok = controller_->startStandaloneProxy(indexId, actualPort);
    if (ok) {
        Logger::write("[UI] Standalone proxy started: " + indexId
                      + " on SOCKS5 127.0.0.1:" + std::to_string(actualPort),
                      LogLevel::REPORT);
    } else {
        Logger::write("[UI] Failed to start standalone proxy: " + indexId, LogLevel::ERR);
    }
    (void)event;
}

// -------------------------------------------------------------------
void ProxyListPanel::onStandaloneProxyEvent(StandaloneProxyEvent& event) {
    if (event.isStarted()) {
        Logger::write("[UI] Standalone proxy started: " + event.getIndexId()
                      + " on port " + std::to_string(event.getSocksPort()), LogLevel::REPORT);
    } else {
        Logger::write("[UI] Standalone proxy stopped: " + event.getIndexId(), LogLevel::REPORT);
    }
    event.Skip();
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
    if (!item.IsOk()) {
        lastSelItem_ = wxDataViewItem();
        return;
    }

    unsigned int viewRow = model_->GetRow(item);
    if (viewRow == static_cast<unsigned int>(-1)) return;

    const db::models::Profileitem* proxy = model_->getProfileAtRow(viewRow);
    if (!proxy) return;

    const std::string& indexId = proxy->indexid;

    // Double-click detection: same item selected twice within timeout.
    // Workaround for MSW wxDataViewMainWindow lacking CS_DBLCLKS class style.
    wxLongLong now = wxGetLocalTimeMillis();
    if (lastSelItem_.IsOk() && item.IsOk() &&
        lastSelItem_.GetID() == item.GetID() &&
        now - lastSelTime_ < DBLCLICK_TIMEOUT_MS) {
        wxCommandEvent dummy;
        onStartProxy(dummy);
        // Reset to prevent triple-click from triggering action again
        lastSelItem_ = wxDataViewItem();
        lastSelTime_ = 0;
    } else {
        lastSelItem_ = item;
        lastSelTime_ = now;
    }

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
        for (const db::models::Profileitem& p : allProxies_) {
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

    const db::models::Profileitem* proxy = model_->getProfileAtRow(0);
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
