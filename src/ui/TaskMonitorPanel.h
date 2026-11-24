#ifndef UI_TASK_MONITOR_PANEL_H
#define UI_TASK_MONITOR_PANEL_H

#include <wx/wx.h>
#include <wx/listctrl.h>

#include <string>
#include <vector>

#include "task/TaskQueue.h"

class AppController;
class wxListEvent;
class wxPopupTransientWindow;

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
    // Hover report popup (task-report-hover Task 6). wxEVT_MOTION over the
    // list shows the finished row's report popup; wxEVT_LEAVE_WINDOW on the
    // list AND the popup hides it (the transient window has no mouse-leave
    // dismissal of its own for wxPU_CONTAINS_CONTROLS).
    void onListMotion(wxMouseEvent& event);
    void onListLeave(wxMouseEvent& event);
    void showReportPopup(const std::string& taskId, const wxPoint& pos);
    void hideReportPopup();
    // Context menu over the list (spec 2026-11-06 §3.3). Single entry point:
    // wxEVT_CONTEXT_MENU covers row AND blank space; ITEM_RIGHT_CLICK would
    // fire a second time for row clicks on wxMSW.
    void onListContextMenu(wxContextMenuEvent& event);
    void onContextCommand(wxCommandEvent& event);
    void updateButtonStates(const task::TaskQueueSnapshot& snap);
    // Selected pending row's taskId (from the per-row client data), "" if none.
    std::string selectedTaskId() const;
    // RowKind values for rowKinds_ / the context menu.
    enum RowKind { RowCurrent = 0, RowChild = 1, RowPending = 2, RowFinished = 3 };

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
    // Parallel to pendingIds_: RowKind per row (spec §3.2). Drives the
    // context-menu items and the 取消选中 enablement (Finished rows are
    // terminal — they get 移除, not 取消).
    std::vector<int> rowKinds_;
    // Context-menu target captured at popup time (the menu command arrives
    // after the list may have been re-selected elsewhere).
    std::string ctxTaskId_;
    int ctxRowKind_{-1};

    // Hovered finished row + the report popup showing its report. hoveredRow_
    // is the row index that last showed the popup (restored after each 1s
    // rebuild by scanning pendingIds_); popupTaskId_ is the report currently
    // shown ("" when none). Both are UI-thread only.
    wxPopupTransientWindow* reportPopup_{nullptr};
    wxTextCtrl* reportPopupText_{nullptr};
    long hoveredRow_{-1};
    std::string popupTaskId_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_TASK_MONITOR_PANEL_H
