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

    // ── task list: RUNNING first, then pending (snapshot order == dequeue
    // order, do NOT re-sort). The running task shares the list so one table
    // answers "what is the queue doing right now" (owner request 2026-11-04).
    pendingList_->DeleteAllItems();
    selectedRow_ = -1;
    pendingIds_.clear();
    rowIsChild_.clear();
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
    std::function<void(const task::TaskHandle&, bool, bool)> insertRow =
        [this, &row, &targetName](const task::TaskHandle& h, bool indent, bool monitorOnly) {
        // Child rows (auto-task steps) get a "└" prefix and are monitor-only:
        // they never live in queue_, so cancel-selected stays disabled for
        // them (rowIsChild_).
        wxString idText = wxString::FromAscii(h.taskId.c_str());
        if (indent) idText = wxString::FromUTF8(u8"  └ ") + idText;
        const long idx = pendingList_->InsertItem(row, idText);
        pendingList_->SetItem(idx, 1, wxString::FromUTF8(task::taskTypeToString(h.type)));
        pendingList_->SetItem(idx, 2, targetName(h));
        pendingList_->SetItem(idx, 3, wxString::Format("%d", h.priority));
        pendingList_->SetItem(idx, 4, wxString::FromAscii(statusToString(h.status)));
        // 提交于: local wall-clock HH:MM:SS derived from the steady stamp
        // (owner request 2026-11-04: was "Ns前" relative seconds).
        pendingList_->SetItem(idx, 5, wxString::FromAscii(
            task::wallClockText(h.createdAt, std::chrono::steady_clock::now(),
                                std::chrono::system_clock::now()).c_str()));
        pendingIds_.push_back(h.taskId);   // row index → taskId (see .h note)
        rowIsChild_.push_back(!h.parentTaskId.empty() || monitorOnly);
        ++row;
    };
    if (!snap.current.taskId.empty()) insertRow(snap.current, false, false);
    // Auto-task step breakdown, rendered under its running parent
    // (owner request 2026-11-04: 子任务拆解纳入任务监控).
    for (const task::TaskHandle& h : snap.children) insertRow(h, true, false);
    // Finished-task outcome: while idle, keep the last parent + its breakdown
    // visible until the next task starts (owner feedback 2026-11-05). The
    // rows are terminal — monitor-only so cancel cannot target them.
    if (idle && !snap.recentParent.taskId.empty()) {
        insertRow(snap.recentParent, false, true);
        for (const task::TaskHandle& h : snap.recentChildren) insertRow(h, true, false);
    }
    for (const task::TaskHandle& h : snap.pending) insertRow(h, false, false);

    updateButtonStates(snap);
}

void TaskMonitorPanel::updateButtonStates(const task::TaskQueueSnapshot& snap) {
    cancelCurrentBtn_->Enable(!snap.current.taskId.empty());
    // Child (auto-task step) rows are monitor-only: cancelling one means
    // cancelling the whole AUTO_TASK, which is what 终止当前 already does.
    const bool rowSelected = selectedRow_ >= 0 &&
                             selectedRow_ < static_cast<long>(pendingIds_.size());
    const bool selectedIsChild = rowSelected && rowIsChild_.size() == pendingIds_.size() &&
                                 rowIsChild_[static_cast<std::size_t>(selectedRow_)];
    cancelSelectedBtn_->Enable(rowSelected && !selectedIsChild);
    cancelAllBtn_->Enable(snap.queueDepth > 0);
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
