#ifndef UI_TEST_PANEL_H
#define UI_TEST_PANEL_H

#include <wx/wx.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>

#include <atomic>
#include <string>

class ProxyTestProgressEvent;

// ---------------------------------------------------------------
// TestPanel — shows proxy batch testing progress & results
// ---------------------------------------------------------------
class TestPanel : public wxPanel {
public:
    explicit TestPanel(wxWindow* parent);

    void startTest(const std::string& subId);
    void cancelTest();
    bool isRunning() const { return isRunning_; }

private:
    void onProgress(ProxyTestProgressEvent& event);
    void onCancel(wxCommandEvent& event);

    wxGauge* progressBar_;
    wxListCtrl* resultList_;
    wxButton* cancelBtn_;
    wxStaticText* statusLabel_;
    wxStaticText* progressText_;

    std::atomic<bool> isRunning_{false};
    int total_{0};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_TEST_PANEL_H
