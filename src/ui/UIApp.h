#ifndef UI_APP_H
#define UI_APP_H

#include <wx/wx.h>
#include <sqlite3.h>
#include "ConfigReader.h"

class MainFrame;

// ---------------------------------------------------------------
// UIApp — wxWidgets application for GUI mode
// ---------------------------------------------------------------
class UIApp : public wxApp {
public:
    UIApp() = default;  // Default constructor required by wxIMPLEMENT_APP
    UIApp(const config::AppConfig& cfg, sqlite3* db);
    bool OnInit() override;
    int OnExit() override;
    void OnUnhandledException() override;
    bool OnExceptionInMainLoop() override;
private:
    config::AppConfig cfg_;
    sqlite3* db_{nullptr};
    MainFrame* frame_{nullptr};
};

wxDECLARE_APP(UIApp);

#endif // UI_APP_H
