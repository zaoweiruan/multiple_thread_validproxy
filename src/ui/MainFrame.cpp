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

#include <wx/sizer.h>
#include <wx/aui/auibook.h>
#include <wx/aui/auibar.h>
#include <wx/treectrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/artprov.h>
#include <wx/stdpaths.h>
#include <wx/srchctrl.h>
#include <wx/file.h>
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
    ID_TOOL_UPDATE_ALL    = wxID_HIGHEST + 200,
    ID_TOOL_TEST          = wxID_HIGHEST + 201,
    ID_TOOL_FIND          = wxID_HIGHEST + 202,
    ID_TOOL_DEDUP         = wxID_HIGHEST + 203,
    ID_TOOL_IMPORT        = wxID_HIGHEST + 204,
    ID_TOOL_CONFIG        = wxID_HIGHEST + 205,
    ID_TOOL_CANCEL        = wxID_HIGHEST + 208,
    ID_TOOL_SYNC          = wxID_HIGHEST + 209,
    ID_TOOL_CLEAR         = wxID_HIGHEST + 207,
    ID_SEARCH_BOX         = wxID_HIGHEST + 206,
    ID_TOOLBAR_DBPATH     = wxID_HIGHEST + 301,
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
            onStatusUpdate(evt);
        }
    });

    // ── Test completion → refresh Delay column and restore UI state ──
    Bind(wxEVT_PROXY_TEST_PROGRESS, [this](ProxyTestProgressEvent& evt) {
        if (evt.isCompleted() && proxyPanel_) {
            proxyPanel_->refreshResults();
        }
        if (evt.isCompleted()) {
            setOperationState(OperationType::NONE);
            setStatusText(0, "Test completed");
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
      
// Bind proxy selection to update detail panel
       Bind(wxEVT_PROXY_SELECTION, [this](ProxySelectionEvent& evt) {
           if (detailPanel_ && controller_) {
               // Get full proxy data from controller for advanced fields
               auto proxyOpt = controller_->getProxyByIndexId(evt.getIndexId());
               const db::models::Profileitem* proxy = proxyOpt.has_value() ? &*proxyOpt : nullptr;
               
               detailPanel_->UpdateDetail(
                   evt.getIndexId(), evt.getHost(), evt.getPort(), evt.getDelay(),
                   evt.getMessage(), evt.getFailures(), evt.getRemarks(), proxy);
           }
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
    Logger::write("[MainFrame] Constructor end", LogLevel::DEBUG);
}

MainFrame::~MainFrame() {
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
            default:
                break;
        }
    }
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
    m_toolbar->AddTool(ID_TOOL_CONFIG, "配置", ToolbarIcons::load("tool_config"), "配置");

    // ── Search box: left-shifted by 150px from center ──
    m_toolbar->AddSpacer(20);  // small gap after tools, then search (shifted ~150px left)
    m_searchBox = new wxSearchCtrl(m_toolbar, ID_SEARCH_BOX, wxEmptyString,
                                   wxDefaultPosition, wxSize(200, 25),
                                   wxTE_PROCESS_ENTER);
    m_searchBox->ShowSearchButton(true);
    m_searchBox->ShowCancelButton(true);
    m_toolbar->AddControl(m_searchBox);
    m_toolbar->AddStretchSpacer(1);  // Push dbPath + toggle detail to right edge

    // ── dbPath label: auto-width from font metrics + tooltip ──
    wxString dbPath = wxString(getDbPath());
    m_dbPathLabel = new wxStaticText(m_toolbar, ID_TOOLBAR_DBPATH,
                                     dbPath,
                                     wxDefaultPosition, wxDefaultSize,
                                     wxALIGN_RIGHT | wxST_ELLIPSIZE_END);
    // Match toolbar background so label blends in (no dark box)
    m_dbPathLabel->SetBackgroundColour(m_toolbar->GetBackgroundColour());
    m_dbPathLabel->SetForegroundColour(m_toolbar->GetForegroundColour());
    // Measure actual text width from current font, add 16px padding
    {
        wxClientDC dc(m_dbPathLabel);
        wxSize sz = dc.GetTextExtent(dbPath);
        m_dbPathLabel->SetMinSize(wxSize(sz.GetWidth() + 16, -1));
    }
    m_dbPathLabel->SetToolTip(dbPath);  // full path on hover
    m_toolbar->AddControl(m_dbPathLabel);

    // Toggle detail panel button (rightmost)
    m_toggleDetailItem = m_toolbar->AddTool(ID_TOOL_DETAIL_TOGGLE, "详情",
                                             ToolbarIcons::load("tool_dockarrow"),
                                             "Toggle Detail Panel");

    m_toolbar->Realize();
}

void MainFrame::initStatusBar() {
    statusBar_ = CreateStatusBar(3);
    statusBar_->SetStatusText("Ready", 0);
    statusBar_->SetStatusText("", 1);
    statusBar_->SetStatusText(wxString(getDbPath()), 2);
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

    // ── Center panel first (parent for sub/proxy/log panels) ──
    wxPanel* centerPanel = new wxPanel(this);
    wxBoxSizer* centerSizer = new wxBoxSizer(wxVERTICAL);

    // ── Create individual panels ──
    subPanel_ = new SubscriptionPanel(centerPanel, controller_);
    proxyPanel_ = new ProxyListPanel(centerPanel, controller_, db_);
    detailPanel_ = new ProxyDetailPanel(this);  // AUI-managed, parent stays as MainFrame
    logPanel_ = new LogPanel(centerPanel);

    subPanel_->SetMinSize(wxSize(380, -1));
    proxyPanel_->SetMinSize(wxSize(620, -1));
    // detailPanel_ is NO LONGER added to the wxBoxSizer

    // Top row: subscription | proxy list (no detail panel)
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
    topSizer->Add(subPanel_, 0, wxEXPAND | wxRIGHT, 2);
    topSizer->Add(proxyPanel_, 1, wxEXPAND);
    centerSizer->Add(topSizer, 1, wxEXPAND);

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

    // Right: proxy detail panel
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
    auto [ok, count, filename] = controller_->exportShareLinks();
    if (ok && count > 0) {
        setStatusText(0, wxString::Format("导出%d个有效代理至文件%s", count, filename));
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
    // Prevent config editing during active operations
    if (controller_->isRunning()) {
        wxMessageBox(L"请等待当前操作完成后再编辑配置", L"操作进行中", wxOK | wxICON_WARNING);
        return;
    }

    if (configDialog_) {
        delete configDialog_;
        configDialog_ = nullptr;
    }
    configDialog_ = new ConfigDialog(this, controller_->getConfig());
    if (configDialog_->ShowModal() == wxID_OK) {
        config::AppConfig cfg = configDialog_->getConfig();
        std::string oldDbPath = config_.database_path;
        controller_->saveConfig(cfg);

        // Apply log level changes
        Logger::setFileLevel(Logger::stringToLevel(cfg.log_file_level));
        Logger::setConsoleLevel(Logger::stringToLevel(cfg.log_console_level));

        // If database path changed, switch to the new database at runtime
        if (cfg.database_path != oldDbPath && !cfg.database_path.empty()) {
            sqlite3* newDb = controller_->switchDatabase(cfg.database_path);

            if (newDb) {
                db_ = newDb;

                // Notify UIApp so main.cpp can close the correct handle on exit
                if (UIApp* uiApp = wxDynamicCast(wxApp::GetInstance(), UIApp)) {
                    uiApp->setDb(newDb);
                }

                // Update config path and toolbar dbPath label
                config_.database_path = cfg.database_path;
                if (m_dbPathLabel) {
                    m_dbPathLabel->SetLabel(wxString(cfg.database_path));
                    m_dbPathLabel->SetToolTip(wxString(cfg.database_path));
                }
                if (statusBar_) {
                    statusBar_->SetStatusText(wxString(cfg.database_path), 2);
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
                controller_->saveConfig(cfg);
                wxMessageBox("Failed to open the selected database file.\n"
                             "The previous database path has been restored.",
                             "Database Error", wxOK | wxICON_ERROR);
            }
        }
    }
    delete configDialog_;
    configDialog_ = nullptr;
}

void MainFrame::onMenuAbout(wxCommandEvent&) {
    wxMessageBox("validproxy v1.0\nManage and test your proxy subscriptions.",
                 "About", wxOK | wxICON_INFORMATION, this);
}

// -------------------------------------------------------------------
//  Event handlers — toolbar
// -------------------------------------------------------------------
void MainFrame::onToolUpdateAll(wxCommandEvent& event) {
    onMenuUpdateAll(event);
}

void MainFrame::onTestSubscription(SubscriptionTestEvent& evt) {
    setOperationState(OperationType::TEST);
    controller_->testSubscriptionAsync(evt.getSubId(), this);
    setStatusText(0, "Testing subscription…");
}

void MainFrame::onToolTest(wxCommandEvent& event) {
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
    if (proxyPanel_) {
        proxyPanel_->filterBySearch(query);
    }
    (void)event;
}

void MainFrame::onSearchTextChanged(wxCommandEvent& event) {
    if (proxyPanel_) {
        proxyPanel_->filterBySearch(m_searchBox->GetValue());
    }
    (void)event;
}

void MainFrame::onSearchClear(wxCommandEvent& event) {
    m_searchBox->SetValue("");
    if (proxyPanel_) {
        proxyPanel_->filterBySearch("");
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
