#include "BatchQueuePanel.h"
#include "i18n/I18nWx.h"

#include <wx/gauge.h>

#include "AppController.h"
#include "UiControlIds.h"

BatchQueuePanel::BatchQueuePanel(wxWindow* parent, AppController* controller)
    : wxPanel(parent, ID_BATCH_QUEUE_PANEL), controller_(controller) {
    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    statusText_ = new wxStaticText(this, ID_BATCH_STATUS_TEXT,
                                   i18n::wx(i18n::StrId::BatchQueue_Status_Idle));
    wxFont statusFont = statusText_->GetFont();
    statusFont.SetPointSize(statusFont.GetPointSize() + 1);
    statusFont.SetWeight(wxFONTWEIGHT_BOLD);
    statusText_->SetFont(statusFont);
    root->Add(statusText_, 0, wxALL, 6);

    wxBoxSizer* progressRow = new wxBoxSizer(wxHORIZONTAL);
    gauge_ = new wxGauge(this, ID_BATCH_GAUGE, 1000, wxDefaultPosition, wxSize(300, -1));
    percentText_ = new wxStaticText(this, ID_BATCH_PERCENT_TEXT, wxString("0/0 (0.0%)"));
    progressRow->Add(gauge_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    progressRow->Add(percentText_, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(progressRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    queueDetail_ = new wxStaticText(this, ID_BATCH_QUEUE_DETAIL,
                                    i18n::wx(i18n::StrId::BatchQueue_Detail_Zero));
    root->Add(queueDetail_, 0, wxALL, 6);

    // The pending-proxy list (ID_BATCH_PENDING_COUNT / ID_BATCH_PENDING_LIST)
    // was removed per owner request 2026-11-04 — the queue page shows summary
    // numbers only. IDs stay reserved in UiControlIds.h (no reuse).

    SetSizer(root);
}

void BatchQueuePanel::refreshNow() {
    const ProxyBatchTester::BatchQueueStats s = controller_->getBatchQueueStats();

    if (s.running || s.processed > 0) {
        // Live or just-finished batch: show the numbers.
        endingGrace_ = 2;
        lastStats_ = s;
    } else if (endingGrace_ > 0) {
        // Controller stopped exposing the tester (pointer cleared the moment
        // run() returned): keep the last frame for two refreshes so the user
        // sees the final result instead of an instant flip to idle.
        --endingGrace_;
    } else {
        statusText_->SetLabel(i18n::wx(i18n::StrId::BatchQueue_Status_Idle));
        gauge_->SetValue(0);
        percentText_->SetLabel(wxString("0/0 (0.0%)"));
        queueDetail_->SetLabel(i18n::wx(i18n::StrId::BatchQueue_Detail_Zero));
        return;
    }

    const ProxyBatchTester::BatchQueueStats& view = lastStats_;
    if (view.updating) {
        // Update task shares the gauge; "已结束" once the last subscription is
        // done (the panel then keeps this final frame for endingGrace_ ticks).
        statusText_->SetLabel((view.running ? i18n::wx(i18n::StrId::BatchQueue_Status_Updating) : i18n::wx(i18n::StrId::BatchQueue_Status_UpdateFinished)));
    } else {
        statusText_->SetLabel((view.running ? i18n::wx(i18n::StrId::BatchQueue_Status_Running) : i18n::wx(i18n::StrId::BatchQueue_Status_Finished)));
    }
    gauge_->SetValue(static_cast<int>(view.percent * 10.0));   // range 1000 = 1 decimal
    percentText_->SetLabel(wxString::Format("%d/%d (%.1f%%)", view.processed, view.total, view.percent));
    if (view.updating) {
        // Same gauge, update-task wording (owner 2026-11-06: 更新订阅复用队列
        // 进度条，只显示整体进度).
        queueDetail_->SetLabel(i18n::wxf(i18n::StrId::BatchQueue_Detail_Updating,
                  { wxString::Format("%d", view.remaining),
                    wxString::Format("%d", view.success),
                    wxString::Format("%d", view.failed) }));
        return;
    }
    queueDetail_->SetLabel(i18n::wxf(i18n::StrId::BatchQueue_Detail_Full,
                      { wxString::Format("%d", view.batchQueueSize),
                        wxString::Format("%d", view.insertedQueueSize),
                        wxString::Format("%d", view.success),
                        wxString::Format("%d", view.failed) }));
}
