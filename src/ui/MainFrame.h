#ifndef UI_MAIN_FRAME_H
#define UI_MAIN_FRAME_H

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/statusbr.h>

#include <string>

#include "ConfigReader.h"
#include "Events.h"

struct sqlite3;

class AppController;
class SubscriptionPanel;
class ProxyListPanel;
class TestPanel;
class LogPanel;
class ConfigDialog;
class TrayIcon;

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
    TestPanel* getTestPanel() const { return testPanel_; }
    LogPanel* getLogPanel() const { return logPanel_; }
    AppController* getController() const { return controller_; }

    // Status bar helpers
    void setStatusText(int field, const wxString& text);
    void showBalloon(const wxString& title, const wxString& msg);

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
    void onMenuAddSub(wxCommandEvent& event);
    void onMenuFindProxy(wxCommandEvent& event);
    void onMenuFindBest(wxCommandEvent& event);
    void onMenuDedup(wxCommandEvent& event);
    void onMenuExportShareLink(wxCommandEvent& event);
    void onMenuGenerateConfig(wxCommandEvent& event);
    void onMenuConfig(wxCommandEvent& event);
    void onMenuAbout(wxCommandEvent& event);
    void onToolUpdateAll(wxCommandEvent& event);
    void onToolTest(wxCommandEvent& event);
    void onToolFind(wxCommandEvent& event);
    void onToolDedup(wxCommandEvent& event);
    void onToolImport(wxCommandEvent& event);
    void onToolConfig(wxCommandEvent& event);
    void onStatusUpdate(StatusUpdateEvent& event);

    // Members
    wxAuiManager auiManager_;
    AppController* controller_;
    SubscriptionPanel* subPanel_{nullptr};
    ProxyListPanel* proxyPanel_{nullptr};
    TestPanel* testPanel_{nullptr};
    LogPanel* logPanel_{nullptr};
    ConfigDialog* configDialog_{nullptr};
    TrayIcon* trayIcon_{nullptr};
    sqlite3* db_;
    wxStatusBar* statusBar_{nullptr};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_MAIN_FRAME_H
