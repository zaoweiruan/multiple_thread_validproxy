#include "ProxyListPanel.h"
#include "AppController.h"
#include "Events.h"
#include "Logger.h"
#include "Utils.h"
#include "MainFrame.h"

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
    ID_CONTEXT_TEST_PROXY     = wxID_HIGHEST + 400,
    ID_CONTEXT_EXPORT_SHARE   = wxID_HIGHEST + 401,
    ID_CONTEXT_START_PROXY    = wxID_HIGHEST + 402,
    ID_CONTEXT_RESOLVE_REGION = wxID_HIGHEST + 403,
    ID_CONTEXT_BATCH_RESOLVE_REGION = wxID_HIGHEST + 404,
    ID_CONTEXT_REFRESH        = wxID_HIGHEST + 405,
    ID_HISTORY_TIMER          = wxID_HIGHEST + 406,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(ProxyListPanel, wxPanel)
    EVT_DATAVIEW_ITEM_CONTEXT_MENU(wxID_ANY, ProxyListPanel::onContextMenu)
    EVT_MENU(ID_CONTEXT_TEST_PROXY, ProxyListPanel::onTestProxy)
    EVT_MENU(ID_CONTEXT_EXPORT_SHARE, ProxyListPanel::onExportShareLink)
    EVT_MENU(ID_CONTEXT_START_PROXY, ProxyListPanel::onStartProxy)
    EVT_MENU(ID_CONTEXT_RESOLVE_REGION, ProxyListPanel::onResolveRegion)
    EVT_MENU(ID_CONTEXT_BATCH_RESOLVE_REGION, ProxyListPanel::onBatchResolveRegion)
    EVT_MENU(ID_CONTEXT_REFRESH, ProxyListPanel::onRefreshProxyList)
    EVT_DATAVIEW_SELECTION_CHANGED(wxID_ANY, ProxyListPanel::onSelectionChanged)
    EVT_TIMER(ID_HISTORY_TIMER, ProxyListPanel::onHistoryTimer)
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

    // Periodic evaluation refresh: every 3 seconds re-read the live
    // running-session durations from the DB (on a background thread) so the
    // displayed evaluation stays in sync with the heartbeat updates without
    // blocking the UI.
    historyTimer_ = new wxTimer(this, ID_HISTORY_TIMER);
    historyTimer_->Start(3000);

    // Columns: Region | Latency ↕ | Health ↕ | Type | Host ↕ | Port | Message ↕ |
    // Starts ↕ | Runtime(ms) ↕ | # | IndexId | Failures ↕ | Remarks
    listCtrl_->AppendTextColumn("Region",   COL_REGION,   wxDATAVIEW_CELL_INERT,  90);
    listCtrl_->AppendTextColumn("Latency ↕", COL_DELAY,  wxDATAVIEW_CELL_INERT,  80);
    listCtrl_->AppendTextColumn("Health ↕", COL_HEALTH, wxDATAVIEW_CELL_INERT, 70);
    listCtrl_->AppendTextColumn("Type",     COL_TYPE,     wxDATAVIEW_CELL_INERT,  80);
    listCtrl_->AppendTextColumn("Host ↕",   COL_ADDRESS,  wxDATAVIEW_CELL_INERT, 100);
    listCtrl_->AppendTextColumn("Port",     COL_PORT,     wxDATAVIEW_CELL_INERT,  70);
    listCtrl_->AppendTextColumn("Message ↕", COL_MESSAGE,  wxDATAVIEW_CELL_INERT, 160);
    listCtrl_->AppendTextColumn("Starts ↕", COL_START_COUNT, wxDATAVIEW_CELL_INERT, 60);
    listCtrl_->AppendTextColumn("Runtime ↕", COL_TOTAL_RUNTIME_MS, wxDATAVIEW_CELL_INERT, 90);
    listCtrl_->AppendTextColumn("#",        COL_ROWNUM,   wxDATAVIEW_CELL_INERT,  40);
    listCtrl_->AppendTextColumn("IndexId",  COL_INDEXID,  wxDATAVIEW_CELL_INERT, 120);
    listCtrl_->AppendTextColumn("Failures ↕", COL_FAILURES, wxDATAVIEW_CELL_INERT, 80);
    listCtrl_->AppendTextColumn("Remarks",  COL_REMARKS,  wxDATAVIEW_CELL_EDITABLE, 160);

    sizer->Add(listCtrl_, 1, wxEXPAND | wxALL, 2);
    SetSizer(sizer);

    // Bind custom events for completion handling
    Bind(wxEVT_PROXY_TEST_PROGRESS, &ProxyListPanel::onProxyTestProgress, this);
    Bind(wxEVT_STANDALONE_PROXY, &ProxyListPanel::onStandaloneProxyEvent, this);
    Bind(wxEVT_RUNNING_DURATIONS_LOADED, &ProxyListPanel::onRunningDurationsLoaded, this);
    Bind(wxEVT_DATAVIEW_COLUMN_HEADER_CLICK, &ProxyListPanel::onColumnHeaderClick, this);

    // Double-click to start proxy
    listCtrl_->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent&) {
        wxCommandEvent dummy;
        onStartProxy(dummy);
    });
}

ProxyListPanel::~ProxyListPanel() {
    if (historyTimer_) {
        historyTimer_->Stop();
        delete historyTimer_;
        historyTimer_ = nullptr;
    }
}

// -------------------------------------------------------------------
// UI update helper — sets member data, resets model, selects first row.
// Called by both loadProxies overloads.
// -------------------------------------------------------------------
void ProxyListPanel::updateProxyList(const std::vector<db::models::Profileitem>& proxies,
                                      const std::vector<db::models::ProfileExItem>& exItems,
                                      const std::string& subId) {
    currentSubId_ = subId;
    allProxies_ = proxies;
    cacheReady_ = true;

    sortState_.column = -1;
    sortState_.direction = SortDirection::None;
    proxies_ = proxies;
    exItems_ = exItems;

    model_->setData(&proxies_, &exItems_);
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
    model_->detectIdOffset();

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
// Accept pre-fetched data and pre-built maps (no DB read, no O(N) map
// rebuild on the UI thread).  The async reader posts the FULL unfiltered
// profile table; it is kept as the panel cache and the visible subset is
// derived through applySubscriptionFilter() — the same in-memory path
// used for instant subscription switching.
// -------------------------------------------------------------------
void ProxyListPanel::loadProxies(std::vector<db::models::Profileitem> proxies,
                                  std::vector<db::models::ProfileExItem> exItems,
                                  utils::ProxyListMaps maps,
                                  const std::string& subId) {
    currentSubId_ = subId;
    allProxies_ = std::move(proxies);
    exItems_ = std::move(exItems);
    model_->setMaps(std::move(maps));
    cacheReady_ = true;

    applySubscriptionFilter(subId);
}

// -------------------------------------------------------------------
// Refresh only the Delay/Message/Failures columns by reloading exItems_ from DB.
// Proxies list and user selection are preserved.
// Model's lookup maps are rebuilt and the view is notified to redraw.
// -------------------------------------------------------------------
void ProxyListPanel::refreshResults() {
    Logger::write("[ProxyListPanel] refreshResults called", LogLevel::REPORT);

    exItems_ = controller_->loadProxyResults();

    model_->rebuildMaps();

    // Merge live elapsed time of in-progress standalone sessions so the
    // Runtime column shows total_runtime_ms + current session duration and
    // the Health score gains a running-time bonus.
    model_->setRunningDurations(controller_->getRunningDurations());

    // Notify the view that the history columns changed so the DataViewCtrl
    // re-queries the model for Starts/Runtime/Health cells (Refresh() alone
    // only repaints, it does not invalidate cached cell values on MSW).
    model_->notifyHistoryChanged();

    listCtrl_->Refresh();
}

// -------------------------------------------------------------------
// Reload the full proxy rows (ProfileItem incl. Region column) for the
// current subscription filter from the database.  refreshResults() only
// re-reads ProfileExItem test results, so region values written by the
// background resolver would never appear without this full reload.
// Runs asynchronously (background reader + prebuilt maps) to avoid
// blocking the UI thread on 50k+ row databases.
// -------------------------------------------------------------------
void ProxyListPanel::reloadFromDatabase() {
    if (controller_) {
        controller_->loadProxiesAsync(currentSubId_, this);
    }
}

// -------------------------------------------------------------------
// Instant subscription switch: filter the cached full list in memory
// (same pattern as filterBySearch) instead of re-reading the entire
// database on every click.  The model lookup maps are keyed by indexId
// and unaffected by the subid filter, so no rebuildMaps() is needed —
// only the non-owning pointers must be re-pointed at the reassigned
// vectors before resetting the view.
// -------------------------------------------------------------------
void ProxyListPanel::applySubscriptionFilter(const std::string& subId) {
    if (!cacheReady_) {
        // Cache not populated yet (startup) — fall back to async DB reload.
        reloadFromDatabase();
        return;
    }

    currentSubId_ = subId;
    if (subId.empty()) {
        proxies_ = allProxies_;  // Restore unfiltered list
    } else {
        proxies_.clear();
        for (const db::models::Profileitem& p : allProxies_) {
            if (p.subid == subId) {
                proxies_.push_back(p);
            }
        }
    }

    sortState_.column = -1;
    sortState_.direction = SortDirection::None;

    model_->setDataWithoutRebuild(&proxies_, &exItems_);
    // Double-Reset workaround (see filterBySearch): Reset(N) alone may keep
    // stale m_list entries in some wxWidgets versions.
    model_->Reset(0);
    model_->Reset(static_cast<unsigned int>(proxies_.size()));
    model_->detectIdOffset();

    if (!proxies_.empty()) {
        if (!listCtrl_->GetSelection().IsOk()) {
            selectFirstProxy();
        }
    }
}

// -------------------------------------------------------------------
void ProxyListPanel::onHistoryTimer(wxTimerEvent& event) {
    refreshHistoryPeriodic();
}

// -------------------------------------------------------------------
// Periodic evaluation refresh.  Kicks off a background read of live
// running-session durations; the actual DB query runs off the UI thread
// (see AppController::getRunningDurationsAsync).  No full-table re-read
// of exItems and no map rebuild here — only in-flight sessions change on
// a heartbeat cadence, so an incremental merge is sufficient.  The
// refreshInFlight_ guard prevents stacking multiple background reads if
// the timer fires while the previous query is still running.
void ProxyListPanel::refreshHistoryPeriodic() {
    if (!model_ || !controller_) {
        return;
    }
    if (refreshInFlight_.exchange(true)) {
        // Previous background read still in flight — skip this tick.
        return;
    }
    controller_->getRunningDurationsAsync(this);
}

// -------------------------------------------------------------------
// RunningDurationsLoadedEvent handler (UI thread).  Merges the live
// durations of in-progress sessions into the model and re-queries the
// view for the evaluation columns.  Only rows with standalone history
// are notified, so the cost is small even with a large profile database.
// When the snapshot is unchanged (e.g. no in-progress session at all) the
// redraw is skipped entirely — otherwise an idle UI would repaint every
// 3s even though no evaluation data changed.
void ProxyListPanel::onRunningDurationsLoaded(RunningDurationsLoadedEvent& event) {
    if (!model_ || !controller_) {
        refreshInFlight_ = false;
        return;
    }
    if (model_->setRunningDurations(event.takeDurations())) {
        // Only the in-progress (running) rows actually changed, so notify
        // and repaint only those.  Idle proxies keep their historical values
        // and stay completely still during the periodic 3s poll.
        model_->notifyRunningChanged();
        listCtrl_->Refresh();
    }
    refreshInFlight_ = false;
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
wxDataViewColumn* ProxyListPanel::resolveColumnByModel(int modelCol) const {
    if (modelCol < 0) {
        return nullptr;
    }
    const unsigned int count = listCtrl_->GetColumnCount();
    for (unsigned int i = 0; i < count; ++i) {
        wxDataViewColumn* col = listCtrl_->GetColumn(i);
        if (col != nullptr && static_cast<int>(col->GetModelColumn()) == modelCol) {
            return col;
        }
    }
    return nullptr;
}

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
        wxDataViewColumn* dvCol = resolveColumnByModel(col);
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
    menu.AppendSeparator();
    menu.Append(ID_CONTEXT_RESOLVE_REGION, "解析地区");
    menu.Append(ID_CONTEXT_BATCH_RESOLVE_REGION, "批量解析地区");
    menu.AppendSeparator();
    menu.Append(ID_CONTEXT_REFRESH, "刷新");
    menu.AppendSeparator();
    menu.Append(ID_CONTEXT_START_PROXY, "开启代理");
    PopupMenu(&menu);
    event.Skip();
}

// -------------------------------------------------------------------
void ProxyListPanel::onTestProxy(wxCommandEvent& event) {
    // Sync toolbar Cancel button state from controller before re-entry check
    {
        wxWindow* topLevel = wxGetTopLevelParent(this);
        if (topLevel && topLevel != this) {
            static_cast<MainFrame*>(topLevel)->syncToolbarState();
        }
    }
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
void ProxyListPanel::onRefreshProxyList(wxCommandEvent& event) {
    // Refresh proxy list from database, preserving current subscription filter
    loadProxies(currentSubId_);
    (void)event;
}

// -------------------------------------------------------------------
void ProxyListPanel::onExportShareLink(wxCommandEvent& event) {
    // Sync toolbar Cancel button state from controller before re-entry check
    {
        wxWindow* topLevel = wxGetTopLevelParent(this);
        if (topLevel && topLevel != this) {
            static_cast<MainFrame*>(topLevel)->syncToolbarState();
        }
    }
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
void ProxyListPanel::onResolveRegion(wxCommandEvent& event) {
    // Sync toolbar Cancel button state from controller before re-entry check
    {
        wxWindow* topLevel = wxGetTopLevelParent(this);
        if (topLevel && topLevel != this) {
            static_cast<MainFrame*>(topLevel)->syncToolbarState();
        }
    }

    wxDataViewItem item = listCtrl_->GetSelection();
    if (!item.IsOk()) return;

    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }

    unsigned int viewRow = model_->GetRow(item);
    if (viewRow == static_cast<unsigned int>(-1)) return;

    std::string indexId = model_->getIndexIdAtRow(viewRow);
    if (indexId.empty()) return;

    // Use batch resolver async method (online via api.ipinfo.io/lite)
    controller_->resolveSingleProxyRegionAsync(indexId, this);
    (void)event;
}

// -------------------------------------------------------------------
void ProxyListPanel::onBatchResolveRegion(wxCommandEvent& event) {
    if (!controller_) return;

    // Sync toolbar Cancel button state from controller before re-entry check
    {
        wxWindow* topLevel = wxGetTopLevelParent(this);
        if (topLevel && topLevel != this) {
            static_cast<MainFrame*>(topLevel)->syncToolbarState();
        }
    }

    if (controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }

    controller_->resolveRegionsBatchAsync(this, currentSubId_);
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

    // Reject a duplicate start before any port check: if a standalone proxy
    // using the same derived config file is already running, there is no point
    // in resolving ports for it (it would be refused right after anyway).
    if (controller_->isStandaloneProxyRunning(indexId)) {
        Logger::write("[UI] Standalone proxy for " + indexId
                      + " is already running, skipped duplicate start", LogLevel::WARN);
        wxMessageDialog dlg(this, "该代理已作为独立进程运行，请先停止后再启动。",
                            "代理已运行", wxOK | wxICON_INFORMATION);
        dlg.CentreOnScreen();
        dlg.ShowModal();
        return;
    }

    // Reject proxies whose last test result is invalid (delay <= 0 or
    // untested).  The user must run a connectivity test first so the proxy
    // has a meaningful latency before being promoted to standalone mode.
    {
        std::string reason = model_->getProxyValidityReason(indexId);
        if (!reason.empty()) {
            Logger::write("[UI] Refusing standalone start for " + indexId
                          + ": reason=" + reason, LogLevel::WARN);
            wxString userMsg = reason == "untested"
                ? "该代理尚未测速，请先进行连通性测速后再启动。"
                : "该代理测速失败，请先进行连通性测速后再启动。";
            wxMessageDialog dlg(this, userMsg, "需要测速", wxOK | wxICON_WARNING);
            dlg.CentreOnScreen();
            dlg.ShowModal();
            return;
        }
    }

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
        // Refresh history/health columns so runtimes and health reflect the
        // new in-progress session (insertStart already updated the DB).
        refreshResults();
    } else {
        Logger::write("[UI] Standalone proxy stopped: " + event.getIndexId(), LogLevel::REPORT);
        refreshResults();
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
                p.region.find(q) != std::string::npos ||
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
        wxDataViewColumn* dvCol = resolveColumnByModel(sortState_.column);
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
bool ProxyListPanel::HasSelection() const {
    return listCtrl_->GetSelection().IsOk();
}

// -------------------------------------------------------------------
// RefreshContextMenu — re-evaluate context menu state.
// Currently a no-op because the context menu is created on-demand
// in onContextMenu(). Retained as an extension point for when
// persistent menu state is added.
// -------------------------------------------------------------------
void ProxyListPanel::RefreshContextMenu() {
    // Context menu is created dynamically; no persistent state to refresh.
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
