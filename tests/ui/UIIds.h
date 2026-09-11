// tests/ui/UIIds.h - pinned UI identifiers for the wxWidgets main window.
//
// All values pinned from the LIVE [dumptree] control-tree dump (Task 6,
// 2026-08-24, sandbox DB ui-test.db). Layout fact: the main frame has NO
// notebook tabs - it is a vertical splitter (top = subscription DataView,
// bottom = proxy-result DataView) with a log area below and a menu bar
// (File / Proxy / 任务 / Settings / Help).
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

} // namespace ids
} // namespace uitest
