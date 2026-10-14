#ifndef UI_BUNDLE_CHAIN_EDITOR_H
#define UI_BUNDLE_CHAIN_EDITOR_H

#include <wx/wx.h>
#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/choice.h>
#include <functional>
#include <string>
#include <vector>
#include <sqlite3.h>
#include <boost/json.hpp>

// windef.h defines ERROR/WARN macros which collide with config::ConflictSeverity
#ifdef ERROR
#undef ERROR
#endif
#ifdef WARN
#undef WARN
#endif

#include "config/ComponentBundle.h"
#include "config/BundleChainResolver.h"

/// BundleChain 编辑器（设计文档 §8.1/§8.2）：
/// 顶部按钮（添加 Bundle / 移除） + Bundle 列表 + 参数 JSON 编辑器 + 解析按钮。
/// 解析时调用 BundleChainResolver 生成组件与冲突，供外层 ComponentListPanel 显示。
class BundleChainEditor : public wxPanel {
public:
    /// @param showButtons false = 不显示内置「添加/移除/解析」按钮条，只显示列表。
///        组件链编辑页需把三栏表头放进**同一条** header 行才能让三个列表框
///        顶/底严格对齐，此时由调用方（以自己的窗口为 parent）重建这三个按钮，
///        再调 triggerAddBundle()/triggerRemoveBundle()/triggerResolve()。
///        控件不可跨 parent 搬运——wxSizer::SetContainingWindow 会断言
///        「sizer 管理的控件 parent 必须是该 sizer 所属窗口」。
    BundleChainEditor(wxWindow* parent, sqlite3* db, bool showButtons = true);

    /// 设置当前链（刷新 Bundle 列表 + 参数编辑器）
    void setChain(const config::BundleChain& chain);
    /// 取回编辑后的链（bundleIds + params）
    config::BundleChain getChain() const;
    /// 清空编辑内容
    void clear();

    /// 使用当前链解析生成组件+冲突（供外层 ComponentListPanel 显示）
    config::BundleChainResolveResult resolve() const;

    /// 解析并生成最终配置 JSON（占位符替换 + 组件应用）。
    /// 与 resolve() 的区别：result.config 被填充，供 UI 显示配置预览。
    config::BundleChainResolveResult resolveToConfig() const;

    /// 设置当前模板的 CoreType（阶段 8.4：Bundle 选择器按核心过滤）
    void setCoreType(config::CoreType coreType) { coreType_ = coreType; }

    /// 解析完成回调（外层通过此回调把组件/冲突交给 ComponentListPanel）
    std::function<void(const config::BundleChainResolveResult&)> onResolved;

    /// 由 ConfigDialog 重建的同名按钮触发（保持行为与内置按钮一致）
    void triggerAddBundle();
    void triggerRemoveBundle();
    void triggerResolve();

private:
    void refreshBundleList();
    void onAddBundle(wxCommandEvent& event);
    void onRemoveBundle(wxCommandEvent& event);
    void onResolve(wxCommandEvent& event);
    void onBundleSelected(wxListEvent& event);

    sqlite3* db_;
    config::CoreType coreType_{config::CoreType::XRAY};
    config::BundleChain chain_;
    wxListCtrl* bundleList_;
    wxButton* addBtn_;
    wxButton* removeBtn_;
    wxButton* resolveBtn_;

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_BUNDLE_CHAIN_EDITOR_H