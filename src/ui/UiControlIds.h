#ifndef UI_CONTROL_IDS_H
#define UI_CONTROL_IDS_H

#include <wx/defs.h>

// ---------------------------------------------------------------
// UiControlIds — single source of truth for the bottom shared-view
// controls (spec 2026-11-03 v1.2 §3.9). Segments avoid the ids already
// taken in MainFrame.cpp: +100 menu block, +200 toolbar/log block,
// +300 search, +400 timers, +901, +1002.
//
// tests/ui/UIIds.h does NOT include this file (UITests must not pull in
// wxWidgets headers); UIA assertions match by Name/class, per §5.3.
// ---------------------------------------------------------------
enum UiControlIds {
    // 底部视图容器
    ID_BOTTOM_VIEW_PANEL    = wxID_HIGHEST + 1200,
    ID_BOTTOM_VIEW_RADIO    = wxID_HIGHEST + 1201,
    ID_BOTTOM_VIEW_TIMER    = wxID_HIGHEST + 1202,
    // 任务监控面板
    ID_TASK_MONITOR_PANEL   = wxID_HIGHEST + 1300,
    ID_TASK_CURRENT_INFO    = wxID_HIGHEST + 1301,
    ID_TASK_CANCEL_CURRENT  = wxID_HIGHEST + 1302,
    ID_TASK_PENDING_LIST    = wxID_HIGHEST + 1303,
    ID_TASK_CANCEL_SELECTED = wxID_HIGHEST + 1304,
    ID_TASK_CANCEL_ALL      = wxID_HIGHEST + 1305,
    // 任务进度面板（原「测试队列」，owner 2026-11-06 更名）
    ID_BATCH_QUEUE_PANEL    = wxID_HIGHEST + 1400,
    ID_BATCH_STATUS_TEXT    = wxID_HIGHEST + 1401,
    ID_BATCH_GAUGE          = wxID_HIGHEST + 1402,
    ID_BATCH_PERCENT_TEXT   = wxID_HIGHEST + 1403,
    ID_BATCH_QUEUE_DETAIL   = wxID_HIGHEST + 1404,
    ID_BATCH_PENDING_COUNT  = wxID_HIGHEST + 1405,
    ID_BATCH_PENDING_LIST   = wxID_HIGHEST + 1406,
    // 任务列表右键菜单（spec 2026-11-06 §3.3）
    ID_TASK_CTX_CLEAR_ALL   = wxID_HIGHEST + 1500,   // 清理全部完成记录
    ID_TASK_CTX_CANCEL_ONE  = wxID_HIGHEST + 1501,   // 取消此排队任务
    ID_TASK_CTX_REMOVE_ONE  = wxID_HIGHEST + 1502,   // 移除此记录
    ID_TASK_CTX_CANCEL_ALL  = wxID_HIGHEST + 1503,   // 取消全部排队
    ID_TASK_CTX_TERM_ONE    = wxID_HIGHEST + 1504,   // 终止当前任务
};

#endif // UI_CONTROL_IDS_H
