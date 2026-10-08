#ifndef UI_CONFIG_DIALOG_H
#define UI_CONFIG_DIALOG_H

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/property.h>
#include <wx/propgrid/advprops.h>
#include <wx/choice.h>
#include <wx/stattext.h>
#include <sqlite3.h>

#include <unordered_map>
#include <string>
#include <boost/json.hpp>

#include "ConfigReader.h"
#include "FullConfigTemplateDAO.h"
#include "config/PipelineConfig.h"
#include "config/StepHandler.h"
#include "config/StepRegistry.h"
#include "config/ComponentBundleDAO.h"

// 前置声明：组件链编辑面板（阶段 8）
class BundleChainEditor;
class ComponentListPanel;

// ---------------------------------------------------------------
// ConfigDialog — tabbed config editor (app config + template config + pipeline)
// ---------------------------------------------------------------
class ConfigDialog : public wxDialog {
public:
    ConfigDialog(wxWindow* parent, const config::AppConfig& cfg, sqlite3* db,
                 const std::string& configPath);

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

    // 模板配置 tab
    void createTemplateTab();
    void loadTemplates();
    void onTemplateSelection(wxListEvent& event);
    void onTemplateSave(wxCommandEvent& event);
    void onTemplateRefresh(wxCommandEvent& event);

    // 配置管线 tab
    void createPipelineTab();
    void loadPipelineConfig();
    void savePipelineConfig();
    void initializeDefaultPipelines();
    void fillTemplateChoice();
    bool readConfigJson(boost::json::value& root) const;
    bool writeConfigJson(const boost::json::value& root) const;
    void onPipelineSelection(wxListEvent& event);
    void onPipelineDelete(wxCommandEvent& event);
    void onPipelineSave(wxCommandEvent& event);
    void onStepSelection(wxListEvent& event);
    void onStepAdd(wxCommandEvent& event);
    void onStepDelete(wxCommandEvent& event);
    void onStepMoveUp(wxCommandEvent& event);
    void onStepMoveDown(wxCommandEvent& event);
    void onTemplateChoice(wxCommandEvent& event);
    void onGeneratePreview(wxCommandEvent& event);
    void onClearPreview(wxCommandEvent& event);
    bool generatePreviewConfig(boost::json::value& config) const;
    bool validateStepParams(const std::string& type,
                            const boost::json::object& params) const;
    void refreshStepList();
    void refreshStepParamEditor();
    void refreshPipelineList();
    std::vector<std::string> getAvailableTemplateIds() const;
    std::vector<std::string> getAvailableStepTypes() const;
    std::string getStepDisplayName(const std::string& type) const;
    std::string formatJson(const boost::json::value& value) const;

    // 组件包配置 tab
    void createBundleTab();
    void loadBundles();
    void onBundleSelection(wxListEvent& event);
    void onBundleSave(wxCommandEvent& event);
    void onBundleDelete(wxCommandEvent& event);
    void onBundleRefresh(wxCommandEvent& event);

    // 组件链编辑 tab（阶段 8.2/8.4/8.5）
    void createChainEditTab();
    void syncChainEditorFromSelection();
    void onChainSave(wxCommandEvent& event);

    config::AppConfig editedConfig_;
    std::vector<std::string> stepOrder_;
    bool modified_{false};

    wxNotebook* notebook_{nullptr};
    wxPropertyGrid* propGrid_;

    // 模板配置 tab 控件
    wxNotebook* templateSubNotebook_{nullptr};
    wxListCtrl* templateList_{nullptr};
    wxTextCtrl* templateEditor_{nullptr};
    std::vector<db::FullConfigTemplateItem> templates_;

    // 配置管线 tab 控件
    wxListCtrl* pipelineList_{nullptr};
    std::unordered_map<std::string, config::PipelineDefinition> pipelines_;
    wxChoice* templateChoice_{nullptr};
    wxListCtrl* stepList_{nullptr};
    wxTextCtrl* stepParamEditor_{nullptr};
    wxTextCtrl* previewEditor_{nullptr};
    wxStaticText* previewStatus_{nullptr};
    std::string currentPipelineName_;
    int currentStepIndex_{-1};

    sqlite3* db_{nullptr};
    std::string configPath_;

    // 组件包配置 tab 控件
    wxListCtrl* bundleList_{nullptr};
    wxTextCtrl* bundleEditor_{nullptr};
    std::vector<config::ComponentBundle> bundles_;

    // 组件链编辑 tab 控件（阶段 8）
    BundleChainEditor* chainEditor_{nullptr};
    ComponentListPanel* componentListPanel_{nullptr};
    wxStaticText* chainStatus_{nullptr};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_CONFIG_DIALOG_H