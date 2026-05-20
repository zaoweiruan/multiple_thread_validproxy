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

#include <wx/sizer.h>
#include <wx/aui/auibook.h>
#include <wx/treectrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/artprov.h>
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
    ID_SEARCH_BOX         = wxID_HIGHEST + 206,
};

// -------------------------------------------------------------------
wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::onClose)
    EVT_ICONIZE(MainFrame::onIconize)
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
    EVT_TEXT_ENTER(ID_SEARCH_BOX,  MainFrame::onSearchBoxEnter)
wxEND_EVENT_TABLE()

// -------------------------------------------------------------------
//  Construction / Destruction
// -------------------------------------------------------------------
MainFrame::MainFrame(const config::AppConfig& cfg, sqlite3* db)
    : wxFrame(nullptr, wxID_ANY, "validproxy - Proxy Manager",
              wxDefaultPosition, wxSize(1200, 800)),
      db_(db)
{
    controller_ = new AppController(db, cfg);

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
    wxToolBar* tb = CreateToolBar();

    tb->AddTool(ID_TOOL_UPDATE_ALL, "Update", wxArtProvider::GetBitmap(wxART_EXECUTABLE_FILE));
    tb->AddTool(ID_TOOL_TEST,       "Test",   wxArtProvider::GetBitmap(wxART_TICK_MARK));
    tb->AddTool(ID_TOOL_FIND,       "Find",   wxArtProvider::GetBitmap(wxART_FIND));
    tb->AddTool(ID_TOOL_DEDUP,      "Dedup",  wxArtProvider::GetBitmap(wxART_LIST_VIEW));
    tb->AddTool(ID_TOOL_IMPORT,     "Import", wxArtProvider::GetBitmap(wxART_FILE_OPEN));
    tb->AddTool(ID_TOOL_CONFIG,     "Config", wxArtProvider::GetBitmap(wxART_LIST_VIEW));
    tb->AddSeparator();
    tb->AddControl(new wxStaticText(tb, wxID_ANY, " Search:"));
    m_searchBox = new wxTextCtrl(tb, ID_SEARCH_BOX, "", wxDefaultPosition, wxSize(150, -1), wxTE_PROCESS_ENTER);
    tb->AddControl(m_searchBox);
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
    proxyPanel_->loadProxies("");
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

void MainFrame::onSearchBoxEnter(wxCommandEvent& event) {
    wxString query = m_searchBox->GetValue();
    setStatusText(0, "Search: " + query);
    if (proxyPanel_) {
        proxyPanel_->filterBySearch(query);
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
