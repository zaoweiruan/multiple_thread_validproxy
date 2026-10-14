#ifndef UI_BUNDLE_SELECTOR_DIALOG_H
#define UI_BUNDLE_SELECTOR_DIALOG_H

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <string>
#include <sqlite3.h>

#include "config/ComponentBundle.h"

/// Bundle 选择对话框：列出指定 CoreType 下全部启用的 ComponentBundle
/// （ID/名称/版本/核心），双击或「确定」返回选中 bundleId。
class BundleSelectorDialog : public wxDialog {
public:
    BundleSelectorDialog(wxWindow* parent, sqlite3* db,
                         config::CoreType coreType = config::CoreType::XRAY);

    /// 静态便捷方法：显示对话框并返回选中的 bundleId（取消返回空串）
    static std::string SelectBundle(wxWindow* parent, sqlite3* db,
                                    config::CoreType coreType = config::CoreType::XRAY);

    /// 取回选中的 bundleId
    std::string getSelectedId() const { return selectedId_; }

private:
    void onOk(wxCommandEvent& event);
    void onDoubleClick(wxListEvent& event);

    sqlite3* db_;
    config::CoreType coreType_;
    wxListCtrl* list_;
    std::string selectedId_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_BUNDLE_SELECTOR_DIALOG_H