#ifndef UI_LOG_PANEL_H
#define UI_LOG_PANEL_H

#include <wx/wx.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/button.h>

#include "Logger.h"

class LogMessageEvent;

// ---------------------------------------------------------------
// LogPanel — scrolling log viewer with level filtering
// ---------------------------------------------------------------
class LogPanel : public wxPanel {
public:
    explicit LogPanel(wxWindow* parent);
    ~LogPanel() override;

    void appendLog(const wxString& msg, LogLevel level);
    void clearLog();
    void setLevelFilter(LogLevel minLevel);

private:
    void onLogMessage(LogMessageEvent& event);
    void onClear(wxCommandEvent& event);
    void onFilterChange(wxCommandEvent& event);

    wxTextCtrl* logCtrl_;       // read-only multi-line text
    wxChoice* levelFilter_;     // filter dropdown
    wxButton* clearBtn_;        // clear button

    LogLevel minLevel_{LogLevel::TRACE};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_LOG_PANEL_H
