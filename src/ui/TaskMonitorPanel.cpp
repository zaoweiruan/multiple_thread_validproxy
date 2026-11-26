#include "TaskMonitorPanel.h"
#include "i18n/I18nWx.h"

#include <wx/display.h>
#include <wx/dcclient.h>
#include <wx/popupwin.h>
#include <wx/statline.h>

#include <chrono>
#include <functional>
#include <map>
#include <string>

#include "AppController.h"
#include "Events.h"
#include "UiControlIds.h"
#include "task/TaskFormat.h"
#include "task/TaskType.h"

wxBEGIN_EVENT_TABLE(TaskMonitorPanel, wxPanel)
wxEND_EVENT_TABLE()

namespace {

// "查看报告" context-menu command id. Kept file-local (not in UiControlIds.h):
// it is TaskMonitorPanel-internal (the menu item is only built here and only
// handled here), and this task's change set is limited to TaskMonitorPanel.*.
// wxID_HIGHEST == 6000 → 7505; the UiControlIds block uses +1500..+1504.
const int kCtxViewReport = wxID_HIGHEST + 1505;

// Report popup sizing (2026-11-26): the popup adapts to its content instead of
// the former fixed 560x420. The computed text extent + padding is clamped to
// [min, work-area percentage]; overlong content scrolls inside the text ctrl.
const int kPopupPaddingX = 24;   // horizontal padding incl. text-ctrl border
const int kPopupPaddingY = 28;   // vertical padding incl. text-ctrl border
const int kPopupMinWidth = 360;
const int kPopupMinHeight = 120;
const int kPopupMaxWorkAreaPercentX = 90;
const int kPopupMaxWorkAreaPercentY = 80;

// Popup anchor offset from the trigger point (cursor / focused row).
const int kPopupAnchorOffset = 8;

// Hover report popup. A plain wxPopupTransientWindow dismisses itself the
// moment it loses activation (WM_ACTIVATE WA_INACTIVE → DismissOnDeactivate),
// but a hover report must NOT vanish because unrelated focus churn (the 1s
// task-list refresh, a focus change) deactivates it — it is closed explicitly
// on list leave / popup leave / row change / missing report only. Swallow the
// deactivation so only those explicit paths dismiss it (it stays a real
// wxPopupTransientWindow per the brief; the subclass only removes the
// transient deactivation auto-close).
class ReportPopupWindow : public wxPopupTransientWindow {
public:
    explicit ReportPopupWindow(wxWindow* parent)
        : wxPopupTransientWindow(parent, wxPU_CONTAINS_CONTROLS) {}

    bool MSWHandleMessage(WXLRESULT* result, WXUINT message,
                          WXWPARAM wParam, WXLPARAM lParam) override {
        if (message == WM_ACTIVATE && wParam == WA_INACTIVE) {
            return true;  // keep open; closed only via hideReportPopup()
        }
        return wxPopupTransientWindow::MSWHandleMessage(result, message,
                                                        wParam, lParam);
    }
};

// Measure a multiline text block with the text control's font: width = widest
// line, height = line count * line height (empty lines still occupy a line).
wxSize measureReportText(wxWindow* textCtrl, const wxString& content) {
    wxClientDC dc(textCtrl);
    dc.SetFont(textCtrl->GetFont());
    int maxWidth = 0;
    int lineHeight = dc.GetCharHeight();
    int lineCount = 1;
    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = content.find('\n', start);
        const wxString line = (newline == wxString::npos)
                                  ? content.Mid(start)
                                  : content.Mid(start, newline - start);
        int w = 0;
        int h = 0;
        dc.GetTextExtent(line, &w, &h);
        if (w > maxWidth) maxWidth = w;
        if (h > lineHeight) lineHeight = h;
        if (newline == wxString::npos) break;
        ++lineCount;
        start = newline + 1;
    }
    return wxSize(maxWidth, lineHeight * lineCount);
}

const char* statusToString(task::TaskStatus s) {
    switch (s) {
    case task::TaskStatus::PENDING:   return "PENDING";
    case task::TaskStatus::QUEUED:    return "QUEUED";
    case task::TaskStatus::RUNNING:   return "RUNNING";
    case task::TaskStatus::COMPLETED: return "COMPLETED";
    case task::TaskStatus::FAILED:    return "FAILED";
    case task::TaskStatus::CANCELLED: return "CANCELLED";
    case task::TaskStatus::REJECTED:  return "REJECTED";
    case task::TaskStatus::SKIPPED:   return "SKIPPED";
    }
    return "UNKNOWN";
}

// Steady-count string → "Ns" / "- -" (spec §3.7: never a wall-clock time).
wxString elapsedText(const std::string& steadyCount) {
    const long long secs = task::elapsedSecondsSince(
        steadyCount, std::chrono::steady_clock::now());
    if (secs < 0) return wxString("- -");
    return wxString::Format("%llds", secs);
}

} // namespace

TaskMonitorPanel::TaskMonitorPanel(wxWindow* parent, AppController* controller)
    : wxPanel(parent, ID_TASK_MONITOR_PANEL), controller_(controller) {
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    currentInfo_ = new wxStaticText(this, ID_TASK_CURRENT_INFO,
                                    i18n::wx(i18n::StrId::TaskMonitor_Empty));
    root->Add(currentInfo_, 0, wxEXPAND | wxALL, 4);

    pendingList_ = new wxListCtrl(this, ID_TASK_PENDING_LIST, wxDefaultPosition,
                                  wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    pendingList_->InsertColumn(0, i18n::wx(i18n::StrId::TaskMonitor_Column_Id), wxLIST_FORMAT_LEFT, 150);
    pendingList_->InsertColumn(1, i18n::wx(i18n::StrId::TaskMonitor_Column_Type), wxLIST_FORMAT_LEFT, 160);
    pendingList_->InsertColumn(2, i18n::wx(i18n::StrId::TaskMonitor_Column_Name), wxLIST_FORMAT_LEFT, 140);
    pendingList_->InsertColumn(3, i18n::wx(i18n::StrId::TaskMonitor_Column_Priority), wxLIST_FORMAT_RIGHT, 60);
    pendingList_->InsertColumn(4, i18n::wx(i18n::StrId::TaskMonitor_Column_Status), wxLIST_FORMAT_LEFT, 90);
    pendingList_->InsertColumn(5, i18n::wx(i18n::StrId::TaskMonitor_Column_Submitted), wxLIST_FORMAT_RIGHT, 90);
    root->Add(pendingList_, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    cancelCurrentBtn_ = new wxButton(this, ID_TASK_CANCEL_CURRENT,
                                     i18n::wx(i18n::StrId::TaskMonitor_Button_Terminate));
    cancelSelectedBtn_ = new wxButton(this, ID_TASK_CANCEL_SELECTED,
                                      i18n::wx(i18n::StrId::TaskMonitor_Button_CancelSelected));
    cancelAllBtn_ = new wxButton(this, ID_TASK_CANCEL_ALL,
                                 i18n::wx(i18n::StrId::TaskMonitor_Button_CancelAll));
    buttons->Add(cancelCurrentBtn_, 0, wxRIGHT, 6);
    buttons->Add(cancelSelectedBtn_, 0, wxRIGHT, 6);
    buttons->Add(cancelAllBtn_, 0);
    buttons->AddStretchSpacer();
    root->Add(buttons, 0, wxEXPAND | wxALL, 4);

    SetSizer(root);

    cancelCurrentBtn_->Bind(wxEVT_BUTTON, &TaskMonitorPanel::onCancelCurrent, this);
    cancelSelectedBtn_->Bind(wxEVT_BUTTON, &TaskMonitorPanel::onCancelSelected, this);
    cancelAllBtn_->Bind(wxEVT_BUTTON, &TaskMonitorPanel::onCancelAll, this);
    pendingList_->Bind(wxEVT_LIST_ITEM_SELECTED, &TaskMonitorPanel::onItemSelected, this);
    pendingList_->Bind(wxEVT_LIST_ITEM_DESELECTED, &TaskMonitorPanel::onItemSelected, this);
    // Hover report popup (task-report-hover Task 6): motion over a finished
    // row shows the report; leaving the list hides it. The transient window
    // has no mouse-leave dismissal of its own for wxPU_CONTAINS_CONTROLS, so
    // the popup is bound to the same leave handler (hideReportPopup).
    pendingList_->Bind(wxEVT_MOTION, &TaskMonitorPanel::onListMotion, this);
    pendingList_->Bind(wxEVT_LEAVE_WINDOW, &TaskMonitorPanel::onListLeave, this);
    // Context menu (spec 2026-11-06 §3.3): wxEVT_CONTEXT_MENU handles both row
    // and blank-space right-clicks with one entry point (on wxMSW, row right-
    // clicks fire ITEM_RIGHT_CLICK *and* propagate WM_CONTEXTMENU, so binding
    // both events would double-popup the menu).
    pendingList_->Bind(wxEVT_CONTEXT_MENU, &TaskMonitorPanel::onListContextMenu, this);
    // task-report-hover Task 6 (bugfix): explicit click-away dismissal for the
    // report popup (see onPanelMouseDown). Bound on the PANEL so clicks anywhere
    // in the task monitor area (blank space / list) are considered.
    Bind(wxEVT_LEFT_DOWN, &TaskMonitorPanel::onPanelMouseDown, this);
    // Menu commands bind on the PANEL, not the list: wxMSW routes a popup's
    // WM_COMMAND to the window whose PopupMenu() call owned the track loop
    // (this->PopupMenu below → the panel's HWND). The same pattern as
    // ProxyListPanel's EVT_MENU context bindings. Binding on pendingList_
    // would never fire (verified 2026-11-09 via UIA invoke + ledger unchanged).
    Bind(wxEVT_MENU, &TaskMonitorPanel::onContextCommand, this, ID_TASK_CTX_CLEAR_ALL);
    Bind(wxEVT_MENU, &TaskMonitorPanel::onContextCommand, this, ID_TASK_CTX_CANCEL_ONE);
    Bind(wxEVT_MENU, &TaskMonitorPanel::onContextCommand, this, ID_TASK_CTX_REMOVE_ONE);
    Bind(wxEVT_MENU, &TaskMonitorPanel::onContextCommand, this, ID_TASK_CTX_CANCEL_ALL);
    Bind(wxEVT_MENU, &TaskMonitorPanel::onContextCommand, this, ID_TASK_CTX_TERM_ONE);
    Bind(wxEVT_MENU, &TaskMonitorPanel::onContextCommand, this, kCtxViewReport);

    updateButtonStates(task::TaskQueueSnapshot());   // start fully disabled
}

void TaskMonitorPanel::refreshNow() {
    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();

    // task-report-hover Task 6: capture the popup's taskId BEFORE the row
    // rebuild (DeleteAllItems destroys row identity), so hoveredRow_ can be
    // restored by taskId afterwards. Fixed order: capture → rebuild →
    // restore hoveredRow_ → conditional-hide (see the end of this method).
    const std::string popupTaskIdBefore = popupTaskId_;

    // ── current task line ──
    const bool idle = snap.current.taskId.empty();
    if (idle) {
        currentInfo_->SetLabel(i18n::wx(i18n::StrId::TaskMonitor_Empty));
    } else {
        wxString line = wxString::FromUTF8(task::taskTypeToString(snap.current.type));
        if (!snap.current.targetId.empty()) {
            line << wxString::FromUTF8(u8" [") << wxString::FromAscii(snap.current.targetId.c_str())
                 << wxString::FromUTF8(u8"]");
        }
        line << i18n::wx(i18n::StrId::TaskMonitor_Current_Status) << wxString::FromAscii(statusToString(snap.current.status));
        if (snap.current.progressTotal > 0) {
            const double pct = snap.current.progressCurrent * 100.0 / snap.current.progressTotal;
            line << i18n::wx(i18n::StrId::TaskMonitor_Current_Progress)
                 << wxString::Format("%d/%d (%.1f%%)", snap.current.progressCurrent,
                                     snap.current.progressTotal, pct);
        }
        line << i18n::wx(i18n::StrId::TaskMonitor_Current_Elapsed) << elapsedText(snap.current.startedAt);
        if (!snap.current.progressMessage.empty()) {
            line << i18n::wx(i18n::StrId::TaskMonitor_Current_Info)
                 << wxString::FromUTF8(snap.current.progressMessage.c_str());
        }
        currentInfo_->SetLabel(line);
    }

    // ── task list: RUNNING first, then children, pending (snapshot order ==
    // dequeue order, do NOT re-sort), then FINISHED records (newest first).
    // Finished rows STAY until the user clears them via the context menu
    // (owner request 2026-11-06: 完成任务不清除只标识状态).
    //
    // Selection/scroll survive the 1s full rebuild: DeleteAllItems destroys
    // both, so remember the selected row's taskId + the first visible item
    // and restore by ID afterwards (spec §3.2 — with 200 finished rows a
    // per-second selection loss is unusable).
    wxString keepSelectedId;
    long keepTop = 0;
    if (selectedRow_ >= 0 && selectedRow_ < static_cast<long>(pendingIds_.size())) {
        keepSelectedId = wxString::FromAscii(
            pendingIds_[static_cast<std::size_t>(selectedRow_)].c_str());
    }
    keepTop = pendingList_->GetTopItem();

    pendingList_->DeleteAllItems();
    selectedRow_ = -1;
    pendingIds_.clear();
    rowIsChild_.clear();
    rowKinds_.clear();
    long row = 0;
    // targetId → subscription remark, resolved at most once per id per refresh
    // (the list refreshes every second — never hit SQLite once per row).
    std::map<std::string, wxString> remarkCache;
    std::function<wxString(const task::TaskHandle&)> targetName =
        [this, &remarkCache](const task::TaskHandle& h) {
        if (h.targetId.empty()) return wxString(u8"-");
        // Auto-task child rows carry their Chinese step name directly in
        // targetId (AppController's step observer) — never a subscription id.
        if (!h.parentTaskId.empty()) return wxString::FromUTF8(h.targetId.c_str());
        std::map<std::string, wxString>::const_iterator it = remarkCache.find(h.targetId);
        if (it != remarkCache.end()) return it->second;
        const std::string remarks = controller_->getSubscriptionRemarks(h.targetId);
        wxString name = wxString::FromUTF8(
            remarks.empty() ? h.targetId.c_str() : remarks.c_str());
        remarkCache[h.targetId] = name;
        return name;
    };
    std::function<void(const task::TaskHandle&, bool, int)> insertRow =
        [this, &row, &targetName](const task::TaskHandle& h, bool indent, int kind) {
        // Child rows (auto-task steps) get a "└" prefix and are monitor-only:
        // they never live in queue_, so cancel-selected stays disabled for
        // them (rowIsChild_).
        wxString idText = wxString::FromAscii(h.taskId.c_str());
        if (indent) idText = wxString::FromUTF8(u8"  └ ") + idText;
        const long idx = pendingList_->InsertItem(row, idText);
        pendingList_->SetItem(idx, 1, wxString::FromUTF8(task::taskTypeToString(h.type)));
        pendingList_->SetItem(idx, 2, targetName(h));
        pendingList_->SetItem(idx, 3, wxString::Format("%d", h.priority));
        // FAILED rows carry an ASCII "!" (owner §4: no owner-draw, the status
        // text alone must separate 成功/失败 at a glance).
        wxString statusText = wxString::FromAscii(statusToString(h.status));
        if (h.status == task::TaskStatus::FAILED) statusText += "!";
        pendingList_->SetItem(idx, 4, statusText);
        // 提交于: local wall-clock HH:MM:SS derived from the steady stamp
        // (owner request 2026-11-04: was "Ns前" relative seconds).
        pendingList_->SetItem(idx, 5, wxString::FromAscii(
            task::wallClockText(h.createdAt, std::chrono::steady_clock::now(),
                                std::chrono::system_clock::now()).c_str()));
        pendingIds_.push_back(h.taskId);   // row index → taskId (see .h note)
        rowIsChild_.push_back(kind == RowChild);
        rowKinds_.push_back(kind);
        ++row;
    };
    if (!snap.current.taskId.empty()) insertRow(snap.current, false, RowCurrent);
    // Auto-task step breakdown, rendered under its running parent
    // (owner request 2026-11-04: 子任务拆解纳入任务监控).
    for (const task::TaskHandle& h : snap.children) insertRow(h, true, RowChild);
    for (const task::TaskHandle& h : snap.pending) insertRow(h, false, RowPending);
    // Finished records, newest first (spec §3.1/§3.2). history_ serves BOTH
    // diagnostics and this list now — recentParent (v1.2.6 single-row
    // retention) is superseded: the finished ledger shows the same row plus
    // its predecessors, and unlike recentParent it survives the next start
    // until the user clears it (契约演进, spec §3.1).
    for (const task::TaskHandle& h : snap.history) {
        if (h.status == task::TaskStatus::COMPLETED ||
            h.status == task::TaskStatus::FAILED ||
            h.status == task::TaskStatus::CANCELLED) {
            insertRow(h, false, RowFinished);
        }
    }
    // Empty list: one non-selectable placeholder row (spec §3.2 空态).
    if (row == 0) {
        const long idx = pendingList_->InsertItem(0, i18n::wx(i18n::StrId::TaskMonitor_Placeholder_None));
        pendingList_->SetItem(idx, 1, wxString());
        pendingIds_.push_back(std::string());
        rowIsChild_.push_back(true);   // monitor-only: cancel never targets it
        rowKinds_.push_back(RowChild);
    }

    // Restore selection + scroll by taskId (rows shifted; index is stale).
    if (!keepSelectedId.empty()) {
        for (std::size_t i = 0; i < pendingIds_.size(); ++i) {
            if (pendingIds_[i] == std::string(keepSelectedId.utf8_str())) {
                selectedRow_ = static_cast<long>(i);
                pendingList_->SetItemState(selectedRow_,
                                           wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                           wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
                break;
            }
        }
    }
    if (keepTop > 0 && keepTop < pendingList_->GetItemCount()) {
        pendingList_->EnsureVisible(keepTop);
    }

    // ── hovered report popup: fixed-order conditional hide ──────────────
    // (1) popupTaskIdBefore was captured before the rebuild; (2) restore
    // hoveredRow_ to the task's new row index (rows shift when history
    // changes), hiding the popup if the task left the list; (3) keep the
    // popup only while the restored row is still RowFinished AND its report
    // still exists — a stationary hover must survive the 1s refresh, and a
    // row that no longer finishes/reports must hide.
    if (!popupTaskIdBefore.empty()) {
        bool found = false;
        for (std::size_t i = 0; i < pendingIds_.size(); ++i) {
            if (pendingIds_[i] == popupTaskIdBefore) {
                hoveredRow_ = static_cast<long>(i);
                found = true;
                break;
            }
        }
        if (!found) hideReportPopup();
    }
    if (!popupTaskId_.empty()) {
        bool keep = false;
        if (hoveredRow_ >= 0 && hoveredRow_ < static_cast<long>(pendingIds_.size()) &&
            rowKinds_.size() == pendingIds_.size() &&
            pendingIds_[static_cast<std::size_t>(hoveredRow_)] == popupTaskId_ &&
            rowKinds_[static_cast<std::size_t>(hoveredRow_)] == RowFinished) {
            task::TaskReportInfo info;
            keep = controller_->findTaskReport(popupTaskId_, info);
        }
        if (!keep) hideReportPopup();
    }

    updateButtonStates(snap);
}

void TaskMonitorPanel::updateButtonStates(const task::TaskQueueSnapshot& snap) {
    cancelCurrentBtn_->Enable(!snap.current.taskId.empty());
    // 取消选中 enables for QUEUED rows only (spec §3.5): child/placeholder
    // rows are monitor-only, Running goes through 终止当前, and Finished rows
    // are terminal — they get 移除 from the context menu, never 取消.
    const bool rowSelected = selectedRow_ >= 0 &&
                             selectedRow_ < static_cast<long>(pendingIds_.size());
    const bool selectedIsPending = rowSelected &&
                                   rowKinds_.size() == pendingIds_.size() &&
                                   rowKinds_[static_cast<std::size_t>(selectedRow_)] == RowPending;
    cancelSelectedBtn_->Enable(selectedIsPending);
    cancelAllBtn_->Enable(snap.queueDepth > 0);
}

// -------------------------------------------------------------------
//  Context menu (spec 2026-11-06 §3.3). One wxEVT_CONTEXT_MENU handler for
//  row + blank-space right-clicks (wxMSW fires ITEM_RIGHT_CLICK for rows AND
//  propagates WM_CONTEXTMENU — binding both would double-popup).
// -------------------------------------------------------------------
void TaskMonitorPanel::onListContextMenu(wxContextMenuEvent& event) {
    wxPoint pt = event.GetPosition();
    if (pt == wxDefaultPosition) {
        // Keyboard context-menu key: anchor the menu near the list centre.
        const wxSize sz = pendingList_->GetClientSize();
        pt = pendingList_->ClientToScreen(wxPoint(sz.GetX() / 2, sz.GetY() / 2));
    }
    // HitTest and wxMSW's PopupMenu both want CLIENT coordinates
    // (wxWindowMSW::PopupMenu ClientToScreens its point internally).
    const wxPoint clientPt = pendingList_->ScreenToClient(pt);
    int hitFlags = 0;
    const long hit = pendingList_->HitTest(clientPt, hitFlags);
    const bool hitRow = hit >= 0 && hit < static_cast<long>(pendingIds_.size());

    // Capture the target NOW: the command event arrives later, by which time
    // the 1s refresh may have re-indexed rows (taskId stays stable though).
    ctxTaskId_.clear();
    ctxRowKind_ = -1;
    if (hitRow) {
        ctxTaskId_ = pendingIds_[static_cast<std::size_t>(hit)];
        ctxRowKind_ = rowKinds_[static_cast<std::size_t>(hit)];
        selectedRow_ = hit;
        pendingList_->SetItemState(hit, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                   wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }

    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
    bool hasFinished = false;
    for (const task::TaskHandle& h : snap.history) {
        if (h.status == task::TaskStatus::COMPLETED ||
            h.status == task::TaskStatus::FAILED ||
            h.status == task::TaskStatus::CANCELLED) { hasFinished = true; break; }
    }

    wxMenu menu;
    if (ctxRowKind_ == RowCurrent) {
        menu.Append(ID_TASK_CTX_TERM_ONE, i18n::wx(i18n::StrId::TaskMonitor_Menu_TerminateCurrent));
    } else if (ctxRowKind_ == RowPending) {
        menu.Append(ID_TASK_CTX_CANCEL_ONE, i18n::wx(i18n::StrId::TaskMonitor_Menu_CancelQueued));
    } else if (ctxRowKind_ == RowFinished) {
        menu.Append(ID_TASK_CTX_REMOVE_ONE, i18n::wx(i18n::StrId::TaskMonitor_Menu_RemoveRecord));
        // task-report-hover Task 6: keyboard-reachable path to the report
        // popup. Only offered when the finished row actually has a published
        // report (findTaskReport gate) — a report-less finished row gets no
        // 查看报告 entry (it is omitted, not disabled).
        task::TaskReportInfo reportInfo;
        if (controller_->findTaskReport(ctxTaskId_, reportInfo)) {
            menu.Append(kCtxViewReport, i18n::wx(i18n::StrId::TaskMonitor_Menu_ViewReport));
        }
    } else {
        // Blank space / child rows: whole-queue actions only.
        wxMenuItem* cancelAllItem = menu.Append(ID_TASK_CTX_CANCEL_ALL,
                                                i18n::wx(i18n::StrId::TaskMonitor_Button_CancelAll));
        cancelAllItem->Enable(snap.queueDepth > 0);
    }
    // Every row (and the blank area) additionally offers the bulk cleanup.
    wxMenuItem* clearItem = menu.Append(ID_TASK_CTX_CLEAR_ALL,
                                        i18n::wx(i18n::StrId::TaskMonitor_Menu_ClearFinished));
    clearItem->Enable(hasFinished);

    PopupMenu(&menu, clientPt);
    // No event.Skip(): the list's default (no menu) must not run as well.
}

void TaskMonitorPanel::onContextCommand(wxCommandEvent& event) {
    const int id = event.GetId();
    if (id == ID_TASK_CTX_CLEAR_ALL) {
        const std::size_t n = controller_->clearFinishedTasks();
        refreshNow();
        wxQueueEvent(wxGetTopLevelParent(this), new StatusUpdateEvent(
            0, i18n::u8f(i18n::StrId::TaskMonitor_Status_ClearedFinished, { std::to_string(n) })));
        return;
    }
    if (id == ID_TASK_CTX_REMOVE_ONE) {
        if (!ctxTaskId_.empty()) controller_->removeFinishedTask(ctxTaskId_);
        refreshNow();
        return;
    }
    if (id == ID_TASK_CTX_CANCEL_ONE) {
        if (!ctxTaskId_.empty()) controller_->cancelTask(ctxTaskId_);
        refreshNow();
        wxQueueEvent(wxGetTopLevelParent(this),
                     new StatusUpdateEvent(0, i18n::u8(i18n::StrId::TaskMonitor_Status_CancelQueued)));
        return;
    }
    if (id == ID_TASK_CTX_CANCEL_ALL) {
        controller_->cancelAllQueuedTasks();
        refreshNow();
        wxQueueEvent(wxGetTopLevelParent(this),
                     new StatusUpdateEvent(0, i18n::u8(i18n::StrId::TaskMonitor_Status_CancelAllQueued)));
        return;
    }
    if (id == ID_TASK_CTX_TERM_ONE) {
        const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
        if (!snap.current.taskId.empty()) {
            controller_->cancelTask(snap.current.taskId);
            wxQueueEvent(wxGetTopLevelParent(this), new StatusUpdateEvent(
                0, i18n::u8f(i18n::StrId::TaskMonitor_Status_CancelCurrent,
                             { task::taskTypeToString(snap.current.type) })));
        }
        refreshNow();
        return;
    }
    if (id == kCtxViewReport) {
        // task-report-hover Task 6: keyboard-reachable report popup. Target is
        // the FOCUSED row (selectedRow_ / ctxTaskId_), not a re-HitTest of the
        // list centre; the taskId is stable across refreshes, so find its
        // current row index and anchor the popup there. No report → no-op.
        if (ctxTaskId_.empty()) return;
        long row = -1;
        for (std::size_t i = 0; i < pendingIds_.size(); ++i) {
            if (pendingIds_[i] == ctxTaskId_) {
                row = static_cast<long>(i);
                break;
            }
        }
        if (row < 0) row = selectedRow_;
        wxPoint pos(0, 0);
        if (row >= 0 && row < static_cast<long>(pendingIds_.size())) {
            wxRect rect;
            if (pendingList_->GetItemRect(row, rect, wxLIST_RECT_BOUNDS)) {
                pos = wxPoint(rect.x, rect.y);
            }
        }
        showReportPopup(ctxTaskId_, pos);
        return;
    }
    event.Skip();
}

std::string TaskMonitorPanel::selectedTaskId() const {
    if (selectedRow_ < 0 || selectedRow_ >= static_cast<long>(pendingIds_.size())) {
        return std::string();
    }
    return pendingIds_[static_cast<size_t>(selectedRow_)];
}

void TaskMonitorPanel::onItemSelected(wxListEvent& event) {
    selectedRow_ = event.GetIndex();
    // Deselection events carry -1; clamp guards against stale indices.
    if (selectedRow_ >= static_cast<long>(pendingIds_.size())) selectedRow_ = -1;
    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
    updateButtonStates(snap);
    event.Skip();
}

void TaskMonitorPanel::onCancelCurrent(wxCommandEvent&) {
    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
    if (snap.current.taskId.empty()) return;
    // Routes through AppController::cancelTask → cancelTest() (the only path
    // that truly stops a batch test, spec §3.5 B3-a).
    controller_->cancelTask(snap.current.taskId);
    wxQueueEvent(wxGetTopLevelParent(this),
                 new StatusUpdateEvent(
                     0, i18n::u8f(i18n::StrId::TaskMonitor_Status_CancelCurrent,
                                  { task::taskTypeToString(snap.current.type) })));
    refreshNow();
}

void TaskMonitorPanel::onCancelSelected(wxCommandEvent&) {
    const std::string id = selectedTaskId();
    if (id.empty()) return;
    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
    std::string label = i18n::u8(i18n::StrId::TaskMonitor_Label_Task);
    if (snap.current.taskId == id) {
        label = task::taskTypeToString(snap.current.type);
    } else {
        for (const task::TaskHandle& h : snap.pending) {
            if (h.taskId == id) {
                label = task::taskTypeToString(h.type);
                break;
            }
        }
    }
    controller_->cancelTask(id);
    // refreshNow() re-enters snapshot(): safe because TaskQueue::cancel fires
    // onComplete OUTSIDE the queue lock (spec §3.4, test T4).
    refreshNow();
    wxQueueEvent(wxGetTopLevelParent(this),
                 new StatusUpdateEvent(0, i18n::u8f(i18n::StrId::TaskMonitor_Status_CancelQueuedOne, { label })));
}

void TaskMonitorPanel::onCancelAll(wxCommandEvent&) {
    controller_->cancelAllQueuedTasks();
    refreshNow();
    wxQueueEvent(wxGetTopLevelParent(this),
                 new StatusUpdateEvent(0, i18n::u8(i18n::StrId::TaskMonitor_Status_CancelAllQueued)));
}

// -------------------------------------------------------------------
//  Hover report popup (task-report-hover Task 6). A finished row shows a
//  wxPopupTransientWindow (wxPU_CONTAINS_CONTROLS) with the task's report;
//  the popup is dismissed on mouse leave / non-finished row / missing report.
// -------------------------------------------------------------------
void TaskMonitorPanel::onListMotion(wxMouseEvent& event) {
    const wxPoint pos = event.GetPosition();
    int hitFlags = 0;
    const long hit = pendingList_->HitTest(pos, hitFlags);
    const bool hitRow = hit >= 0 && hit < static_cast<long>(pendingIds_.size());
    if (!hitRow || rowKinds_[static_cast<std::size_t>(hit)] != RowFinished) {
        // Blank space / non-finished row: hide any open report popup.
        suppressHoverId_.clear();  // the pointer left the dismissed row
        hideReportPopup();
        event.Skip();
        return;
    }
    const std::string& rowId = pendingIds_[static_cast<std::size_t>(hit)];
    // A click-away dismissal is sticky for the row it dismissed: hiding the
    // popup makes Windows synthesise a WM_MOUSEMOVE over the row, and without
    // this guard the popup would re-open a few milliseconds after the user
    // closed it (observed as the report popup refusing to close). Normal
    // hover resumes once the pointer reaches another row or leaves the list.
    if (!suppressHoverId_.empty()) {
        if (rowId == suppressHoverId_) {
            event.Skip();
            return;
        }
        suppressHoverId_.clear();
    }
    task::TaskReportInfo info;
    if (!controller_->findTaskReport(rowId, info)) {
        // Finished row without a published report: nothing to show.
        hideReportPopup();
        event.Skip();
        return;
    }
    // Debounce: moving within the same row must not reposition/refresh the
    // popup; switching rows (or re-showing after an external dismiss) does.
    if (hit != hoveredRow_ || reportPopup_ == nullptr || !reportPopup_->IsShown()
        || popupTaskId_ != rowId) {
        hoveredRow_ = hit;  // the popup now tracks this row (debounce + refresh)
        showReportPopup(rowId, pos);
    }
    event.Skip();
}

void TaskMonitorPanel::onPanelMouseDown(wxMouseEvent& event) {
    // task-report-hover Task 6 (bugfix): explicit click-away dismissal for the
    // hover report popup. The popup suppresses wx's native transient dismissal
    // (WM_ACTIVATE -> DismissOnDeactivate) by design, so once shown the close
    // relies on the mouse-leave chain; a left-down that lands OUTSIDE the
    // popup rect must therefore dismiss it explicitly. Clicks inside the popup
    // (read-only report text area) are not dismissals.
    if (reportPopup_ != nullptr && reportPopup_->IsShown()) {
        wxWindow* evtWin = static_cast<wxWindow*>(event.GetEventObject());
        const wxPoint ptScreen =
            (evtWin != nullptr) ? evtWin->ClientToScreen(event.GetPosition())
                                : ClientToScreen(event.GetPosition());
        const wxRect popupRect = reportPopup_->GetScreenRect();
        const bool inside = ptScreen.x >= popupRect.GetLeft() &&
                            ptScreen.x <= popupRect.GetRight() &&
                            ptScreen.y >= popupRect.GetTop() &&
                            ptScreen.y <= popupRect.GetBottom();
        if (!inside) {
            // Capture the dismissed row BEFORE hideReportPopup() clears
            // popupTaskId_, so a spurious motion over the same row does not
            // immediately re-open the popup.
            suppressHoverId_ = popupTaskId_;
            hideReportPopup();
        }
    }
    event.Skip();
}

void TaskMonitorPanel::onListLeave(wxMouseEvent& event) {
    // The list fires a spurious wxEVT_LEAVE_WINDOW as a side effect of the
    // popup being shown (the topmost popup re-evaluates the window under the
    // cursor, so IsMouseInWindow()/WindowFromPoint no longer reports the list
    // even though the cursor never moved off the row). Decide from the actual
    // cursor point against the screen rects instead — that is deterministic
    // and matches what the user perceives: the popup stays while the cursor
    // is over the list or over the popup, and only closes once it has truly
    // left both (moving onto the popup or staying on the row keeps it).
    POINT cp{};
    const bool haveCursor = ::GetCursorPos(&cp) != FALSE;
    RECT lr{};
    const bool haveListRect =
        pendingList_ != nullptr &&
        ::GetWindowRect(static_cast<HWND>(pendingList_->GetHWND()), &lr) != FALSE;
    RECT prr{};
    const bool havePopupRect =
        reportPopup_ != nullptr &&
        ::GetWindowRect(static_cast<HWND>(reportPopup_->GetHWND()), &prr) != FALSE;
    const bool cursorOverList =
        haveCursor && haveListRect && ::IsWindowVisible(
            static_cast<HWND>(pendingList_->GetHWND())) != FALSE &&
        ::PtInRect(&lr, cp) != FALSE;
    const bool cursorOverPopup =
        haveCursor && havePopupRect &&
        ::IsWindowVisible(static_cast<HWND>(reportPopup_->GetHWND())) != FALSE &&
        ::PtInRect(&prr, cp) != FALSE;
    if (cursorOverList || cursorOverPopup) {
        event.Skip();
        return;
    }
    suppressHoverId_.clear();  // pointer truly left the list/popup: hover resumes
    hideReportPopup();
    event.Skip();
}

void TaskMonitorPanel::showReportPopup(const std::string& taskId, const wxPoint& pos) {
    // Guard: no published report → no popup (and clear any stale state).
    task::TaskReportInfo info;
    if (!controller_->findTaskReport(taskId, info)) {
        hideReportPopup();
        return;
    }
    // Lazy-create the transient popup with a read-only multiline text ctrl.
    // wxPU_CONTAINS_CONTROLS (NOT wxPU_NO_FOCUS, which does not exist in
    // wxWidgets 3.3.3) lets the inner control take focus / scroll; the
    // transient window has no mouse-leave dismissal for this style, so the
    // popup is bound to the same leave handler as the list.
    if (reportPopup_ == nullptr) {
        reportPopup_ = new ReportPopupWindow(this);
        reportPopupText_ = new wxTextCtrl(reportPopup_, wxID_ANY, wxString(),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxTE_MULTILINE | wxTE_READONLY);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(reportPopupText_, 1, wxEXPAND);
        reportPopup_->SetSizer(sizer);
        reportPopup_->Bind(wxEVT_LEAVE_WINDOW, &TaskMonitorPanel::onListLeave, this);
    }
    // Identification for UI automation: the popup is located by TITLE
    // ("任务报告 <taskId>"). wxWindow::SetLabel maps to SetWindowText on MSW;
    // SetTitle is wxTopLevelWindow-only and a popup is not one, so SetLabel is
    // the correct (and only) title path.
    reportPopup_->SetLabel(i18n::wxf(i18n::StrId::TaskMonitor_Popup_Title,
                                     { wxString::FromUTF8(taskId.c_str()) }));
    // UTF-8 → wxString so the report text (Chinese log lines) is never decoded
    // with the ANSI code page.
    const wxString content = wxString::FromUTF8(info.content.c_str());
    reportPopupText_->SetValue(content);
    popupTaskId_ = taskId;

    // Size adapts to the content (2026-11-26): measured with the text ctrl's
    // font, padded, then clamped to [min, work-area percentage]. Overlong
    // reports keep the work-area cap and scroll inside the text ctrl.
    const wxPoint screenPt = pendingList_->ClientToScreen(pos);
    wxRect wa;
    const int displayNum = wxDisplay::GetFromPoint(screenPt);
    if (displayNum != wxNOT_FOUND) {
        wa = wxDisplay(static_cast<unsigned>(displayNum)).GetClientArea();
    } else {
        wa = wxDisplay(0u).GetClientArea();
    }
    const wxSize textSize = measureReportText(reportPopupText_, content);
    int width = textSize.GetWidth() + kPopupPaddingX;
    int height = textSize.GetHeight() + kPopupPaddingY;
    const int maxWidth = wa.GetWidth() * kPopupMaxWorkAreaPercentX / 100;
    const int maxHeight = wa.GetHeight() * kPopupMaxWorkAreaPercentY / 100;
    if (width > maxWidth) width = maxWidth;
    if (height > maxHeight) height = maxHeight;
    if (width < kPopupMinWidth) width = kPopupMinWidth;
    if (height < kPopupMinHeight) height = kPopupMinHeight;
    if (width > wa.GetWidth()) width = wa.GetWidth();
    if (height > wa.GetHeight()) height = wa.GetHeight();
    reportPopup_->SetSize(width, height);

    // Position: anchor at the trigger point + offset, clamped into the work
    // area. SetSize BEFORE Position so GetSize() drives the clamp (Position
    // itself only knows the display geometry, not the work area).
    wxPoint pt(screenPt.x + kPopupAnchorOffset, screenPt.y + kPopupAnchorOffset);
    const wxSize sz = reportPopup_->GetSize();
    const int maxX = wa.GetRight() - sz.GetWidth();
    const int maxY = wa.GetBottom() - sz.GetHeight();
    if (pt.x > maxX) pt.x = maxX;
    if (pt.y > maxY) pt.y = maxY;
    if (pt.x < wa.GetLeft()) pt.x = wa.GetLeft();
    if (pt.y < wa.GetTop()) pt.y = wa.GetTop();
    // Position takes ptOrigin + size; size is an ANCHOR OFFSET, NOT the popup
    // size — wxSize(0,0) puts the top-left at pt (cursor+8) so the popup opens
    // below/right of the anchor, with clamping driven by GetSize().
    reportPopup_->Position(pt, wxSize(0, 0));
    reportPopup_->Popup();
}

void TaskMonitorPanel::hideReportPopup() {
    if (reportPopup_ != nullptr && reportPopup_->IsShown()) {
        reportPopup_->Dismiss();
    }
    popupTaskId_.clear();
    // hoveredRow_ is intentionally NOT reset here: the next motion re-shows
    // the popup for the same row via the IsShown() debounce check, and the
    // per-second refresh re-validates it by scanning pendingIds_.
}
