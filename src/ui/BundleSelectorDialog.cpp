#include "BundleSelectorDialog.h"
#include "config/ComponentBundleDAO.h"
#include "config/ComponentBundle.h"
#include <string>

// ============================================================
// BundleSelectorDialog: 选择要加入 BundleChain 的组件包
// ============================================================

wxBEGIN_EVENT_TABLE(BundleSelectorDialog, wxDialog)
    EVT_BUTTON(wxID_OK, BundleSelectorDialog::onOk)
    EVT_LIST_ITEM_ACTIVATED(wxID_ANY, BundleSelectorDialog::onDoubleClick)
wxEND_EVENT_TABLE()

BundleSelectorDialog::BundleSelectorDialog(wxWindow* parent, sqlite3* db,
                                           config::CoreType coreType)
    : wxDialog(parent, wxID_ANY, wxT("选择组件包"), wxDefaultPosition, wxSize(520, 380)),
      db_(db), coreType_(coreType), selectedId_() {

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->InsertColumn(0, wxT("Bundle ID"), wxLIST_FORMAT_LEFT, 200);
    list_->InsertColumn(1, wxT("名称"), wxLIST_FORMAT_LEFT, 160);
    list_->InsertColumn(2, wxT("版本"), wxLIST_FORMAT_LEFT, 60);
    list_->InsertColumn(3, wxT("核心"), wxLIST_FORMAT_LEFT, 70);

    config::ComponentBundleDAO dao(db_);
    const std::vector<config::ComponentBundle> bundles = dao.getAllEnabled();
    long row = 0;
    for (const config::ComponentBundle& b : bundles) {
        // CoreType 适配（阶段 8.4）：仅显示与当前模板核心一致的 Bundle
        if (b.coreType != coreType_) {
            continue;
        }
        list_->InsertItem(row, wxString(b.id));
        list_->SetItem(row, 1, wxString(b.name));
        list_->SetItem(row, 2, wxString(std::to_string(b.version)));
        list_->SetItem(row, 3, b.coreType == config::CoreType::XRAY ? wxT("Xray") : wxT("SingBox"));
        ++row;
    }

    wxButton* okBtn = new wxButton(this, wxID_OK, wxT("确定"));
    wxButton* cancelBtn = new wxButton(this, wxID_CANCEL, wxT("取消"));

    wxBoxSizer* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->AddStretchSpacer(1);
    btnSizer->Add(okBtn, 0, wxRIGHT, 6);
    btnSizer->Add(cancelBtn, 0, 0, 0);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);
    topSizer->Add(list_, 1, wxEXPAND | wxALL, 6);
    topSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 6);
    SetSizer(topSizer);

    if (!bundles.empty()) {
        list_->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    }
}

std::string BundleSelectorDialog::SelectBundle(wxWindow* parent, sqlite3* db,
                                               config::CoreType coreType) {
    BundleSelectorDialog dlg(parent, db, coreType);
    if (dlg.ShowModal() == wxID_OK) {
        return dlg.getSelectedId();
    }
    return std::string();
}

void BundleSelectorDialog::onOk(wxCommandEvent& event) {
    (void)event;
    long sel = list_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel >= 0) {
        selectedId_ = std::string(list_->GetItemText(sel, 0).ToUTF8());
    }
    EndModal(selectedId_.empty() ? wxID_CANCEL : wxID_OK);
}

void BundleSelectorDialog::onDoubleClick(wxListEvent& event) {
    long sel = event.GetIndex();
    if (sel >= 0) {
        selectedId_ = std::string(list_->GetItemText(sel, 0).ToUTF8());
    }
    EndModal(selectedId_.empty() ? wxID_CANCEL : wxID_OK);
}