#include "AddPoolMemberDialog.h"
#include "AppController.h"
#include "Utils.h"

#include <wx/msgdlg.h>
#include <wx/intl.h>

#include <algorithm>

namespace {

enum {
    COL_INDEX_ID = 0,
    COL_PROTOCOL,
    COL_ADDRESS,
    COL_DELAY,
    COL_REGION,
    COL_HEALTH,
    COL_MESSAGE,
    COL_REMARKS,
    COL_COUNT
};

pool_candidate::CandidateSortKey sortKeyForColumn(int col) {
    switch (col) {
        case COL_INDEX_ID: return pool_candidate::CandidateSortKey::IndexId;
        case COL_PROTOCOL: return pool_candidate::CandidateSortKey::Protocol;
        case COL_ADDRESS:  return pool_candidate::CandidateSortKey::Address;
        case COL_DELAY:    return pool_candidate::CandidateSortKey::Delay;
        case COL_REGION:   return pool_candidate::CandidateSortKey::Region;
        case COL_HEALTH:   return pool_candidate::CandidateSortKey::Health;
        case COL_MESSAGE:  return pool_candidate::CandidateSortKey::Message;
        case COL_REMARKS:  return pool_candidate::CandidateSortKey::Remarks;
    }
    return pool_candidate::CandidateSortKey::IndexId;
}

} // namespace

AddPoolMemberDialog::AddPoolMemberDialog(wxWindow* parent, AppController* controller)
    : wxDialog(parent, wxID_ANY, "选择代理", wxDefaultPosition, wxSize(900, 440)),
      controller_(controller) {
    candidates_ = controller_->getPoolCandidateProfiles(500);

    wxBoxSizer* root = new wxBoxSizer(wxVERTICAL);

    wxStaticText* hint = new wxStaticText(this, wxID_ANY,
        wxString::Format("选择要加入代理池的有效代理（共 %d 个候选）：",
                         static_cast<int>(candidates_.size())));
    root->Add(hint, 0, wxALL, 8);

    search_ = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    search_->SetHint("按 IndexId / 地址 / 备注 过滤");
    root->Add(search_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    search_->Bind(wxEVT_TEXT, &AddPoolMemberDialog::onSearch, this);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
    list_->InsertColumn(COL_INDEX_ID, "IndexId", wxLIST_FORMAT_LEFT, 170);
    list_->InsertColumn(COL_PROTOCOL, "协议",    wxLIST_FORMAT_LEFT, 80);
    list_->InsertColumn(COL_ADDRESS,  "地址",    wxLIST_FORMAT_LEFT, 120);
    list_->InsertColumn(COL_DELAY,    "时延",    wxLIST_FORMAT_LEFT, 70);
    list_->InsertColumn(COL_REGION,   "Region",  wxLIST_FORMAT_LEFT, 80);
    list_->InsertColumn(COL_HEALTH,   "健康度",  wxLIST_FORMAT_LEFT, 70);
    list_->InsertColumn(COL_MESSAGE,  "Message", wxLIST_FORMAT_LEFT, 130);
    list_->InsertColumn(COL_REMARKS,  "备注",    wxLIST_FORMAT_LEFT, 120);
    root->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);
    list_->Bind(wxEVT_LIST_COL_CLICK, &AddPoolMemberDialog::onColumnClick, this);

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

    // Sort a copy of the candidates by the current column/direction.
    std::vector<pool_candidate::PoolCandidateItem> rows = candidates_;
    std::stable_sort(rows.begin(), rows.end(),
        [this](const pool_candidate::PoolCandidateItem& a,
               const pool_candidate::PoolCandidateItem& b) {
            return pool_candidate::compareCandidates(a, b, sortColumn_, ascending_);
        });

    for (const auto& p : rows) {
        std::wstring idx   = wxString(p.indexid.c_str(), wxConvUTF8).ToStdWstring();
        std::wstring addr = wxString(p.address.c_str(), wxConvUTF8).ToStdWstring();
        std::wstring rem  = wxString(p.remarks.c_str(), wxConvUTF8).ToStdWstring();
        if (!f.empty()) {
            std::wstring hay = idx + L" " + addr + L" " + rem;
            for (wchar_t& c : hay) c = static_cast<wchar_t>(wxTolower(c));
            if (hay.find(f) == std::wstring::npos) continue;
        }
        long row = list_->InsertItem(list_->GetItemCount(), idx);
        list_->SetItem(row, COL_PROTOCOL, wxString(utils::getProtocolName(p.configtype).c_str(), wxConvUTF8));
        list_->SetItem(row, COL_ADDRESS,  wxString(p.address.c_str(), wxConvUTF8));
        list_->SetItem(row, COL_DELAY,    p.delay.empty() ? "-" : wxString(p.delay.c_str(), wxConvUTF8));
        list_->SetItem(row, COL_REGION,   wxString(p.region.c_str(), wxConvUTF8));
        list_->SetItem(row, COL_HEALTH,
            wxString::Format("%.3f", pool_candidate::computeHealth(p.start_count, p.crash_count)));
        list_->SetItem(row, COL_MESSAGE,  wxString(p.message.c_str(), wxConvUTF8));
        list_->SetItem(row, COL_REMARKS,  wxString(p.remarks.c_str(), wxConvUTF8));
        rowIndexIds_.push_back(p.indexid);
    }
}

void AddPoolMemberDialog::onSearch(wxCommandEvent& event) {
    (void)event;
    buildList(search_->GetValue().ToStdWstring());
}

void AddPoolMemberDialog::onColumnClick(wxListEvent& event) {
    const int col = event.GetColumn();
    if (col < 0 || col >= COL_COUNT) return;
    const pool_candidate::CandidateSortKey key = sortKeyForColumn(col);
    if (key == sortColumn_) {
        ascending_ = !ascending_;
    } else {
        sortColumn_ = key;
        ascending_ = true;
    }
    buildList(search_->GetValue().ToStdWstring());
}

void AddPoolMemberDialog::onOK(wxCommandEvent& event) {
    (void)event;
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