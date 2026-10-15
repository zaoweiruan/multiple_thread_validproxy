// tests/ui/UIIds.h - pinned UI identifiers for the wxWidgets main window.
//
// All values pinned from the LIVE [dumptree] control-tree dump (Task 6,
// 2026-08-24, sandbox DB ui-test.db). Layout fact: the main frame has NO
// notebook tabs - it is a vertical splitter (top = subscription DataView,
// bottom = proxy-result DataView) with a log area below and a menu bar
// (文件 / 代理 / 任务 / 设置 / 帮助).
#pragma once

namespace uitest {
namespace ids {

// Main frame window (live dump: type=50032 class="wxWindowNR")
inline const wchar_t* const MainWindowName  = L"validproxy - Proxy Manager";
inline const wchar_t* const MainWindowClass = L"wxWindowNR";

// NOTE: plan assumed notebook tabs; live tree disproved this. The constants
// below are kept only so Task 7 suites can assert their absence / navigate
// via menus instead. Do NOT use them as tab selectors.
inline const wchar_t* const TabProxyList    = L"(no tabs - splitter layout)";
inline const wchar_t* const TabSubscription = L"(no tabs - splitter layout)";
inline const wchar_t* const TabLog          = L"(no tabs - splitter layout)";

// Search box on the proxy list panel (live dump: type=50033 name="searchCtrl"
// wrapping an Edit type=50004)
inline const wchar_t* const SearchCtrlName  = L"searchCtrl";
inline const wchar_t* const SearchCtrlClass = L"wxWindowNR";

// DataView controls share the automation Name "dataviewCtrl"; they are told
// apart by order inside the splitter (first = subscription list, second =
// proxy results). Header columns for disambiguation:
//   subscription list: # / 启用 / 名称 ↕ / 有效 ↕ / 代理数 ↕ / 更新 ↕ / ID ↕
//   proxy result list: Region / Latency ↕ / Health ↕ / Type / Host ↕ ...
inline const wchar_t* const DataViewCtrlName       = L"dataviewCtrl";
inline const wchar_t* const DataViewInnerClass     = L"wxDataView";

// Floating standalone-monitor widget (SetName on the top-level wxFrame).
// The only locator allowed for asserting its presence/absence.
inline const wchar_t* const FloatingWidgetName = L"StandaloneFloatingWidget";
inline const wchar_t* const SubscriptionHeaderName = L"启用";
inline const wchar_t* const ResultHeaderName       = L"Region";

// Log area buttons (live dump: type=50000 class="Button")
inline const wchar_t* const LogClearButtonName = L"清空日志窗口";
inline const wchar_t* const LogStatsButtonName = L"日志统计";
inline const wchar_t* const LogOpenButtonName  = L"打开日志";

// Unified monitor panel (StandaloneFloatingWidget Panel mode): pool controls
// moved from the removed StandalonePoolDialog into the floating widget.
// The only locators allowed for asserting the panel's presence / controls.
inline const wchar_t* const MonitorPanelPoolStartBtnName   = L"启动池"; // idle label
inline const wchar_t* const MonitorPanelPoolStopBtnName    = L"停止池"; // running label
inline const wchar_t* const MonitorPanelPoolRefreshBtnName = L"刷新";
// "添加代理" button inside the monitor panel — the entry point that opens the
// AddPoolMemberDialog picker for injecting existing proxies into the pool.
inline const wchar_t* const MonitorPanelPoolAddBtnName     = L"添加代理";
// AddPoolMemberDialog title (the picker opened by 添加代理).
inline const wchar_t* const PoolPickerName                 = L"选择代理";
// Right-click "加入代理池" entry on the proxy list (only shown when the pool
// feature is enabled in config).
inline const wchar_t* const ProxyListAddToPoolMenuName     = L"加入代理池";

// ---------------------------------------------------------------------------
// Menu bar captions (all UI text is Chinese since the i18n pass).
//
// Top-level menus carry a wxWidgets "&" mnemonic prefix, and readNativeMenu()
// preserves it verbatim, so the bar actually reads "&文件" etc. The constants
// below hold the *bare* caption: Menu.h::menuCaptionMatches() skips "&" and
// folds case, so both forms resolve to the same entry.
// ---------------------------------------------------------------------------
inline const wchar_t* const MenuFileCaption     = L"文件";   // bar: "&文件"
inline const wchar_t* const MenuProxyCaption    = L"代理";   // bar: "&代理"
inline const wchar_t* const MenuTaskCaption     = L"任务";   // bar: "&任务"
inline const wchar_t* const MenuSettingsCaption = L"设置";   // bar: "&设置"
inline const wchar_t* const MenuHelpCaption     = L"帮助";   // bar: "&帮助"

// Leaf items. readNativeMenu() splits on the first '\t', so the accelerator
// ("Ctrl+F" etc.) is stored separately in MenuEntry::accelerator.
inline const wchar_t* const MenuImportSubItem    = L"导入订阅链接…";   // 6100 [Ctrl+I]
inline const wchar_t* const MenuUpdateAllItem    = L"更新全部订阅";     // 6103 [Ctrl+U]
inline const wchar_t* const MenuSyncDbItem       = L"同步数据库…";     // 6101
inline const wchar_t* const MenuExitItem         = L"退出";            // 6102 [Alt+X]

inline const wchar_t* const MenuFindProxyItem    = L"查找首个可用代理"; // 6105 [Ctrl+F]
inline const wchar_t* const MenuFindBestItem     = L"查找最佳代理";     // 6106 [Ctrl+Shift+F]
inline const wchar_t* const MenuDedupItem        = L"移除重复代理";     // 6107
inline const wchar_t* const MenuExportItem       = L"导出分享链接";     // 6108
inline const wchar_t* const MenuGenConfigItem    = L"生成配置…";        // 6109
inline const wchar_t* const MenuStandaloneMonItem = L"独立代理监控…";   // 6114 [Ctrl+M]

inline const wchar_t* const MenuAutoTaskRunItem   = L"执行自动任务";    // 6112 [Ctrl+T]
inline const wchar_t* const MenuAutoTaskResumeItem = L"恢复自动任务";   // 6113
inline const wchar_t* const MenuCancelTaskItem    = L"取消任务";        // 6208

inline const wchar_t* const MenuConfigItem       = L"配置…";           // 6110 [Ctrl+,]
inline const wchar_t* const MenuAboutItem        = L"关于";            // 6111

// Toolbar button label for ID_TOOL_FIND (short help stays "查找最佳代理").
inline const wchar_t* const ToolBarFindBestLabel = L"最佳代理";

} // namespace ids
} // namespace uitest
