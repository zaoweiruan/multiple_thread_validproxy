#ifndef UI_CONFIG_DIALOG_H
#define UI_CONFIG_DIALOG_H

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/property.h>
#include <wx/propgrid/advprops.h>
#include <sqlite3.h>

#include "ConfigReader.h"
#include "FullConfigTemplateDAO.h"

// ---------------------------------------------------------------
// ConfigDialog — tabbed config editor (app config + template seeds)
// ---------------------------------------------------------------
class ConfigDialog : public wxDialog {
public:
    ConfigDialog(wxWindow* parent, const config::AppConfig& cfg, sqlite3* db);

    config::AppConfig getConfig() const { return editedConfig_; }
    bool isModified() const { return modified_; }

private:
    void loadConfig(const config::AppConfig& cfg);
    bool saveConfig();
    void onOk(wxCommandEvent& event);
    void onCancel(wxCommandEvent& event);
    void onPropertyChanged(wxPropertyGridEvent& event);
    bool validateConfig();
    void refreshUpdateMethodDisplay();
    void refreshAutoTaskChainDisplay();
    // 「代理后端选择」两个后端路径组（条件可见，解决 P3）——指针在构建树时捕获
    wxPGProperty* xrayBackendCat_{nullptr};
    wxPGProperty* sbBackendCat_{nullptr};
    void applyBackendVisibility();

    // 模板种子 tab
    void createTemplateTab();
    void loadTemplates();
    void onTemplateSelection(wxListEvent& event);
    void onTemplateSave(wxCommandEvent& event);
    void onTemplateRefresh(wxCommandEvent& event);

    config::AppConfig editedConfig_;
    std::vector<std::string> stepOrder_;
    bool modified_{false};

    wxNotebook* notebook_{nullptr};
    wxPropertyGrid* propGrid_;

    // 模板种子 tab 控件
    wxListCtrl* templateList_{nullptr};
    wxTextCtrl* templateEditor_{nullptr};
    std::vector<db::FullConfigTemplateItem> templates_;
    sqlite3* db_{nullptr};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_CONFIG_DIALOG_H