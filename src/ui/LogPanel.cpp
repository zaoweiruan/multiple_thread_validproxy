#include "LogPanel.h"
#include "Events.h"
#include "LogStatistics.h"

#include <wx/textctrl.h>
#include <wx/sizer.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/stdpaths.h>
#include <sstream>
#include <shellapi.h>

// Event table bindings (panel-level only)
wxBEGIN_EVENT_TABLE(LogPanel, wxPanel)
    EVT_BUTTON(ID_LOG_CLEAR, LogPanel::onClear)
    EVT_BUTTON(ID_LOG_OPEN, LogPanel::onOpenLog)
    EVT_BUTTON(ID_LOG_STATISTICS, LogPanel::onLogStatistics)
    EVT_CHOICE(ID_LOG_FILTER, LogPanel::onFilterChange)
wxEND_EVENT_TABLE()

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
    minLevel_ = LogLevel::INFO; // Initialize to match the default selection
    toolSizer->Add(levelFilter_, 0, wxRIGHT, 8);

    clearBtn_ = new wxButton(this, ID_LOG_CLEAR, "清空日志窗口");
    toolSizer->Add(clearBtn_, 0);

    statsBtn_ = new wxButton(this, ID_LOG_STATISTICS, "日志统计");
    toolSizer->Add(statsBtn_, 0, wxLEFT, 8);

    openBtn_ = new wxButton(this, ID_LOG_OPEN, "打开日志");
    toolSizer->Add(openBtn_, 0, wxLEFT, 8);

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
    Bind(wxEVT_LOG_STATISTICS, &LogPanel::onLogStatisticsResult, this);

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
    if (statsThread_.joinable()) {
        statsThread_.join();
    }
    if (statsDialog_) {
        statsDialog_->Destroy();
        statsDialog_ = nullptr;
    }
}

void LogPanel::appendLog(const wxString& msg, LogLevel level) {
     // Only show messages at or above the current filter level
     if (level < minLevel_) {
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

void LogPanel::onOpenLog(wxCommandEvent&) {
    std::string logPath = Logger::getFilePath();
    if (logPath.empty()) {
        return;
    }
#ifdef __WXMSW__
    ShellExecuteA(NULL, "open", logPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
    wxLaunchDefaultApplication(logPath);
#endif
}

void LogPanel::onLogStatistics(wxCommandEvent&) {
    if (statsThread_.joinable()) {
        return; // a statistics parse is already running; ignore repeated clicks
    }
    std::string logPath = Logger::getFilePath();
    statisticsForFile(logPath);
}

void LogPanel::statisticsForFile(const std::string& logPath) {
    if (statsThread_.joinable()) {
        return; // a parse is already running; ignore
    }
    statsCurrentPath_ = logPath;
    if (statsDialog_ && statsTextCtrl_) {
        statsTextCtrl_->SetValue(wxString::FromUTF8("正在解析日志文件...\n" + logPath));
    }
    statsThread_ = std::thread([this, logPath]() {
        LogStatisticsResult result = parseLogFile(logPath);
        wxQueueEvent(this, new LogStatisticsEvent(logPath, result));
    });
}

void LogPanel::onLogStatisticsResult(LogStatisticsEvent& event) {
    // 回收已结束的后台解析线程：std::thread 执行完毕后 joinable() 仍为 true，
    // 若不在此 join，下次点击 onLogStatistics 会被防叠加守卫拦截而无法再次统计。
    if (statsThread_.joinable()) {
        statsThread_.join();
    }
    const LogStatisticsResult& result = event.getResult();
    std::string filePath = event.getFilePath();
    std::string text = buildStatisticsText(result, filePath);

    if (!statsDialog_) {
        statsDialog_ = new wxDialog(this, wxID_ANY, "日志统计", wxDefaultPosition, wxSize(760, 560),
                                    wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
        wxBoxSizer* dialogSizer = new wxBoxSizer(wxVERTICAL);
        statsTextCtrl_ = new wxTextCtrl(statsDialog_, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
        statsTextCtrl_->SetFont(wxFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dialogSizer->Add(statsTextCtrl_, 1, wxEXPAND | wxALL, 8);

        wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
        wxButton* selectBtn = new wxButton(statsDialog_, wxID_ANY, "选择日志文件");
        wxButton* closeBtn = new wxButton(statsDialog_, wxID_OK, "关闭");
        btnSizer->Add(selectBtn, 0, wxRIGHT, 8);
        btnSizer->Add(closeBtn, 0);
        dialogSizer->Add(btnSizer, 0, wxALIGN_CENTER | wxBOTTOM, 8);

        statsDialog_->SetSizer(dialogSizer);
        statsDialog_->Centre();

        selectBtn->Bind(wxEVT_BUTTON, &LogPanel::onSelectLogFile, this);
        closeBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            if (statsDialog_) {
                statsDialog_->Destroy();
                statsDialog_ = nullptr;
            }
        });
        statsDialog_->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) {
            if (statsDialog_) {
                statsDialog_->Destroy();
                statsDialog_ = nullptr;
            }
        });
    }

    statsTextCtrl_->SetValue(wxString::FromUTF8(text));
    statsDialog_->Show();
    statsDialog_->Raise();
}

void LogPanel::onSelectLogFile(wxCommandEvent&) {
    wxFileDialog fileDialog(this, "选择日志文件", "", "",
                            "日志文件 (*.log;*.txt)|*.log;*.txt|所有文件 (*.*)|*.*",
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (fileDialog.ShowModal() == wxID_OK) {
        std::string newPath = fileDialog.GetPath().ToStdString();
        statisticsForFile(newPath);
    }
}

std::string LogPanel::buildStatisticsText(const LogStatisticsResult& result,
                                          const std::string& filePath) {
    std::ostringstream oss;
    oss << "日志文件: " << filePath << "\n";
    oss << "有效日志行: " << result.counts.total << "\n";
    oss << "\n===== 级别统计 =====\n";
    oss << "TRACE : " << result.counts.trace << "\n";
    oss << "DEBUG : " << result.counts.debug << "\n";
    oss << "INFO  : " << result.counts.info << "\n";
    oss << "REPORT: " << result.counts.report << "\n";
    oss << "WARN  : " << result.counts.warn << "\n";
    oss << "ERROR : " << result.counts.error << "\n";
    oss << "\n===== WARN 原因明细 (" << result.warnReasons.size() << " 类) =====\n";
    if (result.warnReasons.empty()) {
        oss << "(无)\n";
    }
    for (size_t i = 0; i < result.warnReasons.size(); ++i) {
        oss << i + 1 << ". [" << result.warnReasons[i].count << " 次] "
            << result.warnReasons[i].reason << "\n";
    }
    oss << "\n===== ERROR 原因明细 (" << result.errorReasons.size() << " 类) =====\n";
    if (result.errorReasons.empty()) {
        oss << "(无)\n";
    }
    for (size_t i = 0; i < result.errorReasons.size(); ++i) {
        oss << i + 1 << ". [" << result.errorReasons[i].count << " 次] "
            << result.errorReasons[i].reason << "\n";
    }
    return oss.str();
}

void LogPanel::setInitialLogLevel(LogLevel level) {
    minLevel_ = level;
    // Sync the wxChoice dropdown to match
    switch (level) {
        case LogLevel::TRACE: levelFilter_->SetSelection(0); break;
        case LogLevel::DEBUG: levelFilter_->SetSelection(1); break;
        case LogLevel::INFO:  levelFilter_->SetSelection(2); break;
        case LogLevel::REPORT: levelFilter_->SetSelection(3); break;
        case LogLevel::WARN: levelFilter_->SetSelection(4); break;
        case LogLevel::ERR:   levelFilter_->SetSelection(5); break;
    }
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
