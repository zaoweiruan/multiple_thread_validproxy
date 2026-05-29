#ifndef UI_TEST_PANEL_H
#define UI_TEST_PANEL_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>          // P3-10: Explicit include for wxTextCtrl

#include <atomic>
#include <string>

#include "Logger.h"
#include "TestLogMediator.h"
#include "Events.h"

class ProxyTestProgressEvent;
class TestEvent;

// ---------------------------------------------------------------
// TestPanel — shows proxy batch testing progress & results
// ---------------------------------------------------------------
class TestPanel : public wxPanel {
public:
    explicit TestPanel(wxWindow* parent);
    ~TestPanel();           // dtor: Logger::popCallback() restores previous callback

    void startTest(const std::string& subId);
    void cancelTest();
    bool isRunning() const { return isRunning_; }

    // Log area (read-only)
    void appendLogLine(const std::string& line);
    void resetLog();

    static constexpr int MAX_LOG_LINES = 500;      // log lines cap

private:
    void onProgress(ProxyTestProgressEvent& event);
    void onCancel(wxCommandEvent& event);
    void onLogMessage(LogMessageEvent& event);
    void handleTestEvent(const TestEvent& event);

    wxGauge* progressBar_;
    wxListCtrl* resultList_;
    wxButton* cancelBtn_;
    wxStaticText* statusLabel_;
    wxStaticText* progressText_;
    wxTextCtrl* logCtrl_;          // read-only log area

    std::atomic<bool> isRunning_{false};
    int total_{0};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_TEST_PANEL_H
