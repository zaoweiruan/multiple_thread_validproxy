#include "AddPoolMemberDialog.h"
#include "AppController.h"

#include <wx/msgdlg.h>
#include <wx/intl.h>

AddPoolMemberDialog::AddPoolMemberDialog(wxWindow* parent, AppController* controller)
    : wxDialog(parent, wxID_ANY, "选择代理", wxDefaultPosition, wxSize(580, 440)),
      controller_(controller) {
    candidates_ = controller_->getPoolCandidateProfiles(500);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxStaticText* hint = new wxStaticText(this, wxID_ANY,
        wxString::Format("选择要加入代理池的代理（共 %d 个候选）：",
                         static_cast<int>(candidates_.size())));
    root->Add(hint, 0, wxALL, 8);

    search_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    search_->SetHint("按 IndexId / 地址 / 备注 过滤");
    root->Add(search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    search_->Bind(wxEVT_TEXT, &AddPoolMemberDialog::onSearch, this);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
    list_->InsertColumn(0, "IndexId", wxLIST_FORMAT_LEFT, 220);
    list_->InsertColumn(1, "类型",    wxLIST_FORMAT_LEFT, 80);
    list_->InsertColumn(2, "地址",    wxLIST_FORMAT_LEFT, 130);
    list_->InsertColumn(3, "备注",    wxLIST_FORMAT_LEFT, 140);
    root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
    wxButton* okBtn = new wxButton(this, wxID_OK, "加入");
    wxButton* cancelBtn = new wxButton(this, wxID_CANCEL, "取消");
    btnRow->Add(okBtn, 0, wxALL, 4);
    btnRow->Add(cancelBtn, 0, wxALL, 4);
    root->Add(btnRow, 0, wxALIGN_RIGHT | wxALL, 8);

    okBtn->Bind(wxEVT_BUTTON, &AddPoolMemberDialog::onOK, this);

    SetSizerAndFit(root);
    buildList(L"");
}

void AddPoolMemberDialog::buildList(const std::wstring& filter) {
    list_->DeleteAllItems();
    rowIndexIds_.clear();

    std::wstring f = filter;
    for (wchar_t& c : f) c = static_cast<wchar_t>(wxTolower(c));

    for (const auto& p : candidates_) {
        std::wstring idx   = wxString(p.indexid.c_str(), wxConvUTF8).ToStdWstring();
        std::wstring addr = wxString(p.address.c_str(), wxConvUTF8).ToStdWstring();
        std::wstring rem  = wxString(p.remarks.c_str(), wxConvUTF8).ToStdWstring();
        if (!f.empty()) {
            std::wstring hay = idx + L" " + addr + L" " + rem;
            for (wchar_t& c : hay) c = static_cast<wchar_t>(wxTolower(c));
            if (hay.find(f) == std::wstring::npos) continue;
        }
        long row = list_->InsertItem(list_->GetItemCount(), idx);
        list_->SetItem(row, 1, wxString(p.configtype.c_str(), wxConvUTF8));
        list_->SetItem(row, 2, wxString(p.address.c_str(), wxConvUTF8));
        list_->SetItem(row, 3, wxString(p.remarks.c_str(), wxConvUTF8));
        rowIndexIds_.push_back(p.indexid);
    }
}

void AddPoolMemberDialog::onSearch(wxCommandEvent& event) {
    (void)event;
    buildList(search_->GetValue().ToStdWstring());
}

void AddPoolMemberDialog::onOK(wxCommandEvent& event) {
    selectedIndexIds_.clear();
    long item = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    while (item != -1) {
        if (static_cast<size_t>(item) < rowIndexIds_.size()) {
            selectedIndexIds_.push_back(rowIndexIds_[static_cast<size_t>(item)]);
        }
        item = list_->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    }
    if (selectedIndexIds_.empty()) {
        wxMessageBox("请至少选择一个代理。", "选择代理", wxOK | wxICON_INFORMATION);
        return;
    }
    EndModal(wxID_OK);
}
