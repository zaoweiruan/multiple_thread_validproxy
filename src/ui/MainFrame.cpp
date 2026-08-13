#include "MainFrame.h"
#include "ConfigDialog.h"
#include "LogPanel.h"
#include "ProxyDetailPanel.h"
#include "ProxyListPanel.h"
#include "SubscriptionPanel.h"
#include "TrayIcon.h"
#include "UIApp.h"
#include "AppController.h"
#include "Events.h"
#include "Logger.h"
#include "Profileitem.h"
#include "ToolbarIcons.h"
#include "NetworkMonitor.h"
#include "version.h"

#include <wx/sizer.h>
#include <wx/splitter.h>
#include <wx/aui/auibook.h>
#include <wx/aui/auibar.h>
#include <wx/treectrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/artprov.h>
#include <wx/stdpaths.h>
#include <wx/srchctrl.h>
#include <wx/file.h>
#include <shellapi.h>
#include <thread>
#include <fstream>

// -------------------------------------------------------------------
//  Menu / Tool identifiers
// -------------------------------------------------------------------
enum {
    ID_MENU_IMPORT_SUB    = wxID_HIGHEST + 100,
    ID_MENU_SYNC_DB       = wxID_HIGHEST + 101,
    ID_MENU_EXIT          = wxID_HIGHEST + 102,
    ID_MENU_UPDATE_ALL    = wxID_HIGHEST + 103,

    ID_MENU_FIND_PROXY    = wxID_HIGHEST + 105,
    ID_MENU_FIND_BEST     = wxID_HIGHEST + 106,
    ID_MENU_DEDUP         = wxID_HIGHEST + 107,
    ID_MENU_EXPORT        = wxID_HIGHEST + 108,
    ID_MENU_GEN_CONFIG    = wxID_HIGHEST + 109,
    ID_MENU_CONFIG        = wxID_HIGHEST + 110,
    ID_MENU_ABOUT         = wxID_HIGHEST + 111,
    ID_MENU_AUTOTASK_RUN  = wxID_HIGHEST + 112,
    ID_MENU_AUTOTASK_RESUME = wxID_HIGHEST + 113,
    ID_TOOL_UPDATE_ALL    = wxID_HIGHEST + 200,
    ID_TOOL_TEST          = wxID_HIGHEST + 201,
    ID_TOOL_FIND          = wxID_HIGHEST + 202,
    ID_TOOL_DEDUP         = wxID_HIGHEST + 203,
    ID_TOOL_IMPORT        = wxID_HIGHEST + 204,
    ID_TOOL_CONFIG        = wxID_HIGHEST + 205,
    ID_TOOL_CANCEL        = wxID_HIGHEST + 208,
    ID_TOOL_SYNC          = wxID_HIGHEST + 209,
    ID_TOOL_AUTOTASK      = wxID_HIGHEST + 210,
    ID_TOOL_CLEAR         = wxID_HIGHEST + 207,
    ID_SEARCH_BOX         = wxID_HIGHEST + 206,
    ID_SEARCH_TARGET      = wxID_HIGHEST + 300,
    ID_TOOL_DETAIL_TOGGLE = wxID_HIGHEST + 302,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::onClose)
    EVT_ICONIZE(MainFrame::onIconize)
    EVT_SIZE(MainFrame::onResize)
    // Menu
    EVT_MENU(ID_MENU_IMPORT_SUB,  MainFrame::onMenuImportSub)
    EVT_MENU(ID_MENU_SYNC_DB,     MainFrame::onMenuSyncDb)
    EVT_MENU(ID_MENU_EXIT,        MainFrame::onMenuExit)
    EVT_MENU(ID_MENU_UPDATE_ALL,  MainFrame::onMenuUpdateAll)
    EVT_MENU(ID_MENU_FIND_PROXY,  MainFrame::onMenuFindProxy)
    EVT_MENU(ID_MENU_FIND_BEST,   MainFrame::onMenuFindBest)
    EVT_MENU(ID_MENU_DEDUP,       MainFrame::onMenuDedup)
    EVT_MENU(ID_MENU_EXPORT,      MainFrame::onMenuExportShareLink)
    EVT_MENU(ID_MENU_GEN_CONFIG,  MainFrame::onMenuGenerateConfig)
    EVT_MENU(ID_MENU_CONFIG,      MainFrame::onMenuConfig)
    EVT_MENU(ID_MENU_AUTOTASK_RUN, MainFrame::onMenuAutoTask)
    EVT_MENU(ID_MENU_AUTOTASK_RESUME, MainFrame::onMenuAutoTaskResume)
    EVT_MENU(ID_MENU_ABOUT,       MainFrame::onMenuAbout)
    // Toolbar
    EVT_MENU(ID_TOOL_UPDATE_ALL,  MainFrame::onToolUpdateAll)
    EVT_MENU(ID_TOOL_TEST,        MainFrame::onToolTest)
    EVT_MENU(ID_TOOL_FIND,        MainFrame::onToolFind)
    EVT_MENU(ID_TOOL_DEDUP,       MainFrame::onToolDedup)
    EVT_MENU(ID_TOOL_IMPORT,      MainFrame::onToolImport)
    EVT_MENU(ID_TOOL_CONFIG,      MainFrame::onToolConfig)
    EVT_MENU(ID_TOOL_CANCEL, MainFrame::onToolCancel)
    EVT_MENU(ID_TOOL_SYNC,       MainFrame::onToolSync)
    EVT_MENU(ID_TOOL_AUTOTASK,   MainFrame::onMenuAutoTask)
    // Search
    EVT_TEXT_ENTER(ID_SEARCH_BOX,  MainFrame::onSearchBoxEnter)
    EVT_TEXT(ID_SEARCH_BOX,        MainFrame::onSearchTextChanged)
    EVT_SEARCH_CANCEL(ID_SEARCH_BOX, MainFrame::onSearchClear)
    EVT_MENU(ID_TOOL_DETAIL_TOGGLE, MainFrame::onToggleDetailPane)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
//  Construction / Destruction
// -------------------------------------------------------------------
MainFrame::MainFrame(const config::AppConfig& cfg, sqlite3* db)
    : wxFrame(nullptr, wxID_ANY, "validproxy - Proxy Manager",
              wxDefaultPosition, wxSize(1200, 800)),
      db_(db),
      config_(cfg)
{
    controller_ = new AppController(db, cfg);
    Logger::write("[MainFrame] Constructor begin", LogLevel::DEBUG);

    Logger::write("[MainFrame] After controller creation, initializing icon", LogLevel::DEBUG);

    // Set application icon from embedded resource (icon.ico → resource "icon_ico")
    #ifdef __WXMSW__
    wxIcon appIcon("icon_ico", wxBITMAP_TYPE_ICO_RESOURCE);
    #else
    wxIcon appIcon;
    #endif
    if (appIcon.IsOk()) {
        SetIcon(appIcon);
    } else {
        // Fallback to wxArtProvider themed icon
        wxBitmap appBitmap = wxArtProvider::GetBitmap(wxART_FRAME_ICON);
        if (appBitmap.IsOk()) {
            wxIcon fallbackIcon;
            fallbackIcon.CopyFromBitmap(appBitmap);
            SetIcon(fallbackIcon);
        }
    }

    // Prevent too-narrow window that breaks toolbar right-side control layout
    SetMinSize(wxSize(900, 600));

    // ── Find-proxy completion ──────────────────────────────────────
    // Payload: "FOUND:<indexId>:<address>" | "NOTFOUND" | "ERR:..."
    Bind(wxEVT_STATUS_UPDATE, [this](StatusUpdateEvent& evt) {
        wxString payload = evt.getText();
        if (payload.StartsWith("FOUND:")) {
            if (proxyPanel_) {
                wxString rest = payload.Mid(6);           // strip "FOUND:"
                wxString indexId = rest.BeforeFirst(':');  // 1st field
                proxyPanel_->selectProxyByIndexId(indexId.ToStdString());
            }
            wxString msg = wxString("Found: ") + payload.Mid(6);
            wxMessageBox(msg, "Proxy Found", wxOK | wxICON_INFORMATION);
        } else if (payload == "NOTFOUND") {
            wxMessageBox("No working proxy found.",
                         "Result", wxOK | wxICON_INFORMATION);
        } else if (payload.StartsWith("ERR:")) {
            wxMessageBox("Find error: " + payload.Mid(4),
                         "Error", wxOK | wxICON_ERROR);
        } else if (payload.StartsWith("REJECT:")) {
            wxMessageBox(payload.Mid(7), "Operation Busy",
                         wxOK | wxICON_INFORMATION, this);
        } else if (payload.StartsWith("RESOLVE_REGION_START")) {
            setOperationState(OperationType::RESOLVE_REGION);
        } else {
            // Auto-refresh the subscription panel when an update completes,
            // so the user sees updated proxy counts without manual "刷新".
            if (payload.StartsWith("Update completed:") ||
                payload.StartsWith("All subscriptions updated") ||
                payload.StartsWith("Update (all)")) {
                if (subPanel_ && controller_) {
                    controller_->loadSubscriptionsAsync(this);
                }
            }
            // AutoTask completion → restore UI state and refresh panels
            if (payload.StartsWith("AutoTask completed") ||
                payload.StartsWith("AutoTask failed") ||
                payload.StartsWith("AutoTask cancelled") ||
                payload == "AutoTask completed" ||
                payload == "AutoTask failed" ||
                payload == "AutoTask cancelled") {
                setOperationState(OperationType::NONE);
                if (subPanel_ && controller_) {
                    controller_->loadSubscriptionsAsync(this);
                }
                if (proxyPanel_) {
                    proxyPanel_->refreshResults();
                }
            }
            onStatusUpdate(evt);
        }
    });

    // ── Test completion → refresh Delay column and restore UI state ──
    Bind(wxEVT_PROXY_TEST_PROGRESS, [this](ProxyTestProgressEvent& evt) {
        if (evt.isCompleted()) {
            if (proxyPanel_) {
                proxyPanel_->refreshResults();
            }
            if (subPanel_) {
                subPanel_->loadSubscriptions();
            }
            setOperationState(OperationType::NONE);
            setStatusText(0, "Test completed");
        } else {
            // Show per-proxy progress in status bar (e.g. region resolution)
            setStatusText(0, evt.getMessage());
        }
    });

    // ── Subscription right-click Test ────────────────────────────
    Bind(wxEVT_SUBSCRIPTION_TEST, &MainFrame::onTestSubscription, this);

    Logger::write("[MainFrame] initMenuBar...", LogLevel::DEBUG);
    initMenuBar();
    Logger::write("[MainFrame] initToolBar...", LogLevel::DEBUG);
    initToolBar();
    Logger::write("[MainFrame] initStatusBar...", LogLevel::DEBUG);
    initStatusBar();
    // Disable the menu/toolbar help-text mechanism (wxFrameBase::DoGiveHelp).
    // Opening a menu with empty help strings would otherwise clear status bar
    // field 0 ("Ready"); on wxMSW, clicking to close the menu does not reliably
    // restore it, leaving the field permanently blank until the app writes again.
    SetStatusBarPane(-1);
    Logger::write("[MainFrame] initAuiManager...", LogLevel::DEBUG);
    initAuiManager();
    Logger::write("[MainFrame] initPanels...", LogLevel::DEBUG);
    initPanels();

    // ── SetMenuBar AFTER AUI layout ────────────────────────────────
    // Direct SetMenuBar at initMenuBar time hangs on wxMSW 3.2.5/MinGW.
    // Calling it here (after AUI panes are registered and Update() called)
    // avoids the hang and ensures AUI computes sizes against the correct
    // client area (menu bar changes client geometry).
    Logger::write("[MainFrame] calling delayed SetMenuBar", LogLevel::DEBUG);
    if (menuBar_) {
        SetMenuBar(menuBar_);
        Logger::write("[MainFrame] SetMenuBar done, updating AUI", LogLevel::DEBUG);
        auiManager_->Update();
        Logger::write("[MainFrame] AUI re-layout after SetMenuBar done", LogLevel::DEBUG);
    }
    // Force a WM_SIZE on the status bar so its part widths are recomputed.
    // With the default equal-width path the parts can be laid out at width 0
    // until a real size event arrives, which leaves SetStatusText invisible.
    if (statusBar_) {
        statusBar_->SendSizeEvent();
    }
     
// Bind subscription selection to filter proxy list
      Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
          std::string subId = evt.getSubId();
          if (proxyPanel_ && controller_) {
              controller_->loadProxiesAsync(subId, this);
          }
          setStatusText(0, "Loading subscription: " + wxString(subId));
      });

// ── Async proxy list loaded ───────────────────────────────────
Bind(wxEVT_PROXY_LIST_LOADED, [this](ProxyListLoadedEvent& evt) {
    if (proxyPanel_) {
        proxyPanel_->loadProxies(evt.takeProxies(), evt.takeExItems(), evt.getSubId());
    }
    setStatusText(0, "Loaded subscription: " + wxString(evt.getSubId()));
});

// ── Async subscription list loaded ────────────────────────────
Bind(wxEVT_SUB_LIST_LOADED, [this](SubListLoadedEvent& evt) {
    if (subPanel_) {
        subPanel_->loadSubscriptions(evt.takeSubs(), evt.takeProxyCounts());
    }
});
      
// Bind proxy selection to update detail panel and button/menu states
       Bind(wxEVT_PROXY_SELECTION, [this](ProxySelectionEvent& evt) {
           if (detailPanel_ && controller_) {
               // Get full proxy data from controller for advanced fields
               std::optional<db::models::Profileitem> proxyOpt = controller_->getProxyByIndexId(evt.getIndexId());
               const db::models::Profileitem* proxy = proxyOpt.has_value() ? &*proxyOpt : nullptr;
               
               detailPanel_->UpdateDetail(
                   evt.getIndexId(), evt.getHost(), evt.getPort(), evt.getDelay(),
                   evt.getMessage(), evt.getFailures(), evt.getRemarks(), proxy);
           }
           // Update toolbar and menu states based on new selection
           UpdateButtonStates();
           UpdateMenuStates();
       });
     
    initTrayIcon();

    // Track detail pane close event for visibility state
    auiManager_->Bind(wxEVT_AUI_PANE_CLOSE, [this](wxAuiManagerEvent& evt) {
        wxAuiPaneInfo* pane = evt.GetPane();
        if (pane && pane->name == "detailPane") {
            detailPaneVisible_ = false;
        }
        evt.Skip();
    });

    loadSettings();

    // Network status timer (2s poll)
    netMonTimer_ = new wxTimer(this);
    Bind(wxEVT_TIMER, &MainFrame::onNetMonTimer, this);
    netMonTimer_->Start(2000);

    // Create network status indicator panel on status bar field 1
    netMonPanel_ = new wxPanel(statusBar_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    netMonPanel_->SetBackgroundStyle(wxBG_STYLE_PAINT);
    netMonPanel_->Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
        wxPaintDC dc(netMonPanel_);
        wxSize sz = netMonPanel_->GetClientSize();
        if (sz.x < 4 || sz.y < 4) return;
        wxColour face = wxSystemSettings::GetColour(wxSYS_COLOUR_MENUBAR);
        dc.SetBrush(wxBrush(face));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(0, 0, sz.x, sz.y);
        wxColour shadow = wxSystemSettings::GetColour(wxSYS_COLOUR_3DSHADOW);
        wxColour highlight = wxSystemSettings::GetColour(wxSYS_COLOUR_3DHIGHLIGHT);
        dc.SetPen(wxPen(shadow));
        dc.DrawLine(0, 0, sz.x - 1, 0);
        dc.DrawLine(0, 0, 0, sz.y - 1);
        dc.SetPen(wxPen(highlight));
        dc.DrawLine(0, sz.y - 1, sz.x - 1, sz.y - 1);
        dc.DrawLine(sz.x - 1, 0, sz.x - 1, sz.y - 1);
        wxString label = netMonConnected_ ? L"Network OK" : L"Disconnected";
        dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        wxSize textExt = dc.GetTextExtent(label);
        int textH = textExt.y;
        int r = (textH - 2) / 2;
        if (r < 2) r = 2;
        int cx = r + 3;
        int cy = sz.y / 2;
        wxColour dotColor = netMonConnected_ ? wxColour(0, 180, 0) : wxColour(200, 0, 0);
        dc.SetBrush(wxBrush(dotColor));
        dc.SetPen(wxPen(dotColor));
        dc.DrawCircle(cx, cy, r);
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNTEXT));
        dc.DrawText(label, cx + r + 4, cy - textH / 2);
    });
    // Hide panel if network monitor is disabled
    if (controller_ && !controller_->getNetworkMonitor()->IsEnabled()) {
        netMonPanel_->Show(false);
    }
    repositionNetMonPanel();


    statusBar_->Bind(wxEVT_SIZE, [this](wxSizeEvent& evt) {
        evt.Skip();
        repositionNetMonPanel();
    });

    Logger::write("[MainFrame] Constructor end", LogLevel::DEBUG);
}

MainFrame::~MainFrame() {
    // Step 0: Stop the network monitor timer
    if (netMonTimer_) {
        netMonTimer_->Stop();
        delete netMonTimer_;
        netMonTimer_ = nullptr;
    }

     // Step 1: AUI must be torn down before any panel/frame member is destroyed
     // (AUI holds references to managed panes — pointers must be valid here)
     if (auiManager_) {
         auiManager_->UnInit();
         delete auiManager_;
         auiManager_ = nullptr;
     }

    // Step 2: Controller — worker thread join + XrayManager release
    if (controller_) {
        // cancelTest() is also called in onClose() but is idempotent
        controller_->cancelTest();
        delete controller_;
        controller_ = nullptr;
    }

    // Step 3: TrayIcon — already deleted in onClose() via RemoveIcon() + delete,
    //            so here we only null the dangling pointer to prevent double-free
    if (trayIcon_) {
        // onClose() has already removed it from the shell and freed it
        // (left over if onClose path was never called, e.g. programmatic delete)
        delete trayIcon_;
        trayIcon_ = nullptr;
    }

    if (configDialog_) {
        delete configDialog_;
        configDialog_ = nullptr;
    }
}

// -------------------------------------------------------------------
//  Helpers
// -------------------------------------------------------------------
void MainFrame::setStatusText(int field, const wxString& text) {
    if (statusBar_) statusBar_->SetStatusText(text, field);
}

void MainFrame::setLogFileLabel(const std::string& filePath) {
    if (filePath.empty()) {
        setStatusText(1, "");
        return;
    }
    std::string basename = filePath;
    size_t pos = basename.find_last_of("/\\");
    if (pos != std::string::npos) {
        basename = basename.substr(pos + 1);
    }
    setStatusText(1, wxString(basename));
}

void MainFrame::showBalloon(const wxString& title, const wxString& msg) {
    // TODO: integrate with TrayIcon::showBalloon when exposed
    wxMessageBox(msg, title, wxOK | wxICON_INFORMATION, this);
}

std::string MainFrame::getDbPath() const {
    // Return the actual database path from config
    return config_.database_path;
}

void MainFrame::setOperationState(OperationType op) {
    if (!m_toolbar) return;
    wxAuiToolBar* tb = m_toolbar;
    
    if (op == OperationType::NONE) {
        tb->EnableTool(ID_TOOL_CANCEL, false);
    } else {
        tb->EnableTool(ID_TOOL_CANCEL, true);
        switch (op) {
            case OperationType::TEST:
                tb->SetToolShortHelp(ID_TOOL_CANCEL, "取消测试");
                break;
            case OperationType::UPDATE:
                tb->SetToolShortHelp(ID_TOOL_CANCEL, "停止更新");
                break;
            case OperationType::FIND:
                tb->SetToolShortHelp(ID_TOOL_CANCEL, "取消查找");
                break;
            case OperationType::SYNC:
                tb->SetToolShortHelp(ID_TOOL_CANCEL, "停止同步");
                break;
            case OperationType::AUTOTASK:
                tb->SetToolShortHelp(ID_TOOL_CANCEL, "停止自动任务");
                break;
            case OperationType::RESOLVE_REGION:
                tb->SetToolShortHelp(ID_TOOL_CANCEL, "取消地区解析");
                break;
            default:
                break;
        }
    }
}

void MainFrame::syncToolbarState() {
    if (!m_toolbar || !controller_) return;
    wxAuiToolBar* tb = m_toolbar;
    tb->EnableTool(ID_TOOL_CANCEL, controller_->isRunning());
}

void MainFrame::UpdateButtonStates() {
    // Update toolbar button states based on current selection and operation state.
    // Currently, only the Cancel button is state-dependent (managed by setOperationState).
    // This method serves as an extension point for future selection-dependent buttons.
    if (!m_toolbar) return;
    syncToolbarState();
}

void MainFrame::UpdateMenuStates() {
    // Update main menu item states based on current selection and operation state.
    // Currently, no menu items are selection-dependent.
    // This method serves as an extension point for future selection-dependent menu items.
    if (!menuBar_) return;
    // Future: enable/disable menu items depending on selection state
}

// -------------------------------------------------------------------
//  Initialization steps
// -------------------------------------------------------------------
void MainFrame::initMenuBar() {
    Logger::write("[MainFrame] initMenuBar step 1: creating wxMenuBar", LogLevel::DEBUG);
    wxMenuBar* bar = new wxMenuBar;
    Logger::write("[MainFrame] initMenuBar step 2: wxMenuBar created", LogLevel::DEBUG);

    wxMenu* fileMenu = new wxMenu;
    Logger::write("[MainFrame] initMenuBar step 3: fileMenu created", LogLevel::DEBUG);
    fileMenu->Append(ID_MENU_IMPORT_SUB,  "Import Subscription URL…\tCtrl+I");
    fileMenu->Append(ID_MENU_UPDATE_ALL,  "Update All Subscriptions\tCtrl+U");
    fileMenu->Append(ID_MENU_SYNC_DB,     "Sync Database…");
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_MENU_EXIT,        "Exit\tAlt+X");
    bar->Append(fileMenu, "&File");
    Logger::write("[MainFrame] initMenuBar step 4: fileMenu appended", LogLevel::DEBUG);

    wxMenu* proxyMenu = new wxMenu;
    proxyMenu->Append(ID_MENU_FIND_PROXY, "Find First Working Proxy\tCtrl+F");
    proxyMenu->Append(ID_MENU_FIND_BEST,  "Find Best Proxy\tCtrl+Shift+F");
    proxyMenu->AppendSeparator();
    proxyMenu->Append(ID_MENU_DEDUP,      "Remove Duplicates");
    proxyMenu->Append(ID_MENU_EXPORT,     "Export Share Links");
    proxyMenu->Append(ID_MENU_GEN_CONFIG, "Generate Config…");
    bar->Append(proxyMenu, "&Proxy");

    wxMenu* taskMenu = new wxMenu;
    taskMenu->Append(ID_MENU_AUTOTASK_RUN,  L"执行自动任务\tCtrl+T");
    taskMenu->Append(ID_MENU_AUTOTASK_RESUME, L"恢复自动任务");
    taskMenu->AppendSeparator();
    taskMenu->Append(ID_TOOL_CANCEL, L"取消任务");
    bar->Append(taskMenu, L"&任务");

    wxMenu* settingsMenu = new wxMenu;
    settingsMenu->Append(ID_MENU_CONFIG, "Configuration…\tCtrl+,");
    bar->Append(settingsMenu, "&Settings");

    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(ID_MENU_ABOUT, "About");
    bar->Append(helpMenu, "&Help");
    Logger::write("[MainFrame] initMenuBar step 5: helpMenu appended", LogLevel::DEBUG);

    Logger::write("[MainFrame] initMenuBar step 6: storing menuBar", LogLevel::DEBUG);
    // Keep menuBar_ for later SetMenuBar call (after AUI init + Update()).
    // Direct SetMenuBar inside initMenuBar causes intermittent hang on wxMSW 3.2.5/MinGW.
    menuBar_ = bar;
    Logger::write("[MainFrame] initMenuBar step 7: menuBar stored", LogLevel::DEBUG);
}

void MainFrame::initToolBar() {
    Logger::write("[MainFrame] initToolBar step 1: creating wxAuiToolBar", LogLevel::DEBUG);
    // Use wxAuiToolBar for better resize handling
    // wxAuiToolBar automatically handles control layout on window resize
    m_toolbar = new wxAuiToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 wxAUI_TB_HORIZONTAL | wxAUI_TB_TEXT);
    Logger::write("[MainFrame] initToolBar step 2: wxAuiToolBar created", LogLevel::DEBUG);
    m_toolbar->SetToolBitmapSize(wxSize(24, 24));

    // Note: wxAuiToolBar::AddTool uses different signature than wxToolBar
    // format: AddTool(id, label, bitmap, shortHelp)
    Logger::write("[MainFrame] initToolBar step 3: adding tools", LogLevel::DEBUG);
    m_toolbar->AddTool(ID_TOOL_UPDATE_ALL, "更新", ToolbarIcons::load("tool_update1"), "更新");
    Logger::write("[MainFrame] initToolBar step 3a: tool_update1 added", LogLevel::DEBUG);
    m_toolbar->AddTool(ID_TOOL_TEST, "测试", ToolbarIcons::load("tool_test"), "测试全部代理");
    m_toolbar->AddTool(ID_TOOL_CANCEL, "取消", ToolbarIcons::load("tool_cancel"), "取消测试");
    m_toolbar->EnableTool(ID_TOOL_CANCEL, false);  // disabled until operation starts
    m_toolbar->AddTool(ID_TOOL_SYNC, "同步", ToolbarIcons::load("tool_synchronize"), "同步");
    m_toolbar->AddTool(ID_TOOL_FIND, "查找", ToolbarIcons::load("tool_find"), "查找最佳代理");
    m_toolbar->AddTool(ID_TOOL_DEDUP, "去重", ToolbarIcons::load("tool_dedup"), "去重");
    m_toolbar->AddTool(ID_TOOL_IMPORT, "导入", ToolbarIcons::load("tool_import"), "增加新订阅");
    m_toolbar->AddTool(ID_TOOL_AUTOTASK, "自动任务", ToolbarIcons::load("tool_pipeline"), "自动任务");
    m_toolbar->AddTool(ID_TOOL_CONFIG, "配置", ToolbarIcons::load("tool_config"), "配置");

    // ── Search box: left-shifted by 150px from center ──
    m_toolbar->AddSpacer(70);  // small gap after tools, then search (shifted ~150px left)

    // Search target toggle: "代理" (search proxy list) or "订阅" (search subscriptions)
    wxString searchTargets[] = { "代理", "订阅" };
    m_searchTargetChoice = new wxChoice(m_toolbar, ID_SEARCH_TARGET,
                                        wxDefaultPosition, wxSize(60, 25));
    m_searchTargetChoice->Append("代理");
    m_searchTargetChoice->Append("订阅");
    m_searchTargetChoice->SetSelection(0);
    m_toolbar->AddControl(m_searchTargetChoice);

    m_searchBox = new wxSearchCtrl(m_toolbar, ID_SEARCH_BOX, wxEmptyString,
                                   wxDefaultPosition, wxSize(200, 25),
                                   wxTE_PROCESS_ENTER);
    m_searchBox->ShowSearchButton(true);
    m_searchBox->ShowCancelButton(true);
    m_toolbar->AddControl(m_searchBox);
    m_toolbar->AddStretchSpacer(1);  // Push toggle detail to right edge

    // Toggle detail panel button (rightmost)
    m_toggleDetailItem = m_toolbar->AddTool(ID_TOOL_DETAIL_TOGGLE, "详情",
                                             ToolbarIcons::load("tool_dockarrow"),
                                             "Toggle Detail Panel");

    m_toolbar->Realize();
}

void MainFrame::initStatusBar() {
    statusBar_ = CreateStatusBar(4);
    // Explicit widths (not equal-width): field0=status msg, field1=log file,
    // field2=network status, field3=database path.
    // SetStatusWidths forces SB_SETPARTS with real widths so SetStatusText
    // actually reaches the native control (equal-width path can leave parts
    // at width 0 until a WM_SIZE is processed).
    // -1 = variable-width field: field3 (database path) stretches to fill
    // the remaining status bar width so the bar spans the whole bottom row.
    int widths[] = { 250, 250, 120, -1 };
    statusBar_->SetStatusWidths(4, widths);
    statusBar_->SetStatusText("Ready", 0);
    statusBar_->SetStatusText("", 1);
    statusBar_->SetStatusText("", 2);
    statusBar_->SetStatusText(wxString(getDbPath()), 3);
}

void MainFrame::initAuiManager() {
     auiManager_ = new wxAuiManager;
     auiManager_->SetManagedWindow(this);
 }

void MainFrame::initPanels() {
    // ── Add toolbar to AUI manager for proper resize handling ──
    // Must be done after initAuiManager() sets the managed window
    if (m_toolbar) {
        auiManager_->AddPane(m_toolbar, wxAuiPaneInfo().Name("toolbar").ToolbarPane().Top().Row(0).Resizable(true));
    }

// ── Center panel first (parent for splitter) ──
    wxPanel* centerPanel = new wxPanel(this);
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);

    // ── Create splitter FIRST, then panels — splitter MUST be parent of windows it manages ──
    // Top row: subscription | proxy list (resizable via wxSplitterWindow)
    splitter_ = new wxSplitterWindow(centerPanel, wxID_ANY,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSP_3DSASH);
    splitter_->SetMinimumPaneSize(250);

    // Create panels with splitter as parent (required for SplitVertically)
    subPanel_ = new SubscriptionPanel(splitter_, controller_);
    proxyPanel_ = new ProxyListPanel(splitter_, controller_, db_);
    splitter_->SplitVertically(subPanel_, proxyPanel_, 360);

    detailPanel_ = new ProxyDetailPanel(this);  // AUI-managed, parent stays as MainFrame
    logPanel_ = new LogPanel(centerPanel);
    logPanel_->setInitialLogLevel(Logger::stringToLevel(config_.log_console_level));
    setLogFileLabel(Logger::getFilePath());

    centerSizer->Add(splitter_, 1, wxEXPAND);

    // Bottom row: log panel
    centerSizer->Add(logPanel_, 0, wxEXPAND | wxTOP, 2);
    logPanel_->SetMinSize(wxSize(620, 260));

    centerPanel->SetSizer(centerSizer);

    // ── AUI Pane Management ──
    // Center: main content area (sub/proxy/log panels)
    auiManager_->AddPane(centerPanel, wxAuiPaneInfo()
        .Name("centerPane")
        .CenterPane()
        .PaneBorder(false)
    );

    // Right: proxy detail panel (hidden by default)
    auiManager_->AddPane(detailPanel_, wxAuiPaneInfo()
        .Name("detailPane")
        .Caption("Proxy Details")
        .Right()
        .Layer(0).Position(0)
        .BestSize(320, -1)
        .MinSize(250, 400)
        .CloseButton(true)
        .PinButton(true)
        .Resizable(true)
        .Floatable(true)
        .Hide()
    );

    Logger::write("[MainFrame] AUI panes registered, calling Update()", LogLevel::DEBUG);
    auiManager_->Update();
    Logger::write("[MainFrame] auiManager_->Update() returned", LogLevel::DEBUG);

    // Load initial data (unchanged) — done before AUI Update() to ensure
    // data is ready when the layout triggers first paint
    subPanel_->loadSubscriptions();

    if (!subPanel_->getSubscriptions().empty()) {
        std::string firstSubId = subPanel_->getSubscriptions()[0].id;
        proxyPanel_->loadProxies(firstSubId);
    } else {
        proxyPanel_->loadProxies("");
    }
    Logger::write("[MainFrame] initPanels done", LogLevel::DEBUG);
}

void MainFrame::initTrayIcon() {
    trayIcon_ = new TrayIcon(this);
}

void MainFrame::loadSettings() {
    // Load window position / size from config if available
    // (placeholder — actual settings managed by ConfigDialog)
}

void MainFrame::repositionNetMonPanel() {
    if (!statusBar_ || !netMonPanel_) return;
    wxRect fieldRect;
    // Network status panel lives in field 2; field 1 is reserved for the
    // log file name label.
    statusBar_->GetFieldRect(2, fieldRect);
    netMonPanel_->SetSize(fieldRect);
    netMonPanel_->Refresh();
}

// -------------------------------------------------------------------
//  Network monitor timer — updates status bar field 1
// -------------------------------------------------------------------
void MainFrame::onNetMonTimer(wxTimerEvent&) {
    if (!controller_ || !netMonPanel_) return;
    NetworkMonitor* netMon = controller_->getNetworkMonitor();
    if (!netMon || !netMon->IsEnabled()) return;
    bool connected = netMon->IsConnected();
    if (connected != netMonConnected_) {
        netMonConnected_ = connected;
        netMonPanel_->Refresh();
    }
}

// -------------------------------------------------------------------
//  Event handlers — menus
// -------------------------------------------------------------------
void MainFrame::onClose(wxCloseEvent& event) {
    // Signal cancellation and let destructor handle cleanup
    if (controller_) {
        controller_->cancelTest();
    }

    // ── TrayIcon must be removed from shell BEFORE frame is destroyed -----
    // If the tray icon remains registered after the frame is destroyed,
    // the shell can send notifications to the now-freed hidden window,
    // and wxWidgets' message loop pumps those forever → process hangs.
    if (trayIcon_) {
        Logger::write("[MainFrame][onClose] RemoveTrayIcon before frame destroy", LogLevel::DEBUG);
        trayIcon_->RemoveIcon();
        delete trayIcon_;
        trayIcon_ = nullptr;
    }

    event.Skip();  // Allow frame destruction to proceed
}

void MainFrame::onIconize(wxIconizeEvent& event) {
    if (event.IsIconized() && trayIcon_) {
        // TODO: hide to tray
    }
    event.Skip();
}

void MainFrame::onMenuImportSub(wxCommandEvent&) {
    wxTextEntryDialog dlg(this, "Enter subscription URL:", "Import Subscription", "");
    if (dlg.ShowModal() == wxID_OK) {
        wxString url = dlg.GetValue();
        if (!url.empty()) {
            controller_->importSubscription(url.ToStdString());
        }
    }
}

void MainFrame::onMenuSyncDb(wxCommandEvent&) {
    wxTextEntryDialog dlg(this, "Source DB:", "Sync Database", "");
    if (dlg.ShowModal() != wxID_OK) return;
    std::string src = dlg.GetValue().ToStdString();

    wxTextEntryDialog dlg2(this, "Target DB:", "Sync Database", "");
    if (dlg2.ShowModal() != wxID_OK) return;
    std::string dst = dlg2.GetValue().ToStdString();

    // TODO: prompt for confirmation
    // controller_->syncDatabasesAsync(src, dst, this);
}

void MainFrame::onMenuExit(wxCommandEvent&) {
    Close();
}

void MainFrame::onMenuUpdateAll(wxCommandEvent&) {
    syncToolbarState();
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING, this);
        return;
    }
    setOperationState(OperationType::UPDATE);
    controller_->updateAllSubscriptionsAsync(this);
}

void MainFrame::onMenuFindProxy(wxCommandEvent&) {
    setOperationState(OperationType::FIND);
    setStatusText(0, "Finding first working proxy…");
    controller_->findFirstProxyAsync(this);
}

void MainFrame::onMenuFindBest(wxCommandEvent&) {
    setOperationState(OperationType::FIND);
    setStatusText(0, "Finding best proxy…");
    controller_->findBestProxyAsync(this);
}

void MainFrame::onMenuDedup(wxCommandEvent&) {
    bool ok = controller_->deduplicate();
    if (ok) {
        // Refresh subscription list (updates "Proxies" count column)
        if (subPanel_) {
            subPanel_->loadSubscriptions();
        }
        // Refresh current proxy list if a subscription is selected
        if (proxyPanel_ && subPanel_) {
            std::string currentSubId = subPanel_->getSelectedSubId();
            if (!currentSubId.empty()) {
                proxyPanel_->loadProxies(currentSubId);
            }
        }
    }
    wxMessageBox(ok ? "Dedup completed." : "Dedup failed.",
                 "Dedup", wxOK | (ok ? wxICON_INFORMATION : wxICON_WARNING));
    setStatusText(0, ok ? "Dedup completed." : "Dedup failed.");
}

void MainFrame::onMenuExportShareLink(wxCommandEvent&) {
    std::tuple<bool, int, std::string> exportResult = controller_->exportShareLinks();
    bool ok = std::get<0>(exportResult);
    int count = std::get<1>(exportResult);
    if (ok && count > 0) {
        setStatusText(0, wxString::Format("导出%d个有效代理至文件%s", count, std::get<2>(exportResult)));
    } else if (ok) {
        setStatusText(0, "没有有效代理可导出。");
    } else {
        setStatusText(0, "导出分享链接失败。");
    }
}

void MainFrame::onMenuGenerateConfig(wxCommandEvent&) {
    wxTextEntryDialog dlg(this, "Enter profile IndexId:", "Generate Config", "");
    if (dlg.ShowModal() != wxID_OK) return;
    wxString idx = dlg.GetValue();

    bool ok = controller_->generateConfig(idx.ToStdString());
    wxMessageBox(ok ? "Outbound config generated." : "Index not found.",
                 "Generate Config", wxOK | (ok ? wxICON_INFORMATION : wxICON_WARNING));
}

void MainFrame::onMenuConfig(wxCommandEvent&) {
    if (configDialog_) {
        delete configDialog_;
        configDialog_ = nullptr;
    }
    configDialog_ = new ConfigDialog(this, controller_->getConfig());
    if (configDialog_->ShowModal() == wxID_OK) {
        if (controller_->isRunning()) {
            wxMessageBox(L"操作进行中，无法保存配置", L"操作进行中", wxOK | wxICON_WARNING);
            return;
        }
        config::AppConfig cfg = configDialog_->getConfig();
        std::string oldDbPath = config_.database_path;
        // Capture old network monitor settings BEFORE saveConfig updates config_
        bool oldNetMonEnabled = controller_->getNetworkMonitor()->IsEnabled();
        int oldNetMonInterval = config_.network_monitor.checkIntervalMs;
        int oldNetMonTimeout = config_.network_monitor.checkTimeoutMs;
        bool saveOk = controller_->saveConfig(cfg);
        if (!saveOk) {
            wxMessageBox("Failed to save configuration to file.\n"
                         "Your changes may not persist after restart.",
                         "Save Error", wxOK | wxICON_WARNING);
        }

        // Detect network monitor changes
        bool netMonSettingsChanged = (cfg.network_monitor.enabled != oldNetMonEnabled) ||
                                     (cfg.network_monitor.checkUrls != config_.network_monitor.checkUrls) ||
                                     (cfg.network_monitor.checkIntervalMs != oldNetMonInterval) ||
                                     (cfg.network_monitor.checkTimeoutMs != oldNetMonTimeout);

        if (netMonSettingsChanged) {
            controller_->restartNetworkMonitor();
            if (netMonPanel_) {
                netMonPanel_->Show(cfg.network_monitor.enabled);
                repositionNetMonPanel();
            }
        }

        // Apply log level changes
        Logger::setFileLevel(Logger::stringToLevel(cfg.log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(cfg.log_console_level));

        // Keep the log panel's visible level filter in sync with the new console level
        if (logPanel_) {
            logPanel_->setInitialLogLevel(Logger::stringToLevel(cfg.log_console_level));
        }

        // If database path changed, switch to the new database at runtime
        if (cfg.database_path != oldDbPath && !cfg.database_path.empty()) {
            sqlite3* newDb = controller_->switchDatabase(cfg.database_path);

            if (newDb) {
                db_ = newDb;

                // Notify UIApp so main.cpp can close the correct handle on exit
                if (UIApp* uiApp = wxDynamicCast(wxApp::GetInstance(), UIApp)) {
                    uiApp->setDb(newDb);
                }

                // Update config path
                config_.database_path = cfg.database_path;
                if (statusBar_) {
                    statusBar_->SetStatusText(wxString(cfg.database_path), 3);
                }

                // Refresh all panels with the new database
                if (subPanel_) {
                    subPanel_->loadSubscriptions();
                }
                // Reload proxy list (empty subId = show all / first sub)
                if (subPanel_ && !subPanel_->getSubscriptions().empty()) {
                    std::string firstSubId = subPanel_->getSubscriptions()[0].id;
                    if (proxyPanel_) proxyPanel_->loadProxies(firstSubId);
                } else {
                    if (proxyPanel_) proxyPanel_->loadProxies("");
                }

                setStatusText(0, wxString("Switched to database: ") + cfg.database_path);
            } else {
                // Failed to open new database — restore old path in config
                cfg.database_path = oldDbPath;
                bool restoreOk = controller_->saveConfig(cfg);
                if (!restoreOk) {
                    wxMessageBox("Failed to restore previous database path in config file.\n"
                                 "You may need to manually edit config.json.",
                                 "Save Error", wxOK | wxICON_WARNING);
                }
                wxMessageBox("Failed to open the selected database file.\n"
                             "The previous database path has been restored.",
                             "Database Error", wxOK | wxICON_ERROR);
            }
        }
    }
    delete configDialog_;
    configDialog_ = nullptr;
}

void MainFrame::onMenuAutoTask(wxCommandEvent&) {
    syncToolbarState();
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING, this);
        return;
    }
    setOperationState(OperationType::AUTOTASK);
    setStatusText(0, L"自动任务开始…");
    controller_->runAutoTaskAsync(this);
}

void MainFrame::onMenuAutoTaskResume(wxCommandEvent&) {
    syncToolbarState();
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING, this);
        return;
    }
    setOperationState(OperationType::AUTOTASK);
    setStatusText(0, L"恢复自动任务…");
    controller_->resumeAutoTaskAsync(this);
}

void MainFrame::onMenuAbout(wxCommandEvent&) {
    wxString info;
    info.Printf(
        L"%hs v%hs\n\n"
        L"\u7248\u672C: %hs\n"
        L"Git Tag: %hs\n"
        L"Git Commit: %hs\n"
        L"\u6784\u5EFA\u7C7B\u578B: %hs\n"
        L"\u6784\u5EFA\u65F6\u95F4: %hs\n"
        L"\u7F16\u8BD1\u5668: %hs\n\n"
        L"\u7BA1\u7406\u5E76\u6D4B\u8BD5\u60A8\u7684\u4EE3\u7406\u8BA2\u9605\u3002",
        APP_NAME,
        APP_VERSION,
        APP_VERSION_FULL,
        APP_GIT_TAG,
        APP_GIT_COMMIT,
        APP_BUILD_TYPE,
        APP_BUILD_TIME,
        __VERSION__
    );

    wxMessageBox(info,
                 L"\u5173\u4E8E validproxy",
                 wxOK | wxICON_INFORMATION,
                 this);
}

// -------------------------------------------------------------------
//  Event handlers — toolbar
// -------------------------------------------------------------------
void MainFrame::onToolUpdateAll(wxCommandEvent& event) {
    onMenuUpdateAll(event);
}

void MainFrame::onTestSubscription(SubscriptionTestEvent& evt) {
    syncToolbarState();
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING, this);
        return;
    }
    setOperationState(OperationType::TEST);
    controller_->testSubscriptionAsync(evt.getSubId(), this);
    setStatusText(0, "Testing subscription…");
}

void MainFrame::onToolTest(wxCommandEvent& event) {
    syncToolbarState();
    if (controller_ && controller_->isRunning()) {
        wxMessageBox(L"操作进行中，请等待完成后再试", L"操作进行中", wxOK | wxICON_WARNING, this);
        return;
    }
    // Test ALL proxies (not just the selected subscription)
    setOperationState(OperationType::TEST);
    controller_->testAllProxiesAsync(this);
    setStatusText(0, "Testing all proxies…");
    (void)event;
}

void MainFrame::onToolCancel(wxCommandEvent&) {
    if (controller_) {
        controller_->cancelTest();
    }
    setOperationState(OperationType::NONE);
    setStatusText(0, "操作已取消");
}

void MainFrame::onToolFind(wxCommandEvent& event) {
    onMenuFindBest(event);
}

void MainFrame::onToolDedup(wxCommandEvent& event) {
    onMenuDedup(event);
}

void MainFrame::onToolImport(wxCommandEvent& event) {
    onMenuImportSub(event);
}

void MainFrame::onToolConfig(wxCommandEvent& event) {
    onMenuConfig(event);
}

void MainFrame::onToolSync(wxCommandEvent&) {
    setOperationState(OperationType::SYNC);
    setStatusText(0, "同步中…");
    if (controller_) {
        controller_->syncDatabasesAsync(this);
    }
}

// -------------------------------------------------------------------
//  Event handler — frame resize
// -------------------------------------------------------------------
void MainFrame::onResize(wxSizeEvent& event) {
    // Let the default handler process the resize first
    event.Skip();

    // Force AUI manager to update layout on resize
    // wxAuiToolBar automatically re-layouts controls when managed by AUI
    auiManager_->Update();
}

void MainFrame::onSearchBoxEnter(wxCommandEvent& event) {
    wxString query = m_searchBox->GetValue();
    setStatusText(0, "Search: " + query);
    if (m_searchTargetChoice->GetSelection() == 0) {
        if (proxyPanel_) {
            proxyPanel_->filterBySearch(query);
        }
    } else {
        if (subPanel_) {
            subPanel_->filterBySearch(query);
        }
    }
    (void)event;
}

void MainFrame::onSearchTextChanged(wxCommandEvent& event) {
    if (m_searchTargetChoice->GetSelection() == 0) {
        if (proxyPanel_) {
            proxyPanel_->filterBySearch(m_searchBox->GetValue());
        }
    } else {
        if (subPanel_) {
            subPanel_->filterBySearch(m_searchBox->GetValue());
        }
    }
    (void)event;
}

void MainFrame::onSearchClear(wxCommandEvent& event) {
    m_searchBox->SetValue("");
    // Clear both panels regardless of toggle
    if (proxyPanel_) {
        proxyPanel_->filterBySearch("");
    }
    if (subPanel_) {
        subPanel_->filterBySearch("");
    }
    (void)event;
}

void MainFrame::onToggleDetailPane(wxCommandEvent&) {
    wxAuiPaneInfo& pane = auiManager_->GetPane("detailPane");
    if (pane.IsOk()) {
        bool newVisible = !pane.IsShown();
        pane.Show(newVisible);
        detailPaneVisible_ = newVisible;
        if (newVisible) {
            pane.BestSize(320, -1);
        }
        auiManager_->Update();
    }
}

// -------------------------------------------------------------------
//  StatusUpdateEvent handler — fallback for messages from worker
//  threads that are NOT find-proxy payloads (already handled above
//  in the Bind lambda).
// -------------------------------------------------------------------
void MainFrame::onStatusUpdate(StatusUpdateEvent& event) {
    setStatusText(0, event.getText());
}

