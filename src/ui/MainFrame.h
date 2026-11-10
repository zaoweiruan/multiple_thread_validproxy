#ifndef UI_MAIN_FRAME_H
#define UI_MAIN_FRAME_H

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/splitter.h>
#include <wx/statusbr.h>

#include <atomic>
#include <string>

#include "ConfigReader.h"
#include "Events.h"
#include "RefreshCommands.h"

struct sqlite3;

class AppController;
class SubscriptionPanel;
class ProxyListPanel;
class ProxyDetailPanel;
class LogPanel;
class wxSearchCtrl;
class wxChoice;
class ConfigDialog;
class TrayIcon;
class StandaloneFloatingWidget;
class TaskMonitorPanel;
class BatchQueuePanel;

enum class OperationType {
    NONE,
    TEST,
    UPDATE,
    FIND,
    SYNC,
    AUTOTASK,
    RESOLVE_REGION
};

// ---------------------------------------------------------------
// MainFrame — primary application window
// ---------------------------------------------------------------
class MainFrame : public wxFrame {
public:
    MainFrame(const config::AppConfig& cfg, sqlite3* db);
    ~MainFrame() override;

    // Panel access
    SubscriptionPanel* getSubscriptionPanel() const { return subPanel_; }
    ProxyListPanel* getProxyListPanel() const { return proxyPanel_; }
    LogPanel* getLogPanel() const { return logPanel_; }
    AppController* getController() const { return controller_; }
    std::string getDbPath() const;
    // Status bar helpers
    void setStatusText(int field, const wxString& text);
    // Protected field-0 notice (spec 2026-11-06 §2.4): reuses the existing
    // one-shot statusTextProtected_ guard so the transient loader writes
    // ("Loaded subscription…"/"Refresh: loaded all proxies") that fire right
    // after an enqueue do not swallow the "已加入队列…" feedback.
    void setStatusNotice(const wxString& text);
    void showBalloon(const wxString& title, const wxString& msg);
    void setOperationState(OperationType op);
    void syncToolbarState();
    void UpdateButtonStates();
    void UpdateMenuStates();
    void setLogFileLabel(const std::string& filePath);

private:
    // Initialization
    void initMenuBar();
    void initToolBar();
    void initStatusBar();
    void initAuiManager();
    void initPanels();
    void initTrayIcon();
    void loadSettings();

    // Event handlers
    void onClose(wxCloseEvent& event);
    void onIconize(wxIconizeEvent& event);
    void onMenuImportSub(wxCommandEvent& event);
    void onMenuSyncDb(wxCommandEvent& event);
    void onMenuExit(wxCommandEvent& event);
    void onMenuUpdateAll(wxCommandEvent& event);
    void onMenuFindProxy(wxCommandEvent& event);
    void onMenuFindBest(wxCommandEvent& event);
    void onMenuDedup(wxCommandEvent& event);
    void onMenuExportShareLink(wxCommandEvent& event);
    void onMenuGenerateConfig(wxCommandEvent& event);
    void onMenuConfig(wxCommandEvent& event);
    void onMenuAutoTask(wxCommandEvent& event);
    void onMenuAutoTaskResume(wxCommandEvent& event);
    void onMenuAbout(wxCommandEvent& event);
    void onMenuStandaloneMonitor(wxCommandEvent& event);
    void syncFloatingWidgetControls();
    void onToolUpdateAll(wxCommandEvent& event);
    void onToolTest(wxCommandEvent& event);
    void onToolFind(wxCommandEvent& event);
    void onToolDedup(wxCommandEvent& event);
    void onToolImport(wxCommandEvent& event);
    void onToolConfig(wxCommandEvent& event);
    void onToolCancel(wxCommandEvent& event);
    void onToolSync(wxCommandEvent& event);
    void onStatusUpdate(StatusUpdateEvent& event);
    void rewriteRefreshStatusText();
    void onOnlineProbeFinished(OnlineProbeFinishedEvent& event);
    void onResize(wxSizeEvent& event);
    void onSearchBoxEnter(wxCommandEvent& event);
    void onSearchTextChanged(wxCommandEvent& event);
    void onSearchClear(wxCommandEvent& event);
    void onToggleDetailPane(wxCommandEvent& event);
    void onTestSubscription(SubscriptionTestEvent& event);
    void onNetMonTimer(wxTimerEvent& event);
    void onFirstShow(wxShowEvent& event);
    void repositionNetMonPanel();
    void onProxyMonTimer(wxTimerEvent& event);
    void onBottomViewSelected(wxCommandEvent& event);
    void onBottomViewTimer(wxTimerEvent& event);
    void switchBottomView(int index);
    void repositionProxyMonPanel();
    void updateProxyMonStatus(bool enabled, int aliveCount);
    void startProxyMonitor(int intervalMs);
    void stopProxyMonitor();
    void startMonitoring();

    // Members
     wxAuiManager* auiManager_{nullptr};
     wxSplitterWindow* splitter_{nullptr};  // Resizable splitter for subscription/proxy panels
    AppController* controller_;
    wxMenuBar* menuBar_{nullptr};
    wxMenu* proxyMenu_{nullptr};
    SubscriptionPanel* subPanel_{nullptr};
    ProxyListPanel* proxyPanel_{nullptr};
    ProxyDetailPanel* detailPanel_{nullptr};
    LogPanel* logPanel_{nullptr};
    ConfigDialog* configDialog_{nullptr};
    StandaloneFloatingWidget* floatingWidget_{nullptr};  // Lazy, toggled via Ctrl+M / toolbar / menu
    wxAuiToolBar* m_toolbar{nullptr};  // Toolbar pointer for AUI management
    TrayIcon* trayIcon_{nullptr};
    sqlite3* db_;
    wxStatusBar* statusBar_{nullptr};
    wxSearchCtrl* m_searchBox{nullptr};
    wxChoice* m_searchTargetChoice{nullptr};
    wxAuiToolBarItem* m_toggleDetailItem{nullptr};  // Toggle detail panel button
    bool detailPaneVisible_{false};
    bool monitoringStarted_{false};
    bool initialSubsLoaded_{false};  // Guard: auto-load proxies on first async subscription load
    config::AppConfig config_;
    wxTimer* netMonTimer_{nullptr};
    wxPanel* netMonPanel_{nullptr};
    bool netMonConnected_{true};

    wxTimer* proxyMonTimer_{nullptr};
    wxPanel* proxyMonPanel_{nullptr};
    bool proxyMonEnabled_{false};
    int proxyAliveCount_{0};

    // Bottom shared view (spec 2026-11-03 v1.2 §3.8, v1.2.5 §13): ONE window
    // area, the visible page chosen by the 监控 radio; hidden pages take no
    // sizer space (wxBoxSizer CalcMin/DoLayout skip hidden windows).
    wxPanel* bottomPanel_{nullptr};
    wxRadioBox* bottomViewRadio_{nullptr};
    // Queue-count badge next to the 监控 radio (spec 2026-11-06 §2.4).
    // Separate wxStaticText on purpose: wxRadioBox::SetString rebuilds the
    // whole control on MSW (destroys the UIA element every poll). Hidden at
    // depth 0 so it never enters the UIA tree of the idle UI.
    wxStaticText* queueBadge_{nullptr};
    long badgeQueueDepth_{-1};   // last published depth (skip no-op SetLabel)
    void updateQueueBadge();
    TaskMonitorPanel* taskMonitorPanel_{nullptr};
    BatchQueuePanel* batchQueuePanel_{nullptr};
    wxTimer* bottomViewTimer_{nullptr};
    int bottomViewIndex_{0};   // 0=任务 1=任务进度 2=日志 (default 任务, owner 2026-11-05)

    // Refresh command status-bar protection (see Step 2b)
    std::atomic<bool> statusTextProtected_{false};
    std::string statusTextPending_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_MAIN_FRAME_H
