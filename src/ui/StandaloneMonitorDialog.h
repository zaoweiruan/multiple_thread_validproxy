#ifndef UI_STANDALONE_MONITOR_DIALOG_H
#define UI_STANDALONE_MONITOR_DIALOG_H

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/timer.h>

class AppController;

// ---------------------------------------------------------------
// StandaloneMonitorDialog — non-modal dialog listing every watched
// standalone proxy process joined with its proxy_runtime_history
// session data: start time, live runtime (minutes), SOCKS listening
// port and pid. Refreshes every 2 seconds via wxTimer.
// Spec: docs/specs/2026-08-21-Spec-StandaloneMonitor-Dialog-v1.0.md
//
// Lifetime: owned by MainFrame, created lazily and reused (hidden
// instead of destroyed on close) so the timer state stays simple.
// ---------------------------------------------------------------
class StandaloneMonitorDialog : public wxDialog {
public:
    StandaloneMonitorDialog(wxWindow* parent, AppController* controller);

    // Start/stop the refresh timer together with visibility.
    bool Show(bool show = true) override;

private:
    void onRefreshTimer(wxTimerEvent& event);
    void onCloseButton(wxCommandEvent& event);
    void onCloseWindow(wxCloseEvent& event);
    void refreshRows();

    AppController* controller_;
    wxListCtrl* list_;
    wxTimer timer_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_STANDALONE_MONITOR_DIALOG_H
