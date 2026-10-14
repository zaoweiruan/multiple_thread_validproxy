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
    // 注：「配置管线」UI 已于 2026-10-10 移除（setPipeline() 全仓库无调用点，
    // 管线永不执行，属未启用的遗留半成品）。组件链方案已覆盖「可编排」需求。
    // 底层 ConfigPipeline / StepRegistry / StepHandler 类及其单测全部保留未动。
    bool readConfigJson(boost::json::value& root) const;
    bool writeConfigJson(const boost::json::value& root) const;
    std::string formatJson(const boost::json::value& value) const;

    // 组件包配置 tab
    void createBundleTab();
    void loadBundles();
    void onBundleSelection(wxListEvent& event);
    void onBundleSave(wxCommandEvent& event);
    void onBundleNew(wxCommandEvent& event);
    void onBundleDelete(wxCommandEvent& event);
    void onBundleRefresh(wxCommandEvent& event);
    void populateBundleTypeTable();
    void populateBundleVarTable();

    // 组件链编辑 tab（阶段 8.2/8.4/8.5）
    void createChainEditTab();
    void syncChainEditorFromSelection();
    void onChainSave(wxCommandEvent& event);
    void showChainPreview(const std::string& jsonText);

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

    // 注：原「配置管线」控件（pipelineList_ / pipelines_ / templateChoice_ /
    // stepList_ / stepParamEditor_ / previewEditor_ / previewStatus_ /
    // currentPipelineName_ / currentStepIndex_）已随该 Tab 一并移除。
    // 注意 stepOrder_ 仍在（AutoTask 步骤顺序，与管线无关，勿删）。

    sqlite3* db_{nullptr};
    std::string configPath_;

    // 组件包配置 tab 控件
    wxListCtrl* bundleList_{nullptr};
    wxTextCtrl* bundleEditor_{nullptr};
    /// 组件类型速查表（由 ComponentRegistry 运行时生成）
    wxListCtrl* bundleTypeTable_{nullptr};
    /// 占位符速查表（与 ConfigBuilder::variablesToMap 对应）
    wxListCtrl* bundleVarTable_{nullptr};
    std::vector<config::ComponentBundle> bundles_;

    // 组件链编辑 tab 控件（阶段 8）
    BundleChainEditor* chainEditor_{nullptr};
    ComponentListPanel* componentListPanel_{nullptr};
    wxStaticText* chainStatus_{nullptr};
    wxTextCtrl* chainPreview_{nullptr};
    /// 「当前模板：<id>（<备注>）」——显示在组件链编辑页最上方，
    /// 随模板列表选中项变化（syncChainEditorFromSelection 中刷新）
    wxStaticText* chainTemplateLabel_{nullptr};

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_CONFIG_DIALOG_H