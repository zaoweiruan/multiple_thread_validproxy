#ifndef UI_LOG_PANEL_H
#define UI_LOG_PANEL_H

#include <wx/wx.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <string>
#include <thread>

#include "Logger.h"

class LogMessageEvent;
class LogStatisticsEvent;
struct LogStatisticsResult;

enum {
    ID_LOG_CLEAR = wxID_HIGHEST + 200,
    ID_LOG_FILTER,
    ID_LOG_OPEN,
    ID_LOG_STATISTICS,
};

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
    void setInitialLogLevel(LogLevel level);

private:
    void onLogMessage(LogMessageEvent& event);
    void onLogStatisticsResult(LogStatisticsEvent& event);
    void onClear(wxCommandEvent& event);
    void onFilterChange(wxCommandEvent& event);
    void onOpenLog(wxCommandEvent& event);
    void onLogStatistics(wxCommandEvent& event);
    void statisticsForFile(const std::string& logPath);
    void onSelectLogFile(wxCommandEvent& event);
    std::string buildStatisticsText(const LogStatisticsResult& result,
                                    const std::string& filePath);

    wxTextCtrl* logCtrl_;         // read-only multi-line text
    wxChoice* levelFilter_;       // filter dropdown
    wxButton* clearBtn_;          // clear button
    wxButton* statsBtn_;          // log statistics button
    wxButton* openBtn_;           // open log file button

    wxDialog* statsDialog_{nullptr};     // modeless statistics dialog
    wxTextCtrl* statsTextCtrl_{nullptr}; // read-only text inside stats dialog

    std::thread statsThread_;     // background parse thread for log statistics
    std::string statsCurrentPath_;// current file path shown in stats dialog

    LogLevel minLevel_{LogLevel::TRACE};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_LOG_PANEL_H
