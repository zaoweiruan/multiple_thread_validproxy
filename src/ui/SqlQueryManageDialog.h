#ifndef UI_SQL_QUERY_MANAGE_DIALOG_H
#define UI_SQL_QUERY_MANAGE_DIALOG_H

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <sqlite3.h>

#include <string>

#include "SqlQuery.h"

/// SQL 查询方案库对话框：
///   上方列出 SqlQuery 表中的方案（ID/名称/范围/默认/描述），
///   下方编辑名称、范围、默认标记、描述与 SQL 文本；
///   支持新增、保存、删除、设为默认、刷新，以及选择某个方案返回其 ID。
/// 打开时可指定 requiredScope（"all" / "by_subid"），
/// 选择时只接受相同范围的方案，防止把通用 SQL 配到按 SubId 的配置项上。
class SqlQueryManageDialog : public wxDialog {
public:
    SqlQueryManageDialog(wxWindow* parent, sqlite3* db,
                         const std::string& requiredScope = std::string(),
                         int currentId = 0);

    /// 静态便捷入口：模态显示，返回选中方案 ID；取消/无选中返回 0。
    static int SelectSqlQuery(wxWindow* parent, sqlite3* db,
                              const std::string& requiredScope = std::string(),
                              int currentId = 0);

    int getSelectedId() const { return selectedId_; }

private:
    void buildUi();
    void bindEvents();
    void reloadList(int selectId);
    void reloadEditor(int id);
    void clearEditor(const std::string& preferredScope);
    void selectRowById(int id);
    void clearListSelection();
    int selectedRowId() const;
    std::string currentFilterScope() const;
    bool currentFilterAccepts(const std::string& scope) const;
    bool collectEditor(db::SqlQueryItem& item);

    void onNew(wxCommandEvent& event);
    void onSave(wxCommandEvent& event);
    void onDelete(wxCommandEvent& event);
    void onSetDefault(wxCommandEvent& event);
    void onRefresh(wxCommandEvent& event);
    void onFilterChanged(wxCommandEvent& event);
    void onListSelected(wxListEvent& event);
    void onListActivated(wxListEvent& event);
    void onChoose(wxCommandEvent& event);
    void onClose(wxCommandEvent& event);
    void chooseCurrent();

    sqlite3* db_{nullptr};
    std::string requiredScope_;
    int currentId_{0};
    int selectedId_{0};
    bool loading_{false};

    wxListCtrl* list_{nullptr};
    wxChoice* filterChoice_{nullptr};
    wxChoice* scopeChoice_{nullptr};
    wxTextCtrl* nameText_{nullptr};
    wxTextCtrl* descriptionText_{nullptr};
    wxTextCtrl* sqlText_{nullptr};
    wxCheckBox* defaultCheck_{nullptr};
    wxStaticText* statusText_{nullptr};
};

#endif // UI_SQL_QUERY_MANAGE_DIALOG_H
