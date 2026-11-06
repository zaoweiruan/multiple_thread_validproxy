#ifndef UI_BATCH_QUEUE_PANEL_H
#define UI_BATCH_QUEUE_PANEL_H

#include <wx/wx.h>

#include "AppController.h"   // ProxyBatchTester::BatchQueueStats

class wxStaticText;
class wxGauge;

// ---------------------------------------------------------------
// BatchQueuePanel — live progress of the running ProxyBatchTester:
// status line, progress gauge (range 1000 = one decimal), percent text and
// the dual-queue detail line (spec 2026-11-03 v1.2 §3.10).
// The pending-proxy list was removed per owner request 2026-11-04; the
// backend API (getPendingBatchProxies) stays for CLI/diagnostic use.
//
// Refresh model: NO own timer; MainFrame's bottomViewTimer_ drives
// refreshNow() while this page is visible. Data comes from
// AppController::getBatchQueueStats() (non-const, UI thread only).
// ---------------------------------------------------------------
class BatchQueuePanel : public wxPanel {
public:
    BatchQueuePanel(wxWindow* parent, AppController* controller);

    void setActive(bool active) { active_ = active; }
    bool isActive() const { return active_; }

    void refreshNow();

private:
    AppController* controller_;
    bool active_{false};

    wxStaticText* statusText_{nullptr};
    wxGauge* gauge_{nullptr};
    wxStaticText* percentText_{nullptr};
    wxStaticText* queueDetail_{nullptr};

    // Anti-flicker (spec §3.10): right after a batch ends the controller stops
    // exposing the tester (stats drop to all-zero), so keep the last non-empty
    // frame visible for two refreshes before switching to the idle line.
    int endingGrace_ = 0;
    ProxyBatchTester::BatchQueueStats lastStats_;
};

#endif // UI_BATCH_QUEUE_PANEL_H
