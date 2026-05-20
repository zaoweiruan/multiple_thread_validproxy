#include "LogPanel.h"
#include "Events.h"

#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/choice.h>
#include <wx/button.h>

// Event table bindings (non-custom events only)
wxBEGIN_EVENT_TABLE(LogPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, LogPanel::onClear)
    EVT_CHOICE(wxID_ANY, LogPanel::onFilterChange)
wxEND_EVENT_TABLE()

enum {
    ID_LOG_CLEAR = wxID_HIGHEST + 200,
    ID_LOG_FILTER,
};

// -------------------------------------------------------------------
LogPanel::LogPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // Toolbar row: filter + clear button
    wxBoxSizer* toolSizer = new wxBoxSizer(wxHORIZONTAL);

    wxStaticText* filterLabel = new wxStaticText(this, wxID_ANY, "Level:");
    toolSizer->Add(filterLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);

    wxArrayString levels;
    levels.Add("TRACE");
    levels.Add("DEBUG");
    levels.Add("INFO");
    levels.Add("REPORT");
    levels.Add("WARN");
    levels.Add("ERROR");
    levelFilter_ = new wxChoice(this, ID_LOG_FILTER, wxDefaultPosition, wxSize(100, -1), levels);
    levelFilter_->SetSelection(2); // default: INFO
    toolSizer->Add(levelFilter_, 0, wxRIGHT, 8);

    clearBtn_ = new wxButton(this, ID_LOG_CLEAR, "Clear");
    toolSizer->Add(clearBtn_, 0);

    toolSizer->AddStretchSpacer();
    topSizer->Add(toolSizer, 0, wxEXPAND | wxALL, 4);

    // Log text control
    logCtrl_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                               wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY |
                               wxHSCROLL);
    logCtrl_->SetBackgroundColour(wxColour(255, 255, 255));
    logCtrl_->SetForegroundColour(wxColour(0, 0, 0));
    logCtrl_->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    topSizer->Add(logCtrl_, 1, wxEXPAND | wxALL, 4);

    SetSizer(topSizer);

    // Bind custom events
    Bind(wxEVT_LOG_MESSAGE, &LogPanel::onLogMessage, this);

    // Register as log callback
    Logger::setLogCallback([this](const std::string& msg, LogLevel level) {
        // Forward to UI thread via wxQueueEvent
        if (this) {
            wxQueueEvent(this, new LogMessageEvent(msg, level));
        }
    });
}

LogPanel::~LogPanel() {
    Logger::clearLogCallback();
}

void LogPanel::appendLog(const wxString& msg, LogLevel /*level*/) {
    if (static_cast<int>(minLevel_) > static_cast<int>(LogLevel::TRACE)) {
        return;
    }
    logCtrl_->AppendText(msg + "\n");
    logCtrl_->ShowPosition(logCtrl_->GetLastPosition());
}

void LogPanel::clearLog() {
    logCtrl_->Clear();
}

void LogPanel::setLevelFilter(LogLevel minLevel) {
    minLevel_ = minLevel;
}

void LogPanel::onLogMessage(LogMessageEvent& event) {
    appendLog(event.getMessage(), event.getLevel());
}

void LogPanel::onClear(wxCommandEvent&) {
    clearLog();
}

void LogPanel::onFilterChange(wxCommandEvent&) {
    int sel = levelFilter_->GetSelection();
    switch (sel) {
        case 0: minLevel_ = LogLevel::TRACE; break;
        case 1: minLevel_ = LogLevel::DEBUG; break;
        case 2: minLevel_ = LogLevel::INFO;  break;
        case 3: minLevel_ = LogLevel::REPORT; break;
        case 4: minLevel_ = LogLevel::WARN;  break;
        case 5: minLevel_ = LogLevel::ERR;   break;
        default: minLevel_ = LogLevel::INFO;
    }
}
