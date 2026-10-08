#ifndef UI_COMPONENT_LIST_PANEL_H
#define UI_COMPONENT_LIST_PANEL_H

#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/listctrl.h>
#include <string>
#include <vector>
#include <sqlite3.h>

// windef.h defines ERROR/WARN macros which collide with config::ConflictSeverity
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif

#include "config/ComponentBundle.h"
#include "config/ComponentRegistry.h"
#include "config/ConflictDetector.h"

/// 组件列表视图面板（设计文档 §8.1 组件列表视图 + §8.2 冲突高亮）
/// 列：# / 组件类型 / 配置节 / 组合策略 / 来源 Bundle / 状态
/// 冲突高亮：WARN 标黄"警告"（依赖缺失，允许保存）、ERROR 标红"错误"（阻止保存）
class ComponentListPanel : public wxPanel {
public:
    ComponentListPanel(wxWindow* parent, sqlite3* db);

    /// 设置合并后的组件列表与来源信息（sources 用于显示来源 Bundle）
    void setComponents(const std::vector<config::ComponentConfig>& components,
                       const std::vector<config::ComponentSource>& sources);

    /// 设置冲突状态（ERROR 标红阻止保存、WARN 标黄允许但提示）
    void setConflictStatus(const std::vector<config::Conflict>& conflicts);

    /// 是否存在阻塞性（ERROR）冲突
    static bool hasErrors(const std::vector<config::Conflict>& conflicts);

    /// 清空列表
    void clear();

private:
    void rebuild();

    wxListCtrl* list_;
    std::vector<config::ComponentConfig> components_;
    std::vector<config::ComponentSource> sources_;
    std::vector<config::Conflict> conflicts_;
};

#endif // UI_COMPONENT_LIST_PANEL_H