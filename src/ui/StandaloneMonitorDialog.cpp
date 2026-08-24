#include "StandaloneMonitorDialog.h"
#include "AppController.h"
#include "Events.h"

#include <wx/sizer.h>
#include <wx/button.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------
// StandaloneMonitorDialog implementation
// ---------------------------------------------------------------
namespace {

enum ColumnId {
    COL_INDEX_ID = 0,
    COL_HOST,
    COL_STARTED_AT,
    COL_RUNTIME_MIN,
    COL_SOCKS_PORT,
    COL_PID
};

} // namespace

wxBEGIN_EVENT_TABLE(StandaloneMonitorDialog, wxDialog)
    EVT_TIMER(wxID_ANY, StandaloneMonitorDialog::onRefreshTimer)
    EVT_BUTTON(wxID_CLOSE, StandaloneMonitorDialog::onCloseButton)
    EVT_CLOSE(StandaloneMonitorDialog::onCloseWindow)
    EVT_LIST_ITEM_ACTIVATED(wxID_ANY, StandaloneMonitorDialog::onItemActivated)
wxEND_EVENT_TABLE()

StandaloneMonitorDialog::StandaloneMonitorDialog(wxWindow* parent,
                                                 AppController* controller)
    : wxDialog(parent, wxID_ANY, L"独立代理监控", wxDefaultPosition,
               wxSize(860, 360), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      controller_(controller),
      list_(nullptr),
      timer_(this) {
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->InsertColumn(COL_INDEX_ID, L"索引ID", wxLIST_FORMAT_LEFT, 220);
    list_->InsertColumn(COL_HOST, L"Host", wxLIST_FORMAT_LEFT, 140);
    list_->InsertColumn(COL_STARTED_AT, L"起始时间", wxLIST_FORMAT_LEFT, 150);
    list_->InsertColumn(COL_RUNTIME_MIN, L"运行时长(分)", wxLIST_FORMAT_RIGHT, 100);
    list_->InsertColumn(COL_SOCKS_PORT, L"监听端口", wxLIST_FORMAT_RIGHT, 90);
    list_->InsertColumn(COL_PID, L"PID", wxLIST_FORMAT_RIGHT, 90);
    topSizer->Add(list_, 1, wxEXPAND | wxALL, 8);

    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    // 修复(2026-08-21): 水平 sizer 中 wxALIGN_RIGHT 为非法水平对齐标志,
    // 触发 wxBoxSizer::DoInsert 的 wxFAIL_MSG (见 docs/bugfix/2026-08-21-Bugfix-StandaloneMonitorDialog-v1.0.md)。
    // 整行右对齐由下方 topSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | ...) 实现。
    buttonSizer->Add(new wxButton(this, wxID_CLOSE, L"关闭"));
    topSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    SetSizer(topSizer);
    CentreOnScreen();
}

bool StandaloneMonitorDialog::Show(bool show) {
    const bool ret = wxDialog::Show(show);
    if (show) {
        refreshRows();
        if (!timer_.IsRunning()) {
            timer_.Start(2000, wxTIMER_CONTINUOUS);
        }
    } else if (timer_.IsRunning()) {
        timer_.Stop();
    }
    return ret;
}

void StandaloneMonitorDialog::onRefreshTimer(wxTimerEvent& event) {
    refreshRows();
}

void StandaloneMonitorDialog::onCloseButton(wxCommandEvent& event) {
    Show(false);
}

void StandaloneMonitorDialog::onCloseWindow(wxCloseEvent& event) {
    // Hide instead of destroy: the dialog is reused by MainFrame.
    Show(false);
    event.Veto();
}

void StandaloneMonitorDialog::onItemActivated(wxListEvent& event) {
    const long row = event.GetIndex();
    if (row < 0 || !controller_) {
        return;
    }
    // COL_INDEX_ID (column 0) holds the proxy's indexId verbatim.
    const wxString indexId = list_->GetItemText(row, COL_INDEX_ID);
    if (indexId.IsEmpty()) {
        return;
    }
    // Hand off to MainFrame (the dialog's parent), which owns ProxyListPanel
    // and will select + scroll the proxy into view.  The dialog is hidden,
    // not destroyed, so posting to the parent window is safe.
    wxQueueEvent(GetParent(), new LocateProxyEvent(indexId.ToStdString()));
    Show(false);
}

void StandaloneMonitorDialog::refreshRows() {
    if (!controller_) {
        return;
    }
    list_->DeleteAllItems();

    std::vector<StandaloneMonitorRow> rows =
        controller_->getWatchedStandaloneMonitors();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const long index = list_->InsertItem(
            static_cast<long>(i), wxString(rows[i].indexId));

        list_->SetItem(index, COL_HOST,
                       rows[i].host.empty()
                           ? wxString(L"-")
                           : wxString(rows[i].host));

        list_->SetItem(index, COL_STARTED_AT,
                       rows[i].startedAt.empty()
                           ? wxString(L"-")
                           : wxString(rows[i].startedAt));

        const double minutes =
            static_cast<double>(rows[i].durationMs) / 60000.0;
        list_->SetItem(index, COL_RUNTIME_MIN,
                       wxString::Format(L"%.1f", minutes));

        list_->SetItem(index, COL_SOCKS_PORT,
                       rows[i].socksPort > 0
                           ? wxString(std::to_string(rows[i].socksPort))
                           : wxString(L"-"));

        list_->SetItem(index, COL_PID,
                       rows[i].pid >= 0
                           ? wxString(std::to_string(rows[i].pid))
                           : wxString(L"-"));
    }
}
