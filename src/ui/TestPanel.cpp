#include "TestPanel.h" 
#include "TestLogMediator.h" 
#include "Events.h" 

#include <wx/sizer.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h> 

#include "Logger.h" 

// Event table bindings
wxBEGIN_EVENT_TABLE(TestPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, TestPanel::onCancel)
wxEND_EVENT_TABLE()

enum {
    ID_TEST_CANCEL = wxID_HIGHEST + 300,
};

// -------------------------------------------------------------------
// TestPanel — proxy testing progress, results, and embedded log
// -------------------------------------------------------------------
TestPanel::TestPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY),
      logCtrl_(nullptr)
{
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // ── Progress section ──────────────────────────────────────────
    wxBoxSizer* progressSizer = new wxBoxSizer(wxHORIZONTAL);

    statusLabel_ = new wxStaticText(this, wxID_ANY, "Ready");
    progressSizer->Add(statusLabel_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    progressBar_ = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(200, 20));
    progressSizer->Add(progressBar_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    progressText_ = new wxStaticText(this, wxID_ANY, "0/0");
    progressSizer->Add(progressText_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    cancelBtn_ = new wxButton(this, ID_TEST_CANCEL, "Cancel");
    cancelBtn_->Enable(false);
    progressSizer->Add(cancelBtn_, 0, wxALIGN_CENTER_VERTICAL);

    topSizer->Add(progressSizer, 0, wxEXPAND | wxALL, 4);

    // ── Results list ──────────────────────────────────────────────
    resultList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SUNKEN);
    resultList_->AppendColumn("Remarks", wxLIST_FORMAT_LEFT, 150);
    resultList_->AppendColumn("Address", wxLIST_FORMAT_LEFT, 150);
    resultList_->AppendColumn("Delay", wxLIST_FORMAT_RIGHT, 80);
    resultList_->AppendColumn("Message", wxLIST_FORMAT_LEFT, 250);

    topSizer->Add(resultList_, 1, wxEXPAND | wxALL, 4);

    // ── Log text control ──────────────────────────────────────────
    // Review fix (P1-3): Logger callback runs on worker thread;
    //   use wxQueueEvent → onLogMessage() runs on main thread.
    // Review fix (P3-8): single-style (no per-level colours).
    wxFont monoFont(9, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    logCtrl_ = new wxTextCtrl(this, wxID_ANY, "",
                               wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH);
    logCtrl_->SetFont(monoFont);
    logCtrl_->SetBackgroundColour(wxColour(30, 30, 30));   // dark background, console-like
    topSizer->Add(logCtrl_, 0, wxEXPAND | wxALL, 2);       // fixed prop = 0, takes remaining height

    // ── Event bindings ────────────────────────────────────────────
    Bind(wxEVT_PROXY_TEST_PROGRESS, &TestPanel::onProgress, this);
    Bind(wxEVT_LOG_MESSAGE, &TestPanel::onLogMessage, this);

    // ── Logger callback ──────────────────────────────────────────
    // pushCallback() (TD-1): registers this panel's handler and makes it the active callback.
    //   A previous holder (e.g. LogPanel) stays on the stack and is restored on popCallback().
    // The lambda only does wxQueueEvent; onLogMessage() runs on the main thread (P1-3 fix).
    Logger::pushCallback([this](const std::string& msg, LogLevel /*level*/) {
        if (this) {
            wxQueueEvent(this, new LogMessageEvent(msg, LogLevel::INFO));
        }
    });

    SetSizer(topSizer);
}

// dtor pops our callback, restoring whatever was active before pushCallback()
TestPanel::~TestPanel() {
    Logger::popCallback();
}

// -------------------------------------------------------------------
//  TestPanel API
// -------------------------------------------------------------------
void TestPanel::startTest(const std::string& subId) {
    isRunning_ = true;
    total_ = 0;
    progressBar_->SetValue(0);
    progressText_->SetLabel("0/0");
    statusLabel_->SetLabel("Testing: " + subId);
    resultList_->DeleteAllItems();
    resetLog();                  // ← clear previous log lines
    cancelBtn_->Enable(true);
}

// -------------------------------------------------------------------
//  Progress & results
// -------------------------------------------------------------------
void TestPanel::onProgress(ProxyTestProgressEvent& event) {
    // Handle completion state FIRST (but don't return early — need to add result row)
    if (event.isCompleted()) {
        isRunning_ = false;
        cancelBtn_->Enable(false);
        statusLabel_->SetLabel("Test completed");
    }

    // Add result row if we have proxy data (even for completion events)
    if (!event.getProxyId().empty()) {
        long row = resultList_->InsertItem(resultList_->GetItemCount(),
                                           event.getRemarks());
        resultList_->SetItem(row, 1, event.getProxyId());
        resultList_->SetItem(row, 2, event.getDelay());
        resultList_->SetItem(row, 3, event.getMessage());
    }

    // Update progress UI for non-completion events
    if (!event.isCompleted()) {
        int current = event.getCurrent();
        int total = event.getTotal();
        if (total > 0) {
            total_ = total;
            progressBar_->SetRange(total);
            progressBar_->SetValue(current);
            progressText_->SetLabel(wxString::Format("%d/%d", current, total));
        }
    }
}

// -------------------------------------------------------------------
//  Cancel
// -------------------------------------------------------------------
void TestPanel::cancelTest() {
    isRunning_ = false;
    cancelBtn_->Enable(false);
    statusLabel_->SetLabel("Cancelled");
}

void TestPanel::onCancel(wxCommandEvent&) {
    cancelTest();                   // calls AppController::cancelTest() via controller
}

// -------------------------------------------------------------------
//  Log message from Logger callback (P1-3: runs on main thread)
// -------------------------------------------------------------------
void TestPanel::onLogMessage(LogMessageEvent& event) {
    appendLogLine(event.getMessage());
}

/**
  * appendLogLine — called on main thread from onLogMessage()
  *
  * P1-2 fix: row-limit removal uses Remove(), not Clear().
  *   When line count exceeds MAX_LOG_LINES + 100,
  *   the oldest 100 line positions are removed from position 0,
  *   preserving the most recent MAX_LOG_LINES visible.
  */
void TestPanel::appendLogLine(const std::string& line) {
    // Runs on main thread (marshalled by wxQueueEvent in callback lambda)
    logCtrl_->AppendText(line + "\n");

    // Row-limit crop: once we exceed CROP_THRESHOLD lines, trim from top.
    // CROP_THRESHOLD is MAX_LOG_LINES (500) + 100 lines buffer before we need to crop.
    // The oldest 100 lines are removed per-trim, keeping recent lines stable.
    int lineCount = logCtrl_->GetNumberOfLines();
    if (lineCount > MAX_LOG_LINES + 100) {
        // Keep the most recent MAX_LOG_LINES lines, discard the rest
        wxTextPos startOfKeep = logCtrl_->XYToPosition(0, lineCount - MAX_LOG_LINES);
        wxTextPos endOfDiscard = logCtrl_->GetLastPosition();
        if (startOfKeep < endOfDiscard) {
            logCtrl_->Remove(startOfKeep, endOfDiscard);
        }
    }

    logCtrl_->ShowPosition(logCtrl_->GetLastPosition());
}

void TestPanel::resetLog() {
    logCtrl_->Clear();
}
