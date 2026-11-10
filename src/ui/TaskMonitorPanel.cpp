#include "TaskMonitorPanel.h"

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
                                    wxString::FromUTF8(u8"当前无运行任务"));
    root->Add(currentInfo_, 0, wxEXPAND | wxALL, 4);

    pendingList_ = new wxListCtrl(this, ID_TASK_PENDING_LIST, wxDefaultPosition,
                                  wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    pendingList_->InsertColumn(0, wxString::FromUTF8(u8"任务ID"), wxLIST_FORMAT_LEFT, 150);
    pendingList_->InsertColumn(1, wxString::FromUTF8(u8"类型"), wxLIST_FORMAT_LEFT, 160);
    pendingList_->InsertColumn(2, wxString::FromUTF8(u8"名称"), wxLIST_FORMAT_LEFT, 140);
    pendingList_->InsertColumn(3, wxString::FromUTF8(u8"优先级"), wxLIST_FORMAT_RIGHT, 60);
    pendingList_->InsertColumn(4, wxString::FromUTF8(u8"状态"), wxLIST_FORMAT_LEFT, 90);
    pendingList_->InsertColumn(5, wxString::FromUTF8(u8"提交于"), wxLIST_FORMAT_RIGHT, 90);
    root->Add(pendingList_, 1, wxEXPAND | wxLEFT | wxRIGHT, 4);

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    cancelCurrentBtn_ = new wxButton(this, ID_TASK_CANCEL_CURRENT,
                                     wxString::FromUTF8(u8"终止当前"));
    cancelSelectedBtn_ = new wxButton(this, ID_TASK_CANCEL_SELECTED,
                                      wxString::FromUTF8(u8"取消选中"));
    cancelAllBtn_ = new wxButton(this, ID_TASK_CANCEL_ALL,
                                 wxString::FromUTF8(u8"取消全部排队"));
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
    // Context menu (spec 2026-11-06 §3.3): wxEVT_CONTEXT_MENU handles both row
    // and blank-space right-clicks with one entry point (on wxMSW, row right-
    // clicks fire ITEM_RIGHT_CLICK *and* propagate WM_CONTEXTMENU, so binding
    // both events would double-popup the menu).
    pendingList_->Bind(wxEVT_CONTEXT_MENU, &TaskMonitorPanel::onListContextMenu, this);
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

    updateButtonStates(task::TaskQueueSnapshot());   // start fully disabled
}

void TaskMonitorPanel::refreshNow() {
    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();

    // ── current task line ──
    const bool idle = snap.current.taskId.empty();
    if (idle) {
        currentInfo_->SetLabel(wxString::FromUTF8(u8"当前无运行任务"));
    } else {
        wxString line = wxString::FromUTF8(task::taskTypeToString(snap.current.type));
        if (!snap.current.targetId.empty()) {
            line << wxString::FromUTF8(u8" [") << wxString::FromAscii(snap.current.targetId.c_str())
                 << wxString::FromUTF8(u8"]");
        }
        line << wxString::FromUTF8(u8"　状态：") << wxString::FromAscii(statusToString(snap.current.status));
        if (snap.current.progressTotal > 0) {
            const double pct = snap.current.progressCurrent * 100.0 / snap.current.progressTotal;
            line << wxString::FromUTF8(u8"　进度：")
                 << wxString::Format("%d/%d (%.1f%%)", snap.current.progressCurrent,
                                     snap.current.progressTotal, pct);
        }
        line << wxString::FromUTF8(u8"　已运行 ") << elapsedText(snap.current.startedAt);
        if (!snap.current.progressMessage.empty()) {
            line << wxString::FromUTF8(u8"　信息：")
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
        const long idx = pendingList_->InsertItem(0, wxString::FromUTF8(u8"（无任务）"));
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
        menu.Append(ID_TASK_CTX_TERM_ONE, wxString::FromUTF8(u8"终止当前任务"));
    } else if (ctxRowKind_ == RowPending) {
        menu.Append(ID_TASK_CTX_CANCEL_ONE, wxString::FromUTF8(u8"取消此排队任务"));
    } else if (ctxRowKind_ == RowFinished) {
        menu.Append(ID_TASK_CTX_REMOVE_ONE, wxString::FromUTF8(u8"移除此记录"));
    } else {
        // Blank space / child rows: whole-queue actions only.
        wxMenuItem* cancelAllItem = menu.Append(ID_TASK_CTX_CANCEL_ALL,
                                                wxString::FromUTF8(u8"取消全部排队"));
        cancelAllItem->Enable(snap.queueDepth > 0);
    }
    // Every row (and the blank area) additionally offers the bulk cleanup.
    wxMenuItem* clearItem = menu.Append(ID_TASK_CTX_CLEAR_ALL,
                                        wxString::FromUTF8(u8"清理全部完成记录"));
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
            0, "已清理 " + std::to_string(n) + " 条完成记录"));
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
                     new StatusUpdateEvent(0, "已取消排队任务"));
        return;
    }
    if (id == ID_TASK_CTX_CANCEL_ALL) {
        controller_->cancelAllQueuedTasks();
        refreshNow();
        wxQueueEvent(wxGetTopLevelParent(this),
                     new StatusUpdateEvent(0, "已取消全部排队任务"));
        return;
    }
    if (id == ID_TASK_CTX_TERM_ONE) {
        const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
        if (!snap.current.taskId.empty()) {
            controller_->cancelTask(snap.current.taskId);
            wxQueueEvent(wxGetTopLevelParent(this), new StatusUpdateEvent(
                0, std::string("已取消当前任务: ") + task::taskTypeToString(snap.current.type)));
        }
        refreshNow();
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
                 new StatusUpdateEvent(0, std::string("已取消当前任务: ") +
                                            task::taskTypeToString(snap.current.type)));
    refreshNow();
}

void TaskMonitorPanel::onCancelSelected(wxCommandEvent&) {
    const std::string id = selectedTaskId();
    if (id.empty()) return;
    const task::TaskQueueSnapshot snap = controller_->getTaskQueueSnapshot();
    std::string label = "任务";
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
                 new StatusUpdateEvent(0, "已取消排队任务: " + label));
}

void TaskMonitorPanel::onCancelAll(wxCommandEvent&) {
    controller_->cancelAllQueuedTasks();
    refreshNow();
    wxQueueEvent(wxGetTopLevelParent(this),
                 new StatusUpdateEvent(0, "已取消全部排队任务"));
}
