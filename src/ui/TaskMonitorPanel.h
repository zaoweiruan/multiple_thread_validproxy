#ifndef UI_TASK_MONITOR_PANEL_H
#define UI_TASK_MONITOR_PANEL_H

#include <wx/wx.h>
#include <wx/listctrl.h>

#include <string>
#include <vector>

#include "task/TaskQueue.h"

class AppController;
class wxListEvent;

// ---------------------------------------------------------------
// TaskMonitorPanel — shows the running task and the queued-task list
// with manual cancel actions (spec 2026-11-03 v1.2 §3.10).
// The list holds the RUNNING task first (status column shows RUNNING),
// then the queued tasks in dequeue order (owner request 2026-11-04).
//
// Refresh model: NO own timer. MainFrame's single bottomViewTimer_ calls
// refreshNow() once per second while this page is the visible one;
// setActive() only tracks whether refreshing is wanted. All controller
// calls run on the UI thread.
// ---------------------------------------------------------------
class TaskMonitorPanel : public wxPanel {
public:
    TaskMonitorPanel(wxWindow* parent, AppController* controller);

    // Called by MainFrame on page switches; true right before refreshNow().
    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }

    // Re-read the controller snapshot and update every control. UI thread only.
    void refreshNow();

private:
    void onCancelCurrent(wxCommandEvent& event);
    void onCancelSelected(wxCommandEvent& event);
    void onCancelAll(wxCommandEvent& event);
    void onItemSelected(wxListEvent& event);
    void updateButtonStates(const task::TaskQueueSnapshot& snap);
    // Selected pending row's taskId (from the per-row client data), "" if none.
    std::string selectedTaskId() const;

    AppController* controller_;
    bool active_{false};

    wxStaticText* currentInfo_{nullptr};
    wxListCtrl* pendingList_{nullptr};
    wxButton* cancelCurrentBtn_{nullptr};
    wxButton* cancelSelectedBtn_{nullptr};
    wxButton* cancelAllBtn_{nullptr};
    long selectedRow_{-1};
    // Row index → taskId (kept parallel with the list after every refresh;
    // never dereference TaskHandle pointers across refreshes).
    std::vector<std::string> pendingIds_;
    // Parallel to pendingIds_: true for auto-task child (step) rows, which
    // are monitor-only — the cancel-selected action never targets them.
    std::vector<bool> rowIsChild_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_TASK_MONITOR_PANEL_H
