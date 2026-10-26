#include "SqlQueryManageDialog.h"

#include <wx/button.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/font.h>

#include <algorithm>
#include <vector>

namespace {

enum {
    ID_SQL_CHOOSE = wxID_HIGHEST + 901,
    ID_SQL_CLOSE = wxID_HIGHEST + 902
};

std::string trimText(const std::string& value) {
    const std::string whitespace = " \t\r\n";
    const std::size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return std::string();
    }
    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::string toUtf8(const wxString& text) {
    const wxScopedCharBuffer buffer = text.ToUTF8();
    const char* data = buffer.data();
    if (!data) {
        return std::string();
    }
    return std::string(data);
}

wxString fromUtf8(const std::string& text) {
    return wxString::FromUTF8(text.c_str());
}

wxString scopeDisplayName(const std::string& scope) {
    if (scope == "by_subid") {
        return L"按 SubId";
    }
    return L"通用";
}

int scopeChoiceIndex(const std::string& scope) {
    return scope == "by_subid" ? 1 : 0;
}

} // namespace

SqlQueryManageDialog::SqlQueryManageDialog(wxWindow* parent, sqlite3* db,
                                           const std::string& requiredScope,
                                           int currentId)
    : wxDialog(parent, wxID_ANY, L"SQL 查询方案库",
               wxDefaultPosition, wxSize(1080, 780),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , db_(db)
    , requiredScope_()
    , currentId_(currentId)
{
    if (requiredScope == "all" || requiredScope == "by_subid") {
        requiredScope_ = requiredScope;
    }

    buildUi();
    bindEvents();

    if (db_ && !db::SqlQueryDAO::createTableIfNotExists(db_)) {
        wxMessageBox(L"SQL 方案表初始化失败，请检查数据库连接。",
                     L"SQL 方案库", wxOK | wxICON_ERROR, this);
    }

    std::string initialFilter = requiredScope_;
    if (currentId_ > 0 && db_) {
        const db::SqlQueryItem current = db::SqlQueryDAO::getById(db_, currentId_);
        if (current.id > 0 && current.scope != initialFilter) {
            initialFilter.clear();
        }
    }
    if (initialFilter.empty()) {
        filterChoice_->SetSelection(0);
    } else if (initialFilter == "by_subid") {
        filterChoice_->SetSelection(2);
    } else {
        filterChoice_->SetSelection(1);
    }

    reloadList(currentId_);
    CentreOnParent();
}

int SqlQueryManageDialog::SelectSqlQuery(wxWindow* parent, sqlite3* db,
                                         const std::string& requiredScope,
                                         int currentId) {
    SqlQueryManageDialog dialog(parent, db, requiredScope, currentId);
    if (dialog.ShowModal() == wxID_OK) {
        return dialog.getSelectedId();
    }
    return 0;
}

void SqlQueryManageDialog::buildUi() {
    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* hint = new wxStaticText(
        this, wxID_ANY,
        L"管理 SQL 查询方案：新增 / 编辑 / 保存 / 删除 / 设为默认；"
        L"双击列表中的方案或点击「选择此方案」返回配置。");
    topSizer->Add(hint, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    wxBoxSizer* filterSizer = new wxBoxSizer(wxHORIZONTAL);
    filterSizer->Add(new wxStaticText(this, wxID_ANY, L"范围过滤:"),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    filterChoice_ = new wxChoice(this, wxID_ANY);
    filterChoice_->Append(L"全部范围");
    filterChoice_->Append(L"通用 (all)");
    filterChoice_->Append(L"按 SubId (by_subid)");
    filterChoice_->SetSelection(0);
    filterSizer->Add(filterChoice_, 0, wxALIGN_CENTER_VERTICAL);
    topSizer->Add(filterSizer, 0, wxLEFT | wxRIGHT | wxTOP, 10);

    // 列表在上、编辑区在下：两者都占满横向空间，便于查看和编辑较长 SQL。
    wxBoxSizer* bodySizer = new wxBoxSizer(wxVERTICAL);

    list_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxLC_REPORT | wxLC_SINGLE_SEL);
    list_->InsertColumn(0, L"ID", wxLIST_FORMAT_LEFT, 55);
    list_->InsertColumn(1, L"名称", wxLIST_FORMAT_LEFT, 190);
    list_->InsertColumn(2, L"范围", wxLIST_FORMAT_CENTER, 90);
    list_->InsertColumn(3, L"默认", wxLIST_FORMAT_CENTER, 55);
    list_->InsertColumn(4, L"描述", wxLIST_FORMAT_LEFT, 260);
    list_->SetMinSize(wxSize(-1, 190));
    bodySizer->Add(list_, 2, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

    wxBoxSizer* editorSizer = new wxBoxSizer(wxVERTICAL);

    wxFlexGridSizer* formSizer = new wxFlexGridSizer(2, 8, 10);
    formSizer->AddGrowableCol(1, 1);
    formSizer->AddGrowableRow(4, 1);

    formSizer->Add(new wxStaticText(this, wxID_ANY, L"名称:"),
                   0, wxALIGN_CENTER_VERTICAL);
    nameText_ = new wxTextCtrl(this, wxID_ANY);
    formSizer->Add(nameText_, 1, wxEXPAND);

    formSizer->Add(new wxStaticText(this, wxID_ANY, L"作用范围:"),
                   0, wxALIGN_CENTER_VERTICAL);
    scopeChoice_ = new wxChoice(this, wxID_ANY);
    scopeChoice_->Append(L"通用 (all)");
    scopeChoice_->Append(L"按 SubId (by_subid)");
    scopeChoice_->SetSelection(0);
    formSizer->Add(scopeChoice_, 0, wxALIGN_CENTER_VERTICAL);

    formSizer->Add(new wxStaticText(this, wxID_ANY, L"默认方案:"),
                   0, wxALIGN_CENTER_VERTICAL);
    defaultCheck_ = new wxCheckBox(this, wxID_ANY, L"同范围内默认使用此方案");
    formSizer->Add(defaultCheck_, 0, wxALIGN_CENTER_VERTICAL);

    formSizer->Add(new wxStaticText(this, wxID_ANY, L"描述:"),
                   0, wxALIGN_CENTER_VERTICAL);
    descriptionText_ = new wxTextCtrl(this, wxID_ANY);
    formSizer->Add(descriptionText_, 1, wxEXPAND);

    formSizer->Add(new wxStaticText(this, wxID_ANY, L"SQL 语句:"),
                   0, wxALIGN_TOP);
    sqlText_ = new wxTextCtrl(this, wxID_ANY, L"", wxDefaultPosition,
                              wxDefaultSize,
                              wxTE_MULTILINE | wxTE_RICH2 | wxTE_WORDWRAP);
    sqlText_->SetFont(wxFont(10, wxFONTFAMILY_MODERN, wxFONTSTYLE_NORMAL,
                             wxFONTWEIGHT_NORMAL));
    sqlText_->SetMinSize(wxSize(-1, 220));
    formSizer->Add(sqlText_, 1, wxEXPAND);

    editorSizer->Add(formSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

    statusText_ = new wxStaticText(this, wxID_ANY, L"新方案（尚未保存）");
    editorSizer->Add(statusText_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);

    wxBoxSizer* actionSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* newBtn = new wxButton(this, wxID_ANY, L"新增");
    wxButton* saveBtn = new wxButton(this, wxID_ANY, L"保存");
    wxButton* deleteBtn = new wxButton(this, wxID_ANY, L"删除");
    wxButton* defaultBtn = new wxButton(this, wxID_ANY, L"设为默认");
    wxButton* refreshBtn = new wxButton(this, wxID_ANY, L"刷新");
    newBtn->Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onNew, this);
    saveBtn->Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onSave, this);
    deleteBtn->Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onDelete, this);
    defaultBtn->Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onSetDefault, this);
    refreshBtn->Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onRefresh, this);
    actionSizer->Add(newBtn, 0, wxRIGHT, 6);
    actionSizer->Add(saveBtn, 0, wxRIGHT, 6);
    actionSizer->Add(deleteBtn, 0, wxRIGHT, 6);
    actionSizer->Add(defaultBtn, 0, wxRIGHT, 6);
    actionSizer->Add(refreshBtn, 0, 0);
    editorSizer->Add(actionSizer, 0, wxEXPAND | wxALL, 6);

    bodySizer->Add(editorSizer, 3, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 10);
    topSizer->Add(bodySizer, 1, wxEXPAND);

    wxBoxSizer* bottomSizer = new wxBoxSizer(wxHORIZONTAL);
    bottomSizer->AddStretchSpacer(1);
    wxButton* chooseBtn = new wxButton(this, ID_SQL_CHOOSE, L"选择此方案");
    wxButton* closeBtn = new wxButton(this, ID_SQL_CLOSE, L"关闭");
    bottomSizer->Add(chooseBtn, 0, wxRIGHT, 6);
    bottomSizer->Add(closeBtn, 0, 0);
    topSizer->Add(bottomSizer, 0, wxEXPAND | wxALL, 10);

    SetSizer(topSizer);
    SetEscapeId(ID_SQL_CLOSE);
    SetMinSize(wxSize(900, 680));
}

void SqlQueryManageDialog::bindEvents() {
    filterChoice_->Bind(wxEVT_CHOICE, &SqlQueryManageDialog::onFilterChanged, this);
    list_->Bind(wxEVT_LIST_ITEM_SELECTED, &SqlQueryManageDialog::onListSelected, this);
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, &SqlQueryManageDialog::onListActivated, this);
    Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onChoose, this, ID_SQL_CHOOSE);
    Bind(wxEVT_BUTTON, &SqlQueryManageDialog::onClose, this, ID_SQL_CLOSE);
}

void SqlQueryManageDialog::reloadList(int selectId) {
    if (!list_) {
        return;
    }

    loading_ = true;
    list_->Freeze();
    list_->DeleteAllItems();

    std::vector<db::SqlQueryItem> items;
    if (db_) {
        items = db::SqlQueryDAO::getAll(db_, currentFilterScope());
    }

    long row = 0;
    int firstId = 0;
    int defaultId = 0;
    for (const db::SqlQueryItem& item : items) {
        list_->InsertItem(row, wxString::Format(L"%d", item.id));
        list_->SetItem(row, 1, fromUtf8(item.name));
        list_->SetItem(row, 2, scopeDisplayName(item.scope));
        list_->SetItem(row, 3, item.is_default ? L"是" : L"");
        list_->SetItem(row, 4, fromUtf8(item.description));
        list_->SetItemData(row, static_cast<long>(item.id));
        if (firstId == 0) {
            firstId = item.id;
        }
        if (item.is_default) {
            defaultId = item.id;
        }
        ++row;
    }

    list_->Thaw();

    int targetId = selectId;
    if (targetId <= 0) {
        targetId = currentId_;
    }

    bool targetFound = false;
    if (targetId > 0) {
        const int count = list_->GetItemCount();
        for (int i = 0; i < count; ++i) {
            if (static_cast<int>(list_->GetItemData(i)) == targetId) {
                targetFound = true;
                break;
            }
        }
    }
    if (!targetFound) {
        targetId = defaultId > 0 ? defaultId : firstId;
    }

    if (targetId > 0) {
        selectRowById(targetId);
        reloadEditor(targetId);
    } else {
        clearEditor(requiredScope_);
    }

    loading_ = false;
}

void SqlQueryManageDialog::reloadEditor(int id) {
    if (!db_ || id <= 0) {
        clearEditor(currentFilterScope());
        return;
    }

    const db::SqlQueryItem item = db::SqlQueryDAO::getById(db_, id);
    if (item.id <= 0) {
        clearEditor(currentFilterScope());
        return;
    }

    currentId_ = item.id;
    nameText_->SetValue(fromUtf8(item.name));
    descriptionText_->SetValue(fromUtf8(item.description));
    sqlText_->SetValue(fromUtf8(item.sql_text));
    scopeChoice_->SetSelection(scopeChoiceIndex(item.scope));
    defaultCheck_->SetValue(item.is_default);

    wxString status = wxString::Format(L"ID: %d", item.id);
    if (!item.created_at.empty()) {
        status += L"    创建: " + fromUtf8(item.created_at);
    }
    if (!item.updated_at.empty()) {
        status += L"    更新: " + fromUtf8(item.updated_at);
    }
    statusText_->SetLabel(status);
}

void SqlQueryManageDialog::clearEditor(const std::string& preferredScope) {
    currentId_ = 0;
    nameText_->Clear();
    descriptionText_->Clear();
    sqlText_->Clear();
    const std::string scope = preferredScope == "by_subid" ? "by_subid" : "all";
    scopeChoice_->SetSelection(scopeChoiceIndex(scope));
    defaultCheck_->SetValue(false);
    statusText_->SetLabel(L"新方案（尚未保存）");
}

void SqlQueryManageDialog::selectRowById(int id) {
    const int count = list_->GetItemCount();
    for (int i = 0; i < count; ++i) {
        if (static_cast<int>(list_->GetItemData(i)) == id) {
            list_->SetItemState(i,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
            list_->EnsureVisible(i);
            return;
        }
    }
}

void SqlQueryManageDialog::clearListSelection() {
    const long selected = list_->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
    if (selected >= 0) {
        list_->SetItemState(selected, 0,
                            wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    }
}

int SqlQueryManageDialog::selectedRowId() const {
    const long selected = list_->GetNextItem(-1, wxLIST_NEXT_ALL,
                                             wxLIST_STATE_SELECTED);
    if (selected < 0) {
        return 0;
    }
    return static_cast<int>(list_->GetItemData(selected));
}

std::string SqlQueryManageDialog::currentFilterScope() const {
    if (!filterChoice_) {
        return std::string();
    }
    const int selection = filterChoice_->GetSelection();
    if (selection == 1) {
        return "all";
    }
    if (selection == 2) {
        return "by_subid";
    }
    return std::string();
}

bool SqlQueryManageDialog::currentFilterAccepts(const std::string& scope) const {
    const std::string filter = currentFilterScope();
    return filter.empty() || filter == scope;
}

bool SqlQueryManageDialog::collectEditor(db::SqlQueryItem& item) {
    item.id = currentId_;
    item.name = trimText(toUtf8(nameText_->GetValue()));
    item.description = trimText(toUtf8(descriptionText_->GetValue()));
    item.sql_text = trimText(toUtf8(sqlText_->GetValue()));
    item.scope = scopeChoice_->GetSelection() == 1 ? "by_subid" : "all";
    item.is_default = defaultCheck_->GetValue();

    if (item.name.empty()) {
        wxMessageBox(L"请输入方案名称。", L"SQL 方案库",
                     wxOK | wxICON_WARNING, this);
        nameText_->SetFocus();
        return false;
    }
    if (item.sql_text.empty()) {
        wxMessageBox(L"请输入 SQL 语句。", L"SQL 方案库",
                     wxOK | wxICON_WARNING, this);
        sqlText_->SetFocus();
        return false;
    }
    return true;
}

void SqlQueryManageDialog::onNew(wxCommandEvent&) {
    loading_ = true;
    clearListSelection();
    loading_ = false;

    std::string preferred = requiredScope_;
    if (preferred.empty()) {
        preferred = currentFilterScope();
    }
    clearEditor(preferred);
    nameText_->SetFocus();
}

void SqlQueryManageDialog::onSave(wxCommandEvent&) {
    if (!db_) {
        wxMessageBox(L"数据库未连接，无法保存。", L"SQL 方案库",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    db::SqlQueryItem item;
    if (!collectEditor(item)) {
        return;
    }

    const std::vector<db::SqlQueryItem> allItems = db::SqlQueryDAO::getAll(db_, "");
    for (const db::SqlQueryItem& other : allItems) {
        if (other.id != currentId_ && other.name == item.name) {
            wxMessageBox(L"已存在同名 SQL 方案，请换一个名称。", L"SQL 方案库",
                         wxOK | wxICON_WARNING, this);
            nameText_->SetFocus();
            return;
        }
    }

    db::SqlQueryDAO dao(db_);
    int savedId = currentId_;
    if (currentId_ > 0) {
        if (!dao.update(item)) {
            wxMessageBox(L"保存 SQL 方案失败。", L"SQL 方案库",
                         wxOK | wxICON_ERROR, this);
            return;
        }
    } else {
        savedId = dao.insert(item);
        if (savedId <= 0) {
            wxMessageBox(L"新增 SQL 方案失败（名称可能重复）。", L"SQL 方案库",
                         wxOK | wxICON_ERROR, this);
            return;
        }
    }

    if (item.is_default && !db::SqlQueryDAO::setDefault(db_, savedId, item.scope)) {
        wxMessageBox(L"方案已保存，但设置为默认失败。", L"SQL 方案库",
                     wxOK | wxICON_WARNING, this);
    }

    // 若当前过滤条件看不到刚保存的方案，自动切回全部范围。
    if (!currentFilterAccepts(item.scope)) {
        filterChoice_->SetSelection(0);
    }

    reloadList(savedId);
    statusText_->SetLabel(L"已保存。 " + statusText_->GetLabel());
}

void SqlQueryManageDialog::onDelete(wxCommandEvent&) {
    if (!db_ || currentId_ <= 0) {
        wxMessageBox(L"请先选择或保存一个 SQL 方案。", L"SQL 方案库",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    const db::SqlQueryItem item = db::SqlQueryDAO::getById(db_, currentId_);
    if (item.id <= 0) {
        wxMessageBox(L"方案不存在，列表将刷新。", L"SQL 方案库",
                     wxOK | wxICON_WARNING, this);
        reloadList(0);
        return;
    }

    const wxString message = L"确定删除 SQL 方案「" + fromUtf8(item.name) +
                             L"」(ID " + wxString::Format(L"%d", item.id) + L")？";
    if (wxMessageBox(message, L"SQL 方案库",
                     wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
        return;
    }

    db::SqlQueryDAO dao(db_);
    if (!dao.remove(currentId_)) {
        wxMessageBox(L"删除 SQL 方案失败。", L"SQL 方案库",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    currentId_ = 0;
    reloadList(0);
}

void SqlQueryManageDialog::onSetDefault(wxCommandEvent&) {
    if (!db_ || currentId_ <= 0) {
        wxMessageBox(L"请先保存方案，再设为默认。", L"SQL 方案库",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    db::SqlQueryDAO dao(db_);
    if (!dao.setDefault(currentId_)) {
        wxMessageBox(L"设置默认 SQL 方案失败。", L"SQL 方案库",
                     wxOK | wxICON_ERROR, this);
        return;
    }
    reloadList(currentId_);
    statusText_->SetLabel(L"已设为默认。 " + statusText_->GetLabel());
}

void SqlQueryManageDialog::onRefresh(wxCommandEvent&) {
    reloadList(currentId_);
}

void SqlQueryManageDialog::onFilterChanged(wxCommandEvent&) {
    reloadList(0);
}

void SqlQueryManageDialog::onListSelected(wxListEvent&) {
    if (loading_) {
        return;
    }
    const int id = selectedRowId();
    if (id > 0) {
        reloadEditor(id);
    } else {
        clearEditor(currentFilterScope());
    }
}

void SqlQueryManageDialog::onListActivated(wxListEvent& event) {
    if (loading_) {
        return;
    }
    const long index = event.GetIndex();
    if (index < 0) {
        return;
    }
    list_->SetItemState(index,
                        wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                        wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    chooseCurrent();
}

void SqlQueryManageDialog::onChoose(wxCommandEvent&) {
    chooseCurrent();
}

void SqlQueryManageDialog::onClose(wxCommandEvent&) {
    EndModal(wxID_CANCEL);
}

void SqlQueryManageDialog::chooseCurrent() {
    const int id = selectedRowId();
    if (id <= 0) {
        wxMessageBox(L"请先在左侧选择一个 SQL 方案。", L"SQL 方案库",
                     wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!db_) {
        wxMessageBox(L"数据库未连接，无法读取方案。", L"SQL 方案库",
                     wxOK | wxICON_ERROR, this);
        return;
    }

    const db::SqlQueryItem item = db::SqlQueryDAO::getById(db_, id);
    if (item.id <= 0) {
        wxMessageBox(L"方案不存在，列表将刷新。", L"SQL 方案库",
                     wxOK | wxICON_WARNING, this);
        reloadList(0);
        return;
    }

    if (!requiredScope_.empty() && item.scope != requiredScope_) {
        const wxString message = L"当前配置需要「" + scopeDisplayName(requiredScope_) +
                                 L"」范围的方案，选中的方案是「" +
                                 scopeDisplayName(item.scope) + L"」范围。";
        wxMessageBox(message, L"SQL 方案库", wxOK | wxICON_WARNING, this);
        return;
    }

    selectedId_ = id;
    EndModal(wxID_OK);
}
