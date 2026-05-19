#include "TestPanel.h"
#include "Events.h"

#include <wx/sizer.h>
#include <wx/gauge.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>

// Event table bindings (non-custom events only)
wxBEGIN_EVENT_TABLE(TestPanel, wxPanel)
    EVT_BUTTON(wxID_ANY, TestPanel::onCancel)
wxEND_EVENT_TABLE()

enum {
    ID_TEST_CANCEL = wxID_HIGHEST + 300,
};

// -------------------------------------------------------------------
TestPanel::TestPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    // Progress section
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

    // Results list
    resultList_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                  wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SUNKEN);
    resultList_->AppendColumn("Remarks", wxLIST_FORMAT_LEFT, 150);
    resultList_->AppendColumn("Address", wxLIST_FORMAT_LEFT, 150);
    resultList_->AppendColumn("Delay", wxLIST_FORMAT_RIGHT, 80);
    resultList_->AppendColumn("Message", wxLIST_FORMAT_LEFT, 250);

    topSizer->Add(resultList_, 1, wxEXPAND | wxALL, 4);

    // Bind custom events
    Bind(wxEVT_PROXY_TEST_PROGRESS, &TestPanel::onProgress, this);

    SetSizer(topSizer);
}

void TestPanel::startTest(const std::string& subId) {
    isRunning_ = true;
    total_ = 0;
    progressBar_->SetValue(0);
    progressText_->SetLabel("0/0");
    statusLabel_->SetLabel("Testing: " + subId);
    resultList_->DeleteAllItems();
    cancelBtn_->Enable(true);
}

void TestPanel::cancelTest() {
    isRunning_ = false;
    cancelBtn_->Enable(false);
    statusLabel_->SetLabel("Cancelled");
}

void TestPanel::onProgress(ProxyTestProgressEvent& event) {
    // Handle completion state FIRST (but don't return early - need to add result row)
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

void TestPanel::onCancel(wxCommandEvent&) {
    cancelTest();
}
