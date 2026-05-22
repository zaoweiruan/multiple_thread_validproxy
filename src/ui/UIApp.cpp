#include "UIApp.h"
#include "MainFrame.h"

#include <wx/image.h>
#include <wx/msgdlg.h>
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

    // Ensure we have a database connection
    if (!db_) {
        wxMessageBox("No database connection available.\nThe application cannot start.",
                     "Database Error", wxOK | wxICON_ERROR);
        return false;
    }

    // Create the main application frame
    frame_ = new MainFrame(cfg_, db_);
    SetTopWindow(frame_);
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
