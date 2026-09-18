#include "UIApp.h"
#include "MainFrame.h"
#include "service/DatabaseConnectionService.h"

#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <windows.h>
#include <filesystem>
#include <exception>
#include <string>

// Write an assertion report next to the executable for redundancy (the primary
// copy goes to the harness-redirected stderr). File-static helper.
static void WriteAssertCapture(const wxString& text);

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

        std::optional<config::AppConfig> loadedConfig = config::ConfigReader::load(configPath.string());
        std::string effectiveConfigPath = configPath.string();
        if (loadedConfig) {
            cfg_ = *loadedConfig;
        } else {
            // Try default config path
            effectiveConfigPath = config::ConfigReader::getDefaultConfigPath();
            loadedConfig = config::ConfigReader::load(effectiveConfigPath);
            if (loadedConfig) {
                cfg_ = *loadedConfig;
            } else {
                // Both config loads failed - specific error already shown by ConfigReader::load()
                return false;
            }
        }

        // Validate database path is not empty
        if (cfg_.database_path.empty()) {
            wxMessageBox("Database path is empty in configuration file.\nPlease check the database.path setting and restart.",
                         "Configuration Error", wxOK | wxICON_ERROR);
            return false;
        }

        // Validate database file exists before opening
        std::filesystem::path dbPath(cfg_.database_path);
        if (!std::filesystem::exists(dbPath)) {
            wxMessageBox("Database file does not exist.\n\nConfig path:\n" + effectiveConfigPath + "\nDatabase path:\n" + cfg_.database_path + "\n\nThe application cannot start.",
                         "Database Error", wxOK | wxICON_ERROR);
            return false;
        }

        // Open database
        service::DatabaseConnectionService dbService;
        db_ = dbService.open(cfg_.database_path);
        if (!db_) {
            wxMessageBox("Failed to open database.\n\nConfig path:\n" + effectiveConfigPath + "\nDatabase path:\n" + cfg_.database_path,
                         "Database Error", wxOK | wxICON_ERROR);
            return false;
        }
    }

    // Create the main application frame
    // (SetMenuBar is deferred inside MainFrame constructor — after AUI init
    //  — to avoid a hang on wxMSW 3.2.5/MinGW and to ensure AUI computes
    //  pane sizes against the correct client area with the menu bar present.)
    try {
        frame_ = new MainFrame(cfg_, db_);
        SetTopWindow(frame_);
        frame_->Maximize(true);  // Start maximized
        frame_->Show(true);
    } catch (const std::exception& e) {
        wxMessageBox("Application initialization failed:\n" + wxString(e.what()),
                     "Startup Error", wxOK | wxICON_ERROR);
        return false;
    } catch (...) {
        wxMessageBox("Unknown error during application startup.",
                     "Startup Error", wxOK | wxICON_ERROR);
        return false;
    }

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

void UIApp::OnAssertFailure(const wxChar *file, int line, const wxChar *func,
                            const wxChar *cond, const wxChar *msg)
{
    // UI-test / CI mode: log the assertion instead of showing the modal dialog
    // (which would block app initialization and is unreadable by the harness).
    // The text goes to stderr (redirected to a file by the test harness) and to
    // a dedicated file next to the executable for redundancy.
    wxString mode;
    if (wxGetEnv("VALIDPROXY_ASSERT_LOG", &mode) && mode == "1") {
        const wxChar* empty = wxT("");
        wxString text = wxString::Format(
            wxT("WX_ASSERT file=%s line=%d func=%s\n  cond=%s\n  msg=%s\n"),
            file ? file : empty,
            line,
            func ? func : empty,
            cond ? cond : empty,
            msg ? msg : empty);

        // (1a) stderr — captured by the harness' redirected child stderr.
        ::fwprintf(stderr, wxT("%s"), static_cast<const wchar_t*>(text.wc_str()));
        ::fflush(stderr);

        // (1b) redundant dedicated file.
        WriteAssertCapture(text);
        return; // no dialog -> initialization continues
    }

    // Normal run: preserve the default assert dialog.
    wxApp::OnAssertFailure(file, line, func, cond, msg);
}

void WriteAssertCapture(const wxString& text)
{
    wxString exePath = wxStandardPaths::Get().GetExecutablePath();
    wxFileName fn(exePath);
    fn.SetFullName(wxString::Format(wxT("ui-assert-%lu.log"),
                                    static_cast<unsigned long>(::GetCurrentProcessId())));
    wxFFile f(fn.GetFullPath(), "a");
    if (f.IsOpened()) {
        f.Write(text);
        f.Close();
    }
}
