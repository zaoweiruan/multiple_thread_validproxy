#include "MainFrame.h"
#include "ConfigDialog.h"
#include "LogPanel.h"
#include "ProxyDetailPanel.h"
#include "ProxyListPanel.h"
#include "SubscriptionPanel.h"
#include "TrayIcon.h"
#include "AppController.h"
#include "Events.h"
#include "Profileitem.h"
#include "ToolbarIcons.h"

#include <wx/sizer.h>
#include <wx/aui/auibook.h>
#include <wx/treectrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/artprov.h>
#include <wx/stdpaths.h>
#include <wx/srchctrl.h>
#include <wx/file.h>
#include <thread>

// -------------------------------------------------------------------
//  Menu / Tool identifiers
// -------------------------------------------------------------------
enum {
    ID_MENU_IMPORT_SUB    = wxID_HIGHEST + 100,
    ID_MENU_SYNC_DB       = wxID_HIGHEST + 101,
    ID_MENU_EXIT          = wxID_HIGHEST + 102,
    ID_MENU_UPDATE_ALL    = wxID_HIGHEST + 103,
    ID_MENU_ADD_SUB       = wxID_HIGHEST + 104,
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
    ID_TOOL_CLEAR         = wxID_HIGHEST + 207,
    ID_SEARCH_BOX         = wxID_HIGHEST + 206,
    ID_TOOLBAR_DBPATH     = wxID_HIGHEST + 301,
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
    EVT_MENU(ID_MENU_ADD_SUB,     MainFrame::onMenuAddSub)
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
    // Search
    EVT_TEXT_ENTER(ID_SEARCH_BOX,  MainFrame::onSearchBoxEnter)
    EVT_TEXT(ID_SEARCH_BOX,        MainFrame::onSearchTextChanged)
    EVT_SEARCH_CANCEL(ID_SEARCH_BOX, MainFrame::onSearchClear)
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

    // Set application icon from ICO file
    wxString exeDir = wxStandardPaths::Get().GetExecutablePath().BeforeLast('/');
    #ifdef __WXMSW__
    exeDir = wxStandardPaths::Get().GetExecutablePath().BeforeLast('\\');
    #endif
    wxString iconPath = exeDir + "/docs/design/ui/icon.ico";
    if (wxFile::Exists(iconPath)) {
        wxIcon appIcon(iconPath, wxBITMAP_TYPE_ICO);
        if (appIcon.IsOk()) {
            SetIcon(appIcon);
        } else {
            // Fallback to wxArtProvider themed icon
            wxBitmap appBitmap = wxArtProvider::GetBitmap(wxART_FRAME_ICON);
            if (appBitmap.IsOk()) {
                wxIcon appIcon;
                appIcon.CopyFromBitmap(appBitmap);
                SetIcon(appIcon);
            }
        }
    } else {
        // Fallback to wxArtProvider themed icon
        wxBitmap appBitmap = wxArtProvider::GetBitmap(wxART_FRAME_ICON);
        if (appBitmap.IsOk()) {
            wxIcon appIcon;
            appIcon.CopyFromBitmap(appBitmap);
            SetIcon(appIcon);
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
        } else {
            onStatusUpdate(evt);
        }
    });

    // ── Test completion → refresh Delay column ────────────────────
    Bind(wxEVT_PROXY_TEST_PROGRESS, [this](ProxyTestProgressEvent& evt) {
        if (evt.isCompleted() && proxyPanel_) {
            proxyPanel_->refreshResults();
        }
    });

    // ── Subscription right-click Test ────────────────────────────
    Bind(wxEVT_SUBSCRIPTION_TEST, &MainFrame::onTestSubscription, this);

initMenuBar();
     initToolBar();
     initStatusBar();
     initAuiManager();
     initPanels();
     
// Bind subscription selection to filter proxy list
      Bind(wxEVT_SUBSCRIPTION_SELECTED, [this](SubscriptionSelectedEvent& evt) {
          std::string subId = evt.getSubId();
          if (proxyPanel_) {
              proxyPanel_->loadProxies(subId);
          }
          setStatusText(0, "Loaded subscription: " + wxString(subId));
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
     loadSettings();
}

MainFrame::~MainFrame() {
    // Step 1: AUI must be torn down before any panel/frame member is destroyed
    // (AUI holds references to managed panes — pointers must be valid here)
    auiManager_.UnInit();

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

// -------------------------------------------------------------------
//  Initialization steps
// -------------------------------------------------------------------
void MainFrame::initMenuBar() {
    wxMenuBar* bar = new wxMenuBar;

    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(ID_MENU_IMPORT_SUB,  "Import Subscription URL…\tCtrl+I");
    fileMenu->Append(ID_MENU_UPDATE_ALL,  "Update All Subscriptions\tCtrl+U");
    fileMenu->Append(ID_MENU_SYNC_DB,     "Sync Database…");
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_MENU_EXIT,        "Exit\tAlt+X");
    bar->Append(fileMenu, "&File");

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

    SetMenuBar(bar);
}

void MainFrame::initToolBar() {
    wxToolBar* tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
    tb->SetToolBitmapSize(wxSize(24, 24));

    tb->AddTool(ID_TOOL_UPDATE_ALL, "Update", ToolbarIcons::load("tool_update"));
    tb->AddTool(ID_TOOL_TEST,       "Test",   ToolbarIcons::load("tool_test"));
    tb->AddTool(ID_TOOL_FIND,       "Find",   ToolbarIcons::load("tool_find"));
    tb->AddTool(ID_TOOL_DEDUP,      "Dedup",  ToolbarIcons::load("tool_dedup"));
    tb->AddTool(ID_TOOL_IMPORT,     "Import", ToolbarIcons::load("tool_import"));
    tb->AddTool(ID_TOOL_CONFIG,     "Config", ToolbarIcons::load("tool_config"));

    // Search box — on left side, above Host column
    tb->AddControl(new wxStaticText(tb, wxID_ANY, "", wxDefaultPosition, wxSize(240, -1)));  // 240px spacer
    tb->AddControl(new wxStaticText(tb, wxID_ANY, " Search:"));
    m_searchBox = new wxSearchCtrl(tb, ID_SEARCH_BOX, wxEmptyString,
                                   wxDefaultPosition, wxSize(150, -1),
                                   wxTE_PROCESS_ENTER);
    m_searchBox->ShowSearchButton(true);
    m_searchBox->ShowCancelButton(true);
    tb->AddControl(m_searchBox);
    tb->AddStretchableSpace();  // Push dbPathLabel to right

    // Database path — dynamically resized in onResize()
    m_dbPathLabel = new wxStaticText(tb, wxID_ANY, wxString(getDbPath()),
                                     wxDefaultPosition, wxSize(300, -1),
                                     wxALIGN_RIGHT | wxST_ELLIPSIZE_START);
    tb->AddControl(m_dbPathLabel);

    tb->Realize();
}

void MainFrame::initStatusBar() {
    statusBar_ = CreateStatusBar(3);
    statusBar_->SetStatusText("Ready", 0);
    statusBar_->SetStatusText("", 1);
    statusBar_->SetStatusText("", 2);
}

void MainFrame::initAuiManager() {
    auiManager_.SetManagedWindow(this);
}

void MainFrame::initPanels() {
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    // Top row: subscription | proxy list | detail
    wxBoxSizer* topSizer = new wxBoxSizer(wxHORIZONTAL);
    subPanel_ = new SubscriptionPanel(this, controller_);
    proxyPanel_ = new ProxyListPanel(this, controller_, db_);
    detailPanel_ = new ProxyDetailPanel(this);
    logPanel_ = new LogPanel(this);

    // Size hints for column widths
    subPanel_->SetMinSize(wxSize(380, -1));
    proxyPanel_->SetMinSize(wxSize(620, -1));
    detailPanel_->SetMinSize(wxSize(320, -1));

    topSizer->Add(subPanel_, 0, wxEXPAND | wxRIGHT, 2);
    topSizer->Add(proxyPanel_, 1, wxEXPAND | wxRIGHT, 2);
    topSizer->Add(detailPanel_, 0, wxEXPAND);
    sizer->Add(topSizer, 1, wxEXPAND | wxALL, 2);

    // Bottom row: log panel under proxy list area
    sizer->Add(logPanel_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 2);
    logPanel_->SetMinSize(wxSize(620, 260));

    SetSizer(sizer);

    // Load initial data
    subPanel_->loadSubscriptions();

    // Auto-load first subscription
    if (!subPanel_->getSubscriptions().empty()) {
        std::string firstSubId = subPanel_->getSubscriptions()[0].id;
        proxyPanel_->loadProxies(firstSubId);
    } else {
        proxyPanel_->loadProxies("");
    }
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
    setStatusText(0, "Updating all subscriptions...");
    controller_->updateAllSubscriptionsAsync(this);
}

void MainFrame::onMenuAddSub(wxCommandEvent&) {
    // Forward to SubscriptionPanel
    if (subPanel_) {
        subPanel_->showAddDialog();
    }
}

void MainFrame::onMenuFindProxy(wxCommandEvent&) {
    setStatusText(0, "Finding first working proxy…");
    controller_->findFirstProxyAsync(this);
}

void MainFrame::onMenuFindBest(wxCommandEvent&) {
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
    (void)controller_->exportShareLinks();
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
        config::AppConfig cfg = configDialog_->getConfig();
        controller_->saveConfig(cfg);
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
    controller_->testSubscriptionAsync(evt.getSubId(), this);
    setStatusText(0, "Testing subscription…");
}

void MainFrame::onToolTest(wxCommandEvent& event) {
    // Start test for the currently-selected subscription
    if (subPanel_) {
        std::string subId = subPanel_->getSelectedSubId();
        if (!subId.empty()) {
            controller_->testSubscriptionAsync(subId, this);
            setStatusText(0, "Testing subscription…");
        }
    }
    (void)event;  // keep compiler happy
}

void MainFrame::onToolFind(wxCommandEvent& event) {
    onMenuFindProxy(event);
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

// -------------------------------------------------------------------
//  Event handler — dynamic toolbar control sizing on frame resize
// -------------------------------------------------------------------
void MainFrame::onResize(wxSizeEvent& event) {
    // Let the default handler process the resize first (repositions children)
    event.Skip();

    wxToolBar* tb = GetToolBar();
    if (!tb || !m_searchBox || !m_dbPathLabel) return;

    // Query control positions AFTER toolbar has recalculated its layout
    int tbWidth   = tb->GetClientSize().GetWidth();
    wxPoint searchPos = m_searchBox->GetPosition();
    wxPoint dbPos     = m_dbPathLabel->GetPosition();

    // Defensive: if toolbar is too narrow and controls overlap, fall back to minima
    if (dbPos.x <= searchPos.x) {
        m_searchBox->SetSize(80, -1);
        m_dbPathLabel->SetSize(60, -1);
    } else {
        // Search box: everything from its x-position up to the db-path label
        int searchWidth = dbPos.x - searchPos.x;
        searchWidth = std::max(80, std::min(200, searchWidth));
        m_searchBox->SetSize(searchWidth, -1);

        // DB path label: from its x-position to toolbar right edge (minus margin)
        int dbWidth = tbWidth - dbPos.x - 8;
        dbWidth = std::max(60, dbWidth);
        m_dbPathLabel->SetSize(dbWidth, -1);
    }

    // Force toolbar to repaint — clears visual remnants left by the native
    // wxSearchCtrl window at its old position after SetSize narrows it.
    tb->Refresh();
    tb->Update();
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

// -------------------------------------------------------------------
//  StatusUpdateEvent handler — fallback for messages from worker
//  threads that are NOT find-proxy payloads (already handled above
//  in the Bind lambda).
// -------------------------------------------------------------------
void MainFrame::onStatusUpdate(StatusUpdateEvent& event) {
    setStatusText(0, event.getText());
}
