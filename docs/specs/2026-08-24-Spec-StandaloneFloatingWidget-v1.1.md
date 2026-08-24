---
title: "feat: 可配置悬浮窗监控独立代理进程 (StandaloneFloatingWidget) — v1.1 统一接管版"
type: feat
status: draft
date: 2026-08-24
origin: "用户需求演进：v1.0 draft 评审后修订 — 配置并入 proxy_process_monitor 复用 enabled/checkIntervalMs；悬浮窗替代 StandaloneMonitorDialog；鼠标悬停展开详情"
supersedes: "docs/specs/2026-08-24-Spec-StandaloneFloatingWidget-v1.0.md (draft, 已废弃)"
---

# Spec: StandaloneFloatingWidget v1.1 — 统一接管独立代理进程监控

> **评审状态**: 📝 **draft — 待评审**。本文档为 v1.0 的修订版（v1.0 已废弃），**未实施任何代码变更**。
> 评审通过后按 §7 实施计划逐任务执行（TDD），并回写 status: completed。

---

## 1. 修订动机（v1.0 → v1.1）

v1.0 draft 将悬浮窗设计为"**新增的并列第三入口**"（独立 `floating_widget` 配置段 + 保留 `StandaloneMonitorDialog` + 保留状态栏）。评审后用户裁定更激进的统一方案：

| 维度 | v1.0 | **v1.1（本稿）** |
|---|---|---|
| 配置 | 新增 `floating_widget` 段（9 字段） | **并入 `proxy_process_monitor`，零新增键**，复用 `enabled` + `checkIntervalMs` |
| 与既有弹窗关系 | 并存（另加悬浮窗） | **悬浮窗替代 `StandaloneMonitorDialog`**（对话框删除） |
| 详情展示 | 常驻全量表格 + ×关闭 | **折叠条 + 鼠标悬停展开**详情 |
| `targetProcessNames` | 保留 | **废弃**（状态栏计数改由悬浮窗同源数据驱动） |

目标收益：配置面收敛、UI 入口唯一、去除一套独立的"按进程名监控"子系统。

## 2. 目标与非目标

### 2.1 目标 (v1.1)

- G1 新增无边框置顶悬浮窗 `StandaloneFloatingWidget`，**折叠态常驻显示受管独立代理进程总数**，**鼠标悬停展开**为详情表（IndexId/Host/运行时长/端口/PID）。
- G2 悬浮窗**替代** `StandaloneMonitorDialog`：删除对话框类，原 Ctrl+M / 工具栏"监控代理"按钮 / Proxy 菜单项全部重定向到悬浮窗。
- G3 **配置零新增**：使用既有 `proxy_process_monitor.enabled` 作主开关、`proxy_process_monitor.checkIntervalMs` 作刷新周期；透明度/置顶/悬停行为硬编码为默认值。
- G4 **废弃 `targetProcessNames` 监控子系统**：状态栏 `proxyMonPanel_` 计数改为由 `AppController::getRunningStandaloneCount()`（与悬浮窗同源）驱动。
- G5 复用既有数据链 `getWatchedStandaloneMonitors()` 与事件链 `LocateProxyEvent`（双击行定位），**零新增业务查询、零 DB schema 变更**。
- G6 配置热应用：ConfigDialog「监控代理进程」保存后，悬浮窗即时响应（定时器周期 + 显隐随 `enabled` 变化）。

### 2.2 非目标

- N1 不新增任何 config 键（含透明度/置顶/位置记忆均不落盘，硬编码）。
- N2 不提供行内"停止进程"按钮（`stopStandaloneProxy` 集成延后至后续版本）。
- N3 不提供"钉住"展开态（v1.1 展开仅由悬停驱动；如需常驻展开后续另议）。
- N4 不做多显示器智能锚定（默认锚点仅主显示器工作区）。

## 3. 现状盘点（两套相关子系统）

```text
子系统 A — proxy_process_monitor (配置段，含 targetProcessNames)
  ├─ ConfigReader::proxy_process_monitor { enabled, checkIntervalMs, targetProcessNames }
  ├─ ProxyProcessMonitorConfigParser（按进程名枚举存活数）
  └─ 状态栏 proxyMonPanel_ 显示「绿点 + 存活数」（来自名称匹配，非 DB 会话）

子系统 B — 独立代理 watch（StandaloneMonitorDialog 当前数据源）
  ├─ AppController::getWatchedStandaloneMonitors() → vector<StandaloneMonitorRow>
  │     (indexId, host, startedAt, durationMs, socksPort, pid)
  ├─ AppController::getRunningStandaloneCount() → 上述集合 size
  └─ StandaloneMonitorDialog（860×360 弹窗，Ctrl+M 打开，双击行 LocateProxyEvent）

v1.1 统一后：A 的名称监控废弃；B 的数据同时驱动 状态栏计数 + 悬浮窗。
```

## 4. 总体设计

### 4.1 架构与数据流

```text
proxy_process_monitor (既有段，复用)
  ├─ enabled            ← 主开关：特征 + 悬浮窗显隐
  └─ checkIntervalMs    ← 复用为悬浮窗/状态栏刷新周期

StandaloneFloatingWidget : wxFrame (顶层, 无父窗口, wxSTAY_ON_TOP 硬编码)
  ├─ 折叠态 (常驻):  ● 独立代理 N          ← FromDIP(~150×30) 无边框小条
  └─ 悬停展开态 (EVT_ENTER_WINDOW):
        IndexId | Host | 运行时长 | 端口 | PID   ← wxDataViewCtrl（原弹窗表格）
        双击行 → LocateProxyEvent(indexId) → MainFrame 定位 ProxyListPanel
  └─ EVT_LEAVE_WINDOW → 折叠回小条

入口收敛：
  Ctrl+M / 工具栏「监控代理」/ Proxy 菜单项 → onMenuStandaloneMonitor 改指
        floatingWidget_->toggleActive()   （取代 show dialog）

状态栏 proxyMonPanel_（保留 UI 元素，改数据源）:
  onProxyMonTimer → AppController::getRunningStandaloneCount() → 绿点(N>0)/灰点 + N
```

### 4.2 文件结构

| 文件 | 操作 | 职责 |
|---|---|---|
| `src/ui/StandaloneFloatingWidget.h/.cpp` | 新建 | 悬浮窗本体（折叠/悬停展开/表格/定位） |
| `src/ui/StandaloneMonitorDialog.h/.cpp` | **删除** | 被悬浮窗替代 |
| `include/ConfigReader.h` | 修改 | `proxy_process_monitor` 结构体**移除 `targetProcessNames`** |
| `include/config/sections/ProxyProcessMonitorConfigParser.h` | 修改 | 停止解析 `targetProcessNames`；保留 `enabled`/`checkIntervalMs`（含既有限制） |
| `src/config/ConfigJsonSerializer.cpp` | 修改 | 停止写回 `targetProcessNames` |
| `src/ui/ConfigDialog.cpp/.h` | 修改 | 「监控代理进程」类别移除 targetProcessNames 编辑控件；注明启用即显示悬浮窗 |
| `src/ui/MainFrame.h/.cpp` | 修改 | 删除 `monitorDialog_`；新增 `floatingWidget_`；`onMenuStandaloneMonitor` 重定向；状态栏数据源切换；启动默认激活；ConfigDialog OK 热应用 |
| `src/ui/AppController.cpp` | 修改（核查） | 确认 `getRunningStandaloneCount()` 为悬浮窗/状态栏同源计数；移除名称监控残留（若有独立 `ProxyProcessMonitor` 类则删除） |
| `include/FloatingWidgetPolicy.h` | 新建 | 纯 std 无 wx 依赖：折叠/展开状态决策 + 间隔钳制（可单测） |
| `tests/test_floating_widget_state.cpp` | 新建 | Policy 单测（GTest，无 wx） |
| `tests/ui/UIIds.h` | 修改 | 删除对话框常量；固化悬浮窗常量 |
| `tests/ui/TestStandaloneFloatingWidget.cpp` | 新建 | UIA 悬停展开 + Ctrl+M 开关冒烟 |
| `CMakeLists.txt` | 修改 | 删除 dialog 测试目标；注册 test_floating_widget_state；validproxy 源列表调整 |

## 5. 详细设计

### 5.1 配置管线变更（零新增键）

**AppConfig 结构体**（`include/ConfigReader.h`，移除 `targetProcessNames`）：

```cpp
// 代理进程监控（v1.1 起同时驱动悬浮窗；targetProcessNames 已废弃）
struct {
    bool enabled = true;
    int checkIntervalMs = 2000;   // 复用为悬浮窗/状态栏刷新周期（既有限制区间）
} proxy_process_monitor;
```

**解析器**（`ProxyProcessMonitorConfigParser.h`）：删除 `targetProcessNames` 解析分支；保留 `enabled`/`checkIntervalMs` 的类型校验与钳制（沿用既有 `[500, 60000]` 或解析器既有下限）。

**序列化**（`ConfigJsonSerializer.cpp`）：删除 `targetProcessNames` 写回块；仅写 `enabled` + `checkIntervalMs`。

> 实施前核查：需 `grep` 全仓确认 `targetProcessNames` 的其它消费者（ConfigDialog 控件、`ProxyProcessMonitor` 类、状态栏定时器原始数据源、相关单测），一并清理，保证无悬空引用。

### 5.2 策略头（纯逻辑，可单测）

新建 `include/FloatingWidgetPolicy.h`（**不含 wx 头**，GTest 目标免链接 wxWidgets）：

```cpp
#ifndef FLOATING_WIDGET_POLICY_H
#define FLOATING_WIDGET_POLICY_H

#include <cstddef>

namespace floatwidget {

struct Defaults {
    static const int kMinIntervalMs = 500;
    static const int kMaxIntervalMs = 60000;
};

inline int clampIntervalMs(int v) {
    if (v < Defaults::kMinIntervalMs) return Defaults::kMinIntervalMs;
    if (v > Defaults::kMaxIntervalMs) return Defaults::kMaxIntervalMs;
    return v;
}

// 折叠条是否可见：功能启用 且 用户未关闭
inline bool shouldShowCollapsed(bool configEnabled, bool userActive) {
    return configEnabled && userActive;
}

// 是否展开详情：可见 且 鼠标悬停
inline bool shouldExpand(bool collapsedVisible, bool hovering) {
    return collapsedVisible && hovering;
}

} // namespace floatwidget
#endif
```

### 5.3 StandaloneFloatingWidget 类设计

**头文件**（`src/ui/StandaloneFloatingWidget.h`）：

```cpp
#ifndef UI_STANDALONE_FLOATING_WIDGET_H
#define UI_STANDALONE_FLOATING_WIDGET_H

#include <wx/frame.h>
#include <wx/timer.h>
#include <vector>
#include <functional>

#include "AppController.h"
#include "FloatingWidgetPolicy.h"

// 统一接管独立代理进程监控的悬浮窗：折叠条常驻 + 悬停展开详情。
// 替代 StandaloneMonitorDialog（v1.1）。顶层无父窗口，须 MainFrame 显式 delete。
class StandaloneFloatingWidget : public wxFrame {
public:
    StandaloneFloatingWidget(const config::AppConfig& cfg,
                             AppController* controller,
                             wxEvtHandler* locateTarget,
                             std::function<void()> openDetails);

    bool Show(bool show = true) override;
    void applySettings(const config::AppConfig& cfg);   // 热应用：周期 + 显隐
    void toggleActive();                                 // Ctrl+M / 菜单
    bool isActive() const { return active_; }

private:
    void onRefreshTimer(wxTimerEvent& event);
    void onEnterWindow(wxMouseEvent& event);
    void onLeaveWindow(wxMouseEvent& event);
    void onPaint(wxPaintEvent& event);
    void onItemActivated(wxDataViewEvent& event);        // 双击 → LocateProxyEvent
    void onCloseWindow(wxCloseEvent& event);

    void refreshRows();
    void relayout(bool expanded);
    void anchorCollapsedPosition();

    config::AppConfig cfg_;
    AppController* controller_;
    wxEvtHandler* locateTarget_;
    std::function<void()> openDetails_;
    wxTimer timer_;
    wxDataViewCtrl* detailView_{nullptr};
    std::vector<StandaloneMonitorRow> rows_;

    bool active_{false};        // 用户开关（Ctrl+M），启动默认 = cfg.proxy_process_monitor.enabled
    bool hovering_{false};      // 当前鼠标是否在窗口内

    wxDECLARE_EVENT_TABLE();
};

#endif // UI_STANDALONE_FLOATING_WIDGET_H
```

#### 5.3.1 折叠 / 展开视觉规格

```text
折叠态（常驻，无边框小条，FromDIP ~150×30，wxSTAY_ON_TOP 硬编码）:
  ● 独立代理 N          ← ● 绿(N>0) / 灰(N==0)

悬停展开态（EVT_ENTER_WINDOW，FromDIP ~540×320）:
  IndexId | Host | 运行时长 | 端口 | PID
  a1b2c3  | h.com | 12:34    |10808| 4084
  d4e5f6  | x.xyz | 1:05:07  |10809| 5544
  （空态）居中灰字：无运行中的独立代理
```

- 全部几何经 `FromDIP()` 换算（DPI 安全）。
- 展开方向：默认在折叠条下方展开；若距屏幕底边不足，改为上方展开（`anchorCollapsedPosition` 计算可用区）。
- `durationMs` 格式化：「<60min → mm:ss；否则 h:mm:ss」（静态辅助，逻辑可放 Policy 头单测）。

#### 5.3.2 悬停展开判定（MSW）

```cpp
void StandaloneFloatingWidget::onEnterWindow(wxMouseEvent& event) {
    hovering_ = true;
    relayout(true);      // SetSize(展开尺寸) + detailView_->Show()
    event.Skip();
}

void StandaloneFloatingWidget::onLeaveWindow(wxMouseEvent& event) {
    hovering_ = false;
    relayout(false);     // SetSize(折叠尺寸) + detailView_->Hide()
    event.Skip();
}
```

> 关键点：展开后鼠标移入详情区仍在**同一窗口**内，不会触发 leave；仅当光标完全移出（含展开区域）才折叠，避免抖动。

#### 5.3.3 显隐状态机

| 输入 | 动作 |
|---|---|
| GUI 启动（cfg.enabled） | `active_ = true` → 显示折叠条 |
| Ctrl+M / 菜单 | `toggleActive()`：翻转 `active_` → 显/隐折叠条（展开态随之失效） |
| 鼠标 enter | `hovering_=true` → 展开（若 active_ 且 enabled） |
| 鼠标 leave | `hovering_=false` → 折叠 |
| ConfigDialog 取消 enabled | `applySettings`：`active_=false`，隐藏整窗 |
| ConfigDialog 改 checkIntervalMs | `applySettings`：`timer_.Start(clampIntervalMs(...))` |

> 设计要点：悬浮窗显隐完全由 `active_` × `cfg.enabled` 决定，悬停只控制"展开/折叠"不控制"存在"。与 v1.0 的 `hide_when_empty` 自动隐藏不同——v1.1 折叠条**始终显示计数（含 0）**，与状态栏语义一致，配置更简。

#### 5.3.4 定位与详情

- 双击详情行 → `wxQueueEvent(locateTarget_, new LocateProxyEvent(rows_[row].indexId))`（`locateTarget_` = MainFrame，构造函数注入，因顶层窗口 `GetParent()==nullptr`）。
- 「详情」按钮需求消失（悬停即详情），故 `openDetails_` 回调可省略；若保留上下文菜单「打开独立代理监控」再注入 `MainFrame::onMenuStandaloneMonitor` 占位（v1.1 可不含此菜单项）。

### 5.4 MainFrame 集成

| 触点 | 变更 |
|---|---|
| 成员 | 删除 `monitorDialog_`；新增 `StandaloneFloatingWidget* floatingWidget_{nullptr};` |
| `onMenuStandaloneMonitor` | 改调 `floatingWidget_->toggleActive()`（原 show dialog 逻辑删除）；`cfg.proxy_process_monitor.enabled==false` 时禁用菜单项/提示 |
| 入口绑定 | Ctrl+M（`ID_MENU_STANDALONE_MON = wxID_HIGHEST+114`）、工具栏 `ID_TOOL_STANDALONE_MON`、Proxy 菜单项全部指向新 handler |
| 启动激活 | GUI 就绪（`startMonitoring()` 路径）后：`if (cfg.proxy_process_monitor.enabled) floatingWidget_->Show(true)`（active_ 默认 true） |
| 状态栏数据源 | `onProxyMonTimer`（MainFrame.cpp:871）改调 `controller_->getRunningStandaloneCount()` 取代原名称监控计数；绿点 `N>0` |
| 配置热应用 | `onMenuConfig` 保存回调后调 `floatingWidget_->applySettings(config_)`（现有 onMenuConfig 已对比 netMon 设置，同模式扩展） |
| 析构防护 | `~MainFrame()` 显式 `delete floatingWidget_`（顶层无父窗口，置空防双重释放） |

### 5.5 废弃 `targetProcessNames` 的连锁清理（实施前核查清单）

- [ ] `include/ConfigReader.h` 结构体去字段
- [ ] `ProxyProcessMonitorConfigParser.h` 去解析分支
- [ ] `ConfigJsonSerializer.cpp` 去写回
- [ ] `ConfigDialog.cpp/.h` 去编辑控件 + 类别文案更新
- [ ] 若仓库存在独立 `ProxyProcessMonitor` 运行时类（名称枚举线程）→ 删除其启动/心跳，刷新职责移交悬浮窗 `wxTimer` 与状态栏 `onProxyMonTimer`
- [ ] `test_config_reader` 中 `targetProcessNames` 相关断言改为"缺省段含 enabled/checkIntervalMs、无 targetProcessNames 键"类断言
- [ ] 搜索全仓 `targetProcessNames` / `ProxyProcessMonitor` 引用，清零悬空

## 6. 决策点（已裁定 + 残留微决策）

### 6.1 评审已裁定

| # | 议题 | 裁定 |
|---|---|---|
| ① | 数据来源 / 子系统关系 | **B 彻底统一**：悬浮窗取代 `targetProcessNames` 监控；状态栏计数改由 `getRunningStandaloneCount()` 同源驱动；`targetProcessNames` 废弃 |
| ② | 状态栏 `proxyMonPanel_` 去留 | **保留**（仅改数据源为同源计数） |
| ③ | 配置粒度 | **完全复用、零新增键**；透明度/置顶/悬停硬编码默认（置顶=true、悬停展开=true、透明度=不透明以规避分层窗口怪异，单常量可改） |

### 6.2 残留微决策（建议默认，可推翻）

| # | 议题 | 建议默认 |
|---|---|---|
| M1 | 启动默认激活 | `enabled` 时默认显示折叠条（用户 Ctrl+M 可关） |
| M2 | 透明度 | 不透明（跳过 `SetTransparent`，规避 R8 远程桌面分层渲染异常） |
| M3 | 展开方向 | 下方展开；空间不足上方展开 |
| M4 | 详情控件 | 复用 `wxDataViewCtrl`（与 ProxyListPanel 同款，列定义轻量） |

## 7. 实施计划（TDD 任务分解）

> 执行约束：DEV-PROCESS 7 步流；每任务先测后码；全栈禁用 `auto`；构建命令见 AGENTS.md §4.1。

### U1: 配置管线去 `targetProcessNames`（RED→GREEN）
- [ ] Step 1 `test_config_reader` 调整断言（缺省段解析 enabled/checkIntervalMs，无 targetProcessNames 键残留）
- [ ] Step 2 改 `ConfigReader.h` / `ProxyProcessMonitorConfigParser.h` / `ConfigJsonSerializer.cpp` / `ConfigDialog`
- [ ] Step 3 `ctest -R ConfigReaderTest -V` PASS
- [ ] Step 4 commit `refactor(config): drop targetProcessNames, unify under proxy_process_monitor`

### U2: 删除 StandaloneMonitorDialog + 入口重定向
- [ ] Step 1 删 `src/ui/StandaloneMonitorDialog.*`；`MainFrame` 去 `monitorDialog_` 成员与 `onMenuStandaloneMonitor` 旧实现
- [ ] Step 2 handler 改 `floatingWidget_->toggleActive()`；Ctrl+M/工具栏/菜单绑定重定向
- [ ] Step 3 删除旧对话框 UIIds 常量
- [ ] Step 4 commit `refactor(ui): retire StandaloneMonitorDialog, redirect entries to floating widget`

### U3: 实现 StandaloneFloatingWidget（折叠 + 悬停展开）
- [ ] Step 1 建 `include/FloatingWidgetPolicy.h` + `tests/test_floating_widget_state.cpp`（U1 前或此处补单测：clamp/shouldShowCollapsed/shouldExpand）
- [ ] Step 2 建 `src/ui/StandaloneFloatingWidget.h/.cpp`（relayout/expand/DVC/LocateProxyEvent/置顶硬编码）
- [ ] Step 3 编译 `cmake --build build --parallel 8` 0 error
- [ ] Step 4 commit `feat(ui): implement StandaloneFloatingWidget with hover-expand`

### U4: 状态栏数据源切换 + 悬浮窗定时器
- [ ] Step 1 `onProxyMonTimer` 改 `getRunningStandaloneCount()`；绿点语义不变
- [ ] Step 2 悬浮窗 `wxTimer` 周期 = `clampIntervalMs(cfg.proxy_process_monitor.checkIntervalMs)`
- [ ] Step 3 commit `feat(ui): retarget status bar + floating timer to watched count`

### U5: 配置热应用
- [ ] Step 1 `onMenuConfig` 保存后调 `floatingWidget_->applySettings(config_)`（周期重启 + enabled 显隐）
- [ ] Step 2 commit `feat(ui): hot-apply proxy_process_monitor config to floating widget`

### U6: UI 自动化冒烟（DEV-PROCESS v1.1 强制项）
- [ ] Step 1 `--dumptree` 实测悬浮窗 Name/Class 固化 `tests/ui/UIIds.h`（禁猜选择器）
- [ ] Step 2 用例：Ctrl+M 开合 → 窗口出现/消失；悬停 → 详情区可见
- [ ] Step 3 `.\scripts\build-and-test.bat` 一键 ALL TESTS PASSED exit 0
- [ ] Step 4 commit `test(ui): add floating widget hover-expand smoke`

### U7: 文档收尾
- [ ] 本文档 status → completed；INDEX/tracker/CONTEXT 同步
- [ ] commit `docs(spec): finalize floating widget v1.1`

## 8. 测试计划汇总

| 层级 | 载体 | 覆盖点 |
|---|---|---|
| 纯逻辑单测 | `test_floating_widget_state`（GTest，无 wx） | clampIntervalMs 边界、shouldShowCollapsed/shouldExpand 真值表 |
| 配置单测 | `test_config_reader` | 缺省段 enabled/checkIntervalMs 解析；无 targetProcessNames 残留；save/load 往返 |
| UI 自动化 | UITests（Catch2 + UIA） | Ctrl+M 开合；悬停展开详情区可见（Name 定位 + PID 过滤；沙箱 test/ui-sandbox，红线禁触 bin/worker 与 test/guindb.db） |
| 手工回归 | GUI 实机 | 启动默认显示折叠条；悬停展开/离开折叠；双击行定位；ConfigDialog 关 enabled 后悬浮窗消失；状态栏计数同源一致 |

## 9. 风险与缓解

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R1 | 移除 `targetProcessNames` 影响面未知（隐藏消费者） | 中 | U1 实施前核查清单全仓 grep 清零；单测断言"无残留键" |
| R2 | 悬停展开抖动（边缘/多窗口） | 低 | 同窗口 enter/leave 语义；展开方向按可用区计算；MSW 实测 |
| R3 | 顶层无父窗口泄漏/双重释放 | 中 | onClose + ~MainFrame 双点 delete + 置空（同 trayIcon_ 既有模式） |
| R4 | 状态栏语义变更（名称计数→会话计数） | 低 | 属 B 裁定预期行为；手工验收计数一致性 |
| R5 | wxSTAY_ON_TOP 硬编码可能遮挡 | 低 | 决策③硬编码；单常量可切换；折叠条极小 |
| R6 | 透明分层窗口在远程桌面异常 | 低 | M2 默认不透明跳过 SetTransparent |
| R7 | DPI 缩放错位 | 低 | 全几何 FromDIP |

## 10. 文件变更列表

| 文件 | 操作 | 预估规模 |
|---|---|---|
| `src/ui/StandaloneFloatingWidget.h` | 新建 | ~75 行 |
| `src/ui/StandaloneFloatingWidget.cpp` | 新建 | ~280 行 |
| `src/ui/StandaloneMonitorDialog.h` | 删除 | — |
| `src/ui/StandaloneMonitorDialog.cpp` | 删除 | — |
| `include/FloatingWidgetPolicy.h` | 新建 | ~40 行 |
| `include/ConfigReader.h` | 修改 | −1 字段 |
| `include/config/sections/ProxyProcessMonitorConfigParser.h` | 修改 | −targetProcessNames 分支 |
| `src/config/ConfigJsonSerializer.cpp` | 修改 | −写回块 |
| `src/ui/ConfigDialog.cpp/.h` | 修改 | −控件 + 文案 |
| `src/ui/MainFrame.h/.cpp` | 修改 | 成员替换 + handler + 状态栏 + 热应用 |
| `src/ui/AppController.cpp` | 修改（核查） | 名称监控残留清理 |
| `tests/test_floating_widget_state.cpp` | 新建 | ~70 行 |
| `tests/ui/UIIds.h` | 修改 | 常量替换 |
| `tests/ui/TestStandaloneFloatingWidget.cpp` | 新建 | ~60 行 |
| `CMakeLists.txt` | 修改 | 目标增删 |

## 11. 验证步骤（实施完成后）

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8
ctest -V                              # 基线 + 新增 FloatingWidgetStateTest / ConfigReader 调整
ctest -R FloatingWidgetStateTest -V
ctest -R ConfigReaderTest -V
.\scripts\build-and-test.bat          # UI 变更交付门：ALL TESTS PASSED exit 0
.\build\validproxy.exe                # 手工：启动默认折叠条 / 悬停展开 / 双击定位 / ConfigDialog 关 enabled / 状态栏同源
```

**通过标准**: 构建 0 error；ctest 全绿（NetworkMonitorTest 已知环境抖动白名单除外）；build-and-test.bat ALL TESTS PASSED exit 0；全仓无 `targetProcessNames`/`ProxyProcessMonitor` 悬空引用；手工清单全项通过。
