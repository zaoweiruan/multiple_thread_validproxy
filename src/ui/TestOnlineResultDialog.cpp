#include "TestOnlineResultDialog.h"
#include "Events.h"

#include <wx/msgdlg.h>
#include <wx/intl.h>

TestOnlineResultDialog::TestOnlineResultDialog(wxWindow* parent,
                                               const std::vector<std::string>& failedIndexIds,
                                               int total, int success, int failed)
    : wxDialog(parent, wxID_ANY, "在线代理测试结果",
               wxDefaultPosition, wxSize(620, 480)) {
    rowIndexIds_ = failedIndexIds;

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxString summary = wxString::Format(
        L"在线代理测试完成：共 %d，成功 %d，失败 %d。",
        total, success, failed);
    wxStaticText* text = new wxStaticText(this, wxID_ANY, summary);
    root->Add(text, 0, wxALL, 8);

    wxString failedHint;
    if (failed > 0) {
        failedHint.Printf(L"失败代理（%d 个，单击可定位）：", failed);
    } else {
        failedHint = L"失败代理：";
    }
    wxStaticText* hint = new wxStaticText(this, wxID_ANY, failedHint);
    root->Add(hint, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_HRULES | wxLC_VRULES | wxLC_SINGLE_SEL);
    list_->InsertColumn(0, "IndexId", wxLIST_FORMAT_LEFT, 420);
    root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    wxButton* closeBtn = new wxButton(this, wxID_OK, "关闭");
    root->Add(closeBtn, 0, wxALIGN_RIGHT | wxALL, 8);

    closeBtn->Bind(wxEVT_BUTTON, &TestOnlineResultDialog::onClose, this);
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &TestOnlineResultDialog::onItemSelected, this);

    SetSizerAndFit(root);
    buildList();
}

void TestOnlineResultDialog::buildList() {
    list_->DeleteAllItems();
    // rowIndexIds_ was set in the constructor; if any concurrent removal
    // happened, rowIndexIds_ and the control stay consistent here.
    for (size_t i = 0; i < rowIndexIds_.size(); ++i) {
        long row = list_->InsertItem(static_cast<long>(i),
                                     wxString(rowIndexIds_[i].c_str(), wxConvUTF8));
        (void)row;
    }
}

void TestOnlineResultDialog::onItemSelected(wxListEvent& event) {
    long item = event.GetIndex();
    if (item < 0 || static_cast<size_t>(item) >= rowIndexIds_.size()) {
        return;
    }
    const std::string& indexId = rowIndexIds_[static_cast<size_t>(item)];
    // Locate the proxy in the parent frame (Subscription + Proxy List panels),
    // then close this dialog.
    // NOTE: post to the top-level parent resolved from GetParent() (the panel),
    // NOT from `this` — a wxDialog is itself a top-level window, so
    // wxGetTopLevelParent(this) would return this dialog and the event would
    // be discarded with no handler.
    wxQueueEvent(wxGetTopLevelParent(GetParent()), new LocateProxyEvent(indexId));
    EndModal(wxID_OK);
}

void TestOnlineResultDialog::onClose(wxCommandEvent& event) {
    (void)event;
    EndModal(wxID_OK);
}