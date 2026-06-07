#include "UIApp.h"
#include "MainFrame.h"

#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <filesystem>
#include <exception>
#include <iostream>

// -------------------------------------------------------------------
// wxIMPLEMENT_APP_NO_MAIN — provides wxAppConsole-derived class
// initialization glue without defining main().
// -------------------------------------------------------------------
wxIMPLEMENT_APP(UIApp);

// -------------------------------------------------------------------
// UIApp
// -------------------------------------------------------------------
UIApp::UIApp(const config::AppConfig& cfg, sqlite3* db)
    : cfg_(cfg), db_(db)
{
    // wxApp handles locale, display, etc.
}

bool UIApp::OnInit()
{
    // Initialize image handlers (required for XPM/PNG support)
    wxInitAllImageHandlers();

    // If config/database not set (detached launch), load defaults
    if (!db_) {
        std::string exeDir = wxStandardPaths::Get().GetExecutablePath().ToStdString();
        size_t pos = exeDir.find_last_of("/\\");
        if (pos != std::string::npos) {
            exeDir = exeDir.substr(0, pos);
        }
        
        std::filesystem::path configPath = std::filesystem::path(exeDir) / "config.json";
        if (!std::filesystem::exists(configPath)) {
            configPath = std::filesystem::path(exeDir) / ".." / "config.json";
        }
        
        auto loadedConfig = config::ConfigReader::load(configPath.string());
        if (loadedConfig) {
            cfg_ = *loadedConfig;
        } else {
            // Try default config path
            loadedConfig = config::ConfigReader::load(config::ConfigReader::getDefaultConfigPath());
            if (loadedConfig) {
                cfg_ = *loadedConfig;
            }
        }
        
        // Open database
        if (sqlite3_open(cfg_.database_path.c_str(), &db_) != SQLITE_OK) {
            wxMessageBox("Failed to open database.\nThe application cannot start.",
                         "Database Error", wxOK | wxICON_ERROR);
            return false;
        }
    }

    // Create the main application frame
    // (SetMenuBar is deferred inside MainFrame constructor — after AUI init
    //  — to avoid a hang on wxMSW 3.2.5/MinGW and to ensure AUI computes
    //  pane sizes against the correct client area with the menu bar present.)
    frame_ = new MainFrame(cfg_, db_);
    SetTopWindow(frame_);
    frame_->Maximize(true);  // Start maximized
    frame_->Show(true);

    return true;
}

int UIApp::OnExit()
{
    // MainFrame owns the AppController which does not need
    // explicit cleanup here — wxWidgets child window deletion
    // handles it. The caller (main.cpp) is responsible for
    // closing the sqlite3 handle.
    return wxApp::OnExit();
}

void UIApp::OnUnhandledException()
{
    try {
        throw; // re-throw to identify the exception type
    } catch (const std::exception& e) {
        wxMessageBox("Unhandled exception: " + wxString(e.what()),
                     "Fatal Error", wxOK | wxICON_ERROR);
    } catch (...) {
        wxMessageBox("Unknown unhandled exception.",
                     "Fatal Error", wxOK | wxICON_ERROR);
    }
}

bool UIApp::OnExceptionInMainLoop()
{
    try {
        throw;
    } catch (const std::exception& e) {
        wxMessageBox("Exception in main loop: " + wxString(e.what()),
                     "Error", wxOK | wxICON_ERROR);
    } catch (...) {
        wxMessageBox("Unknown exception in main loop.",
                     "Error", wxOK | wxICON_ERROR);
    }
    // Continue the event loop by default
    return true;
}
