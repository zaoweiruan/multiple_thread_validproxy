---
title: "Spec: 统一代理池与代理监控窗口（v1.0）"
module: src/ui（StandaloneFloatingWidget / MainFrame / AppController）+ tests/ui
status: 待评审
date: 2026-09-09
supersedes: （无 — 新增规格；范围内废弃 StandalonePoolDialog 的既有责权）
---

# 规格说明：统一代理池与代理监控窗口（v1.0）

## 1. 目标

| 目标 | 说明 | 验收标准 |
| --- | --- | --- |
| G1 功能迁移 | 将代理池弹窗（`StandalonePoolDialog`）中的按钮与功能全部移入「独立代理监控」窗口（`StandaloneFloatingWidget` 的 Panel 展开面板），代理池不再拥有独立弹窗 | 悬浮窗 Panel 内可完成：启动/停止池、添加代理、删除成员、刷新、上报健康、自动剔除死亡、自动优化；池对话框代码与入口全部移除 |
| G2 统一监控表 | 监控表新增「类型」列，为每条在线代理标识其归属（**独立** / **代理池**）；独立代理与池成员统一显示在同一张表内，右键功能统一 | Panel 列表每一行均显示类型列；独立代理行为「独立」，池成员行为「代理池」；右键菜单根据行类型提供统一动作（关闭代理 / 删除成员 / 测试在线代理 / 退出）且逻辑一致 |
| G3 废弃代理池弹窗 | 彻底废弃 `StandalonePoolDialog`（源码、入口、测试定位符同步清理） | `ID_MENU_OPEN_POOL`、菜单项「代理池…」、`poolDialog_`、`onMenuOpenPool` 全部删除；`grep -r StandalonePoolDialog` 无残留（文档引用除外） |
| G4 文档交付 | 本规格作为实施前设计文档，登记至 `docs/INDEX.md` | 本文件写入 `docs/specs/`，`docs/INDEX.md` §7.5 新增行 43 并更新头 `updated` 时间 |

## 2. 现状与差距

### 2.1 现状

**2.1.1 代理池弹窗 `StandalonePoolDialog`（`src/ui/StandalonePoolDialog.h/.cpp`）**

- wxDialog，标题「独立代理池」，尺寸 660×440。
- 成员表 `wxListCtrl`（`wxLC_REPORT | wxLC_SINGLE_SEL`）6 列：Tag(120) / 状态(110) / 延迟(ms)(80) / 存活(60) / 失败次数(70) / 错误(170)。
- 控件：`statusText_`（「代理池状态: 运行中/未运行 成员数: N」）、`reportChk_`（上报健康）、`pruneChk_`（自动剔除死亡）、`optimizeChk_`（自动优化）、`startStopBtn_`（启动池/停止池）、`delBtn`（删除选中）、`refreshBtn`（刷新）、`addBtn_`（添加代理）。
- 初始化读 `config.standalone_pool.evaluate` 的 `reportHealth / autoPruneDead / autoOptimize` 设置 checkbox；构造末尾调用 `controller_->getPoolMembers()` 填充。
- 私有方法：`onStartStop / onDelete / onRefresh / onAdd / onToggleReport / onTogglePrune / onToggleOptimize / updateStatusText`；`void setMembers(const std::vector<proxy::PoolMemberView>& members)`。
- `onAdd`：池未运行则先 `startProxyPool()`（失败弹「代理池启动失败」），再 `AddPoolMemberDialog`（标题「选择代理」，`getSelectedIndexIds()`）遍历 `controller_->injectProxyToPool(idx)`。
- `onDelete`：取选中行 indexId → `controller_->removePoolMember(indexId, true)`。
- 添加器 `AddPoolMemberDialog`（`src/ui/AddPoolMemberDialog.h/.cpp`）独立保留，不随池弹窗废弃。

**2.1.2 独立代理监控 `StandaloneFloatingWidget`（`src/ui/StandaloneFloatingWidget.h/.cpp`）**

- wxFrame（`wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP | wxBORDER_NONE | wxFRAME_SHAPED`），`SetName/SetTitle(L"StandaloneFloatingWidget")`（UI Automation 定位用，`UIIds.h::FloatingWidgetName`）。
- 双模式：Mode::Orb（圆形悬浮球，绿色数字显示在线代理数量，默认半径 `CircleDefaults::kDefaultRadius` = 25 → 直径约 50 DIP）；Mode::Panel（380×300 圆角面板，背景 #F5F6F7 边框 #D1D1D1，含 `list_` + `slider_`）。
- Panel 列表当前 5 列：运行时长(分,100) / Host(140) / 监听端口(90) / 索引ID(220) / PID(90)。
- 行为：悬停展开、离开延时收起、拖动吸附边缘；单击行 → `LocateProxyEvent` 定位；行/空白双击 → 主窗最大化⇄最小化（200ms 防抖）；失活自动收起到 Orb。
- 右键菜单（MenuId：`ID_MENU_CLOSE_PROXY = wxID_HIGHEST+500`、`ID_MENU_TEST_ONLINE = wxID_HIGHEST+501`，注释强调独占 id）：有选中行时追加「关闭代理」（`stopStandaloneProxy`，失败且 pid>0 时 TerminateProcess 兜底）；始终有「测试在线代理」（`testOnlineProxiesAsync` → `TestOnlineResultDialog`）、「退出」（wxExit）。
- 数据刷新 `refreshRows()`：周期 timer 调 `controller_->getWatchedStandaloneMonitors()`，Orb 模式重绘数量。

**2.1.3 MainFrame 双入口（`src/ui/MainFrame.cpp/.h`）**

- 菜单/工具栏 ID：`ID_MENU_STANDALONE_MON = wxID_HIGHEST+114 = 6114`；`ID_MENU_OPEN_POOL = wxID_HIGHEST+115 = 6115`；`ID_TOOL_STANDALONE_MON = wxID_HIGHEST+211`。
- 事件表：`:99` `EVT_MENU(ID_MENU_STANDALONE_MON, onMenuStandaloneMonitor)`；`:100` `EVT_MENU(ID_MENU_OPEN_POOL, onMenuOpenPool)`；`:111` `EVT_MENU(ID_TOOL_STANDALONE_MON, onMenuStandaloneMonitor)`；`:136` `Bind(wxEVT_POOL_MEMBERS_UPDATED, &MainFrame::onPoolMembersUpdated, this)`。
- 菜单 `:727-729`：`独立代理监控…\tCtrl+M`（wxITEM_CHECK）、`代理池…`、`Check(ID_MENU_STANDALONE_MON, config_.proxy_process_monitor.enabled)`。
- `onMenuStandaloneMonitor`（:1101）：未启用 `proxy_process_monitor.enabled` 则提示并 return；懒创建 `floatingWidget_`；`toggleActive()`；`syncFloatingWidgetControls()`。
- `onMenuOpenPool`（:1114）：未启用 `standalone_pool.enabled` 则提示；懒创建 `poolDialog_`；`setMembers(getPoolMembers())`；`Show()+Raise()`。
- `onPoolMembersUpdated`（:1128）：`if (poolDialog_) poolDialog_->setMembers(event.takeMembers()); event.Skip();`。
- `syncFloatingWidgetControls`（:1135）：工具栏 ToggleTool + 菜单 Check 同步。
- 启动 :548-550 创建悬浮窗 `setActive(true)`；析构 :574-581 delete `floatingWidget_`、`poolDialog_->Destroy()`。**2.1.4 数据来源与模型（`src/ui/AppController.h`、`include/StandaloneProxyPool.h`）**

- 独立代理监控行：`struct StandaloneMonitorRow { std::string indexId; std::string host; std::string startedAt; int64_t durationMs; int socksPort; int64_t pid; }`（pid=-1 表示无历史行；host 为空表示 profile 缺失）；来源 `getWatchedStandaloneMonitors()`（`standaloneMutex_` 保护 `standaloneProxies_`，join 运行历史行）。
- 池成员视图：`struct PoolMemberView { int indexId; std::string tag /* "px-<indexId>" */; std::string state /* ACTIVE/REMOVE_REQUESTED/DRAINING */; long long lastDelayMs; bool lastAlive; std::string lastError; int failStreak; bool probed; }`；来源 `getPoolMembers()`（`poolMutex_` 保护 `proxyPool_`）。
- 池生命周期：`enum class MemberLifecycleState { ACTIVE, REMOVE_REQUESTED, DRAINING }`（PoolMember 内部字段，对外映射为字符串）。
- 池回调：`std::function<void(const std::vector<PoolMemberView>&)> onMembersChanged`（每个评估周期触发）→ `wxEVT_POOL_MEMBERS_UPDATED` → `PoolMembersUpdatedEvent`（`Events.h:362-368`，携带 `std::vector<proxy::PoolMemberView>`）。
- 池端口：`proxy::resolvePoolPorts` 分配 socks/api 端口。

### 2.2 差距分析

| 需求 | 现状 | 差距 |
| --- | --- | --- |
| 池功能移入监控弹窗 | 池功能在 `StandalonePoolDialog`（660×440 独立弹窗）；悬浮窗 Panel（380×300）仅监控独立代理 | Panel 无池控件、无池成员行；需扩展尺寸与布局，加入池状态/设置/操作控件 |
| 监控表类型列 | Panel 列表只有「运行时长/Host/监听端口/索引ID/PID」5 列，仅含独立代理 | 无「类型」列；无池成员数据源；需统一行模型 |
| 统一右键 | Panel 右键仅针对独立代理（关闭/测试/退出）；池弹窗无右键 | 需按行类型统一：独立行=关闭代理，池行=删除成员，公共=测试在线代理/退出 |
| 废弃池弹窗 | `ID_MENU_OPEN_POOL`、`poolDialog_`、`onMenuOpenPool`、`onPoolMembersUpdated` 转发池对话框 | 入口与转发逻辑需整体清理；测试定位符需迁移 |
| 数据刷新 | 独立代理走 timer；池成员走 `wxEVT_POOL_MEMBERS_UPDATED`（仅转发池对话框） | 统一监控表需合并两个数据源；池事件转投悬浮窗 |

> 依据：`docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md` 第 226/375 行已提出「扩展 `StandaloneFloatingWidget` 新增代理池分区、列出 `pool_.getMembers()`、每行带删除按钮 → `AppController::removePoolMember(indexId)`、`Events.h` 新增事件」。本需求是该既定方向的正式收敛：以悬浮窗 Panel 作为唯一「在线代理监控」窗口，并废弃独立池弹窗。

## 3. 变更范围

| 文件 | 变更 | 说明 |
| --- | --- | --- |
| `src/ui/AppController.h` | 新增 `UnifiedMonitorRow` 结构 + `MonitorType` 枚举 + `getUnifiedMonitorRows()` | 统一数据模型与合并接口（唯一新增业务接口） |
| `src/ui/StandaloneFloatingWidget.h/.cpp` | 扩展 Panel：加大尺寸、列表增列（类型/状态/延迟/失败）、顶部池状态与按钮区、统一右键菜单、数据刷新合并 | 核心改动 |
| `src/ui/AddPoolMemberDialog.h/.cpp` | 不变（保留） | 仍用于「添加代理」picker |
| `src/ui/Events.h` | 不变；`wxEVT_POOL_MEMBERS_UPDATED` 消费方由 MainFrame 转投悬浮窗（可内部 Bind 或 MainFrame 转发） | 事件定义保留 |
| `src/ui/MainFrame.h/.cpp` | 删除 `ID_MENU_OPEN_POOL`、菜单项「代理池…」、`onMenuOpenPool`、`poolDialog_` 成员、`onPoolMembersUpdated` 的池对话框转发分支（或整体移除后改由悬浮窗自消费） | 入口清理 |
| `src/ui/StandalonePoolDialog.h/.cpp` | **删除** | 废弃弹窗 |
| `tests/ui/UIIds.h` | `PoolDialogName/PoolStartStopBtnName/PoolStopBtnName/PoolRefreshBtnName/PoolDeleteBtnName/PoolAddBtnName/PoolOpenCmd` 迁移/改名；`PoolPickerName` 保留；新增统一监控面板定位符 | 定位符同步 |
| `tests/ui/TestStandaloneProxyPool.cpp` | 改写为对新监控面板的 UI 测试（用例语义保留：启动池/添加/删除/刷新/停止池） | UI_POOL ctest 保持 |
| `docs/INDEX.md` | §7.5 新增行 43；头 `updated: 2026-09-09` | 文档登记 |
| `docs/CONTEXT.md`（可选） | §三 会话锚点追加本规格引用 | 会话记录 |

**明确不做**：不修改 `StandaloneProxyPool` 核心、`proxy` 命名空间模型、池配置 schema（`standalone_pool` 12 属性不变）；不移动 `AddPoolMemberDialog` 的「选择代理」标题（`PoolPickerName` 定位符不变）。## 4. 设计要点

### 4.1 统一监控行数据模型（`src/ui/AppController.h`）

新增类型枚举与统一行结构，作为悬浮窗 Panel 的唯一行来源：

```cpp
// 在线代理归属类型
enum class MonitorType {
    Standalone,  // 独立代理（单 Xray 进程）
    Pool         // 代理池成员（共享池进程，标签 px-<indexId>）
};

// 统一监控行 = 独立代理行 ∪ 池成员行 的字段并集
struct UnifiedMonitorRow {
    MonitorType type;          // 独立 | 代理池
    std::string indexId;       // 两类型均有（池成员 indexId 与 tag 后缀一致）
    std::string tag;           // 池成员 "px-<indexId>"；独立代理为空串
    std::string host;          // 独立代理来自 profile；池成员来自 profile.address
    int         socksPort;     // 独立代理监听端口；池成员置 0（经池端口对外，面板显示池端口或 "-"）
    int64_t     pid;           // 独立代理 PID；池成员 -1（共享池进程）
    int64_t     durationMs;    // 独立代理已运行时长；池成员 0
    std::string state;         // 池成员生命周期字符串（ACTIVE/REMOVE_REQUESTED/DRAINING）；独立代理 "运行中"
    long long   lastDelayMs;   // 池成员评估延迟；独立代理 -1
    bool        lastAlive;     // 池成员存活标志（仅在状态列呈现）；独立代理恒 true
    int         failStreak;    // 池成员连续失败次数；独立代理 0
    std::string lastError;     // 池成员最近错误；独立代理空串
};
```

> 说明：同一 profile 可同时作为独立代理与池成员，两行并存、互不合并（本质是两个独立进程/生命周期）。

### 4.2 AppController 合并接口

新增唯一读取入口（独立代理 + 池成员合并），供悬浮窗 timer 周期调用：

```cpp
// AppController.h 新增（private 数据成员不变，复用 standaloneMutex_ / poolMutex_）
std::vector<UnifiedMonitorRow> getUnifiedMonitorRows();
```

实现要点（`src/ui/AppController.cpp`）：

1. 依次取 `getWatchedStandaloneMonitors()`（独立）与 `getPoolMembers()`（池）；两段各自在既有锁保护下完成，**不引入跨锁顺序依赖**（先快照独立，再快照池，避免死锁）。
2. 映射规则：独立行 → `type=Standalone`，填 `indexId/host/socksPort/pid/durationMs/state="运行中"`，`tag`、`lastDelayMs`、`failStreak`、`lastError` 用空值；池行 → `type=Pool`，`indexId` 取 `PoolMemberView::indexId`，`tag` 取 `tag`，`host` 从成员 Profileitem 取（`PoolMemberView` 无 host 字段；如需显示 Host，扩展 `PoolMemberView` 增加 `std::string host`，由 `StandaloneProxyPool::getMembers()` 填充 `profile.address` — **推荐采用**）。
3. 排序：`Standalone` 在前、`Pool` 在后（或按 indexId 稳定排序），保证类型列视觉分组稳定。

### 4.3 Panel 布局扩展（`src/ui/StandaloneFloatingWidget.h/.cpp`）

**尺寸**：380×300 → 建议 600×480（容纳状态行 + 按钮行 + 8 列列表；宽高以实际 DIP 微调，`slider_` 保持置于 Panel 右上角控制 hover 展开延时）。

**列表列（ColumnId 扩展）**：

| 列 | 宽度 | 独立代理显示 | 池成员显示 |
| --- | --- | --- | --- |
| 类型 | 60 | 独立 | 代理池 |
| 标识 | 200 | IndexId | Tag（px-&lt;indexId&gt;） |
| Host | 130 | host | profile.address |
| 监听端口 | 80 | socksPort | 池 socks 端口（`controller_` 池端口）或 "-" |
| 状态 | 90 | 运行中 | ACTIVE / REMOVE_REQUESTED / DRAINING |
| 延迟(ms) | 80 | "-" | lastDelayMs（&gt;0 显示，否则 "-"） |
| 失败次数 | 60 | "-" | failStreak |
| PID | 80 | pid | "-" |

**顶部控件区（新增，替代原 `statusText_`/按钮/checkbox）**：

- 状态文本：`代理池状态: 运行中/未运行  成员数: N  在线代理: M`（N=池成员数，M=独立代理数）。
- 第一行（池控制）：`启动池/停止池`（`startStopBtn_`，文本随 `isProxyPoolRunning()` 切换）、`添加代理`（`addBtn_`，复用 `AddPoolMemberDialog`）、`刷新`（`refreshBtn`，池 `probeNow()` + 全表刷新）。
- 第二行（池设置 checkbox）：`上报健康`（reportChk_）、`自动剔除死亡`（pruneChk_）、`自动优化`（optimizeChk_）；均经 `controller_->setPoolReportHealth/setPoolAutoPruneDead/setPoolAutoOptimize`，初始值读 `config.standalone_pool.evaluate`。

> 「删除选中」按钮不放入按钮区：删除语义移至右键菜单（池行「删除成员」），保持面板简洁并满足「统一右键功能」。### 4.4 统一右键菜单

沿用既有 MenuId 独占编号约定（`ID_MENU_CLOSE_PROXY = wxID_HIGHEST+500`、`ID_MENU_TEST_ONLINE = wxID_HIGHEST+501`），新增 `ID_MENU_REMOVE_POOL_MEMBER = wxID_HIGHEST+502`。菜单构建逻辑（`onContextMenu`）：

| 菜单项 | 适用行类型 | 动作 |
| --- | --- | --- |
| 关闭代理 | 独立行（有选中行） | `controller_->stopStandaloneProxy(indexId)`；失败且 pid&gt;0 时 OpenProcess+TerminateProcess 兜底（沿用现状） |
| 删除成员 | 池行（有选中行） | `controller_->removePoolMember(indexId, true)`（graceful） |
| 定位到代理列表 | 任意选中行 | 复用 `LocateProxyEvent`（单击行已定位，右键菜单提供显式入口） |
| 测试在线代理 | 始终 | `controller_->testOnlineProxiesAsync(this)` → `TestOnlineResultDialog`（沿用现状） |
| 退出 | 始终 | wxExit()（沿用现状） |

- 右键仍先 `list_->HitTest` 按鼠标位置选中行，`contextMenuSel_` 记录；根据选中行 `type` 分支追加「关闭代理」或「删除成员」。
- 空行右键：仅显示「测试在线代理 / 退出」（保持现状行为）。

### 4.5 数据刷新机制与 Orb 计数

- **独立代理行**：沿用 Panel timer（配置 `proxy_process_monitor.checkIntervalMs`）调 `controller_->getUnifiedMonitorRows()` 全量刷新（合并接口已含独立代理）。
- **池成员行**：由 `wxEVT_POOL_MEMBERS_UPDATED`（`PoolMembersUpdatedEvent`）推送。消费方从 MainFrame 的 `poolDialog_` 分支改为**悬浮窗自消费**：建议在 `StandaloneFloatingWidget` 构造函数内 `Bind(wxEVT_POOL_MEMBERS_UPDATED, &StandaloneFloatingWidget::onPoolMembersUpdated, this)`；MainFrame 不再转发。若事件先于悬浮窗创建，可忽略（下次 timer 全量刷新兜底）。
- **全量兜底**：`refreshRows()` 每次周期也调 `getUnifiedMonitorRows()`，池成员快照与事件推送以最后一次为准（两源合并时以 `getPoolMembers()` 为准，避免事件滞后数据覆盖）。
- **Orb 计数语义**：显示 `getUnifiedMonitorRows().size()`（独立 + 池成员总数），数字含义从「在线独立代理数」升级为「在线代理总数」。
- 池未启用（`standalone_pool.enabled=false`）时：按钮区禁用（灰化），池相关列正常显示但无池行。

### 4.6 池控件行为语义（迁移自 `StandalonePoolDialog`）

| 控件 | 行为 |
| --- | --- |
| 启动池/停止池（toggle） | 未运行：`controller_->startProxyPool()`，失败弹「代理池启动失败」；运行中：`controller_->stopProxyPool()`；按钮文本随 `isProxyPoolRunning()` 切换为「启动池」/「停止池」 |
| 添加代理 | 池未运行先 `startProxyPool()`（失败弹提示并 return）；打开 `AddPoolMemberDialog(this, controller_)`；`wxID_OK` 后遍历 `getSelectedIndexIds()` 调 `injectProxyToPool(idx)`，汇总成功数弹窗提示（沿用现状） |
| 刷新 | `controller_` 池 `probeNow()`（若运行）+ 立即全量 `refreshRows()`；兼作行手动刷新 |
| 上报健康 / 自动剔除死亡 / 自动优化 | toggle 时调 `setPoolReportHealth(bool)` / `setPoolAutoPruneDead(bool)` / `setPoolAutoOptimize(bool)`；初始化读取 `config.standalone_pool.evaluate` 对应值 |
| 删除成员（右键） | `removePoolMember(indexId, true)` |

> 约束：全部池操作均在 UI 线程调用（沿用现状）；`startProxyPool` 是同步启动，若池二进制缺失/端口占用导致失败，错误提示与现状保持一致。

### 4.7 MainFrame 清理与事件接线（`src/ui/MainFrame.h/.cpp`）

1. 删除 `ID_MENU_OPEN_POOL`（`wxID_HIGHEST+115`）、`proxyMenu_->Append(ID_MENU_OPEN_POOL, L"代理池…", ...)`（`:728`）、`EVT_MENU(ID_MENU_OPEN_POOL, onMenuOpenPool)`（`:100`）。
2. 删除 `onMenuOpenPool` 实现（:1114）、`MainFrame.h` 中 `StandalonePoolDialog* poolDialog_{nullptr}`（:128）与前置声明（:26）、`#include "StandalonePoolDialog.h"`（`MainFrame.cpp:4`）、析构中 `poolDialog_->Destroy()`（:574-581）。
3. 删除 `onPoolMembersUpdated` 的池对话框转发（:1128-1129 的 `if (poolDialog_) ...` 分支与事件绑定 `:136`）——事件消费下沉到悬浮窗（见 4.5）。
4. 菜单区域保留「独立代理监控… Ctrl+M」与 `ID_MENU_STANDALONE_MON`/`ID_TOOL_STANDALONE_MON` 不变。

### 4.8 配置热应用

- `onMenuConfig`（:1145）热应用路径不变：`floatingWidget_->applySettings(cfg)` 后 `syncFloatingWidgetControls()`。
- `applySettings` 内：若 `proxy_process_monitor.enabled=false` 则 `active_=false` 并隐藏（现状语义）；新增处理：刷新按钮区启用状态（依 `standalone_pool.enabled`）、checkbox 初始值依新配置回填。## 5. 测试影响与新增测试

### 5.1 定位符迁移（`tests/ui/UIIds.h`）

| 现有定位符 | 处置 |
| --- | --- |
| `PoolDialogName = L"独立代理池"` | 删除（弹窗不再存在） |
| `PoolOpenCmd = 6000+115` | 删除（菜单命令移除） |
| `PoolStartStopBtnName = L"启动池"`、`PoolStopBtnName = L"停止池"` | 保留语义，改名 `MonitorPanelPoolStartBtnName` / `MonitorPanelPoolStopBtnName`（面板内按钮文本不变） |
| `PoolRefreshBtnName = L"刷新"`、`PoolAddBtnName = L"添加代理"`、`PoolDeleteBtnName = L"删除选中"` | 迁移为 `MonitorPanel*` 前缀（删除按钮不再存在于面板，定位符删除；删除语义走右键） |
| `PoolPickerName = L"选择代理"` | **保留**（`AddPoolMemberDialog` 不变） |
| `FloatingWidgetName = L"StandaloneFloatingWidget"` | 保留（面板仍属悬浮窗，定位方式不变） |

### 5.2 `tests/ui/TestStandaloneProxyPool.cpp` 改写

- 打开入口：由 `PostMessage(MAKEWPARAM(PoolOpenCmd,0))` 改为显示悬浮窗 Panel（复用 `FloatingWidgetName` + 展开面板/`Show()+Raise()`）。
- 用例语义保留：启动池 → 添加代理（`PoolPickerName` 选择）→ 列表出现「代理池」类型行 → 右键删除成员 → 停止池。
- 断言增加：类型列文本（`独立` / `代理池`）。
- 测试注册仍为 `UI_POOL` ctest（或改名 `UI_MONITOR`，由实施阶段决定，需同步 `CMakeLists.txt` 测试注册）。

### 5.3 新增单测建议（`tests/`）

- `AppController::getUnifiedMonitorRows()` 映射单测（依赖 `test/guindb.db`）：注入一条独立代理 + 一个池成员，断言行数、类型、字段映射正确。

## 6. 风险与缓解

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| Panel 尺寸加大影响 hover 展开/边缘吸附/多屏 | 交互退化 | 尺寸按 DIP 微调；保留 Orb 折叠态；吸附仍按窗口外框计算 |
| 池事件与 timer 全量刷新竞态 | 池成员短暂闪烁/覆盖 | 事件置脏标记，timer 统一渲染；两源以 `getPoolMembers()` 为准 |
| `PoolMemberView` 缺 host 字段 | 池行 Host 列空白 | 扩展 `PoolMemberView` 增加 `host`（由 `getMembers()` 填 `profile.address`） |
| UI 自动化测试重写范围大 | 交付延期 | 定位符迁移与用例改写并行；`PoolPickerName` 不动降低风险 |
| 池未启用时按钮误触 | 启动失败提示噪音 | 按钮区灰化 + `isProxyPoolEnabled()` 前置校验（沿用现状提示） |
| 双击最大化与单击定位共存冲突 | 交互歧义 | 沿用现状防抖；右键菜单新增显式「定位到代理列表」 |

## 7. 分阶段实施建议

- **Phase 1（数据层）**：`UnifiedMonitorRow` + `MonitorType` + `getUnifiedMonitorRows()`（含 `PoolMemberView::host` 扩展）+ 单测。
- **Phase 2（UI 层）**：悬浮窗 Panel 扩展（列表列、按钮区、统一右键、事件自消费）+ MainFrame 清理（删池弹窗与入口）。
- **Phase 3（测试与文档）**：`UIIds.h` 迁移、`TestStandaloneProxyPool.cpp` 改写、`docs/INDEX.md` 登记、手工验证。

## 8. 验收清单

- [ ] Debug 构建 0 error（`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8`）
- [ ] `ctest -V` 全绿；`ctest -R "UI_POOL|Dedup" -V` 相关套件通过
- [ ] `grep -r "StandalonePoolDialog\|ID_MENU_OPEN_POOL\|PoolOpenCmd\|PoolDialogName"` 无残留（`docs/` 引用除外）
- [ ] 手工清单：启用池 → 悬浮窗 Panel 显示 独立+代理池 两类行且类型列正确；启动/停止池、添加代理、右键删除成员、刷新、3 个 checkbox 均生效；Orb 计数 = 两类总数；禁用池时按钮灰化；关闭独立代理仍可 TerminateProcess 兜底
- [ ] `docs/INDEX.md` §7.5 行 43 已登记

## 9. 相关文档

- `docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md`（池规格，本需求的先例基础）
- `docs/design/2026-09-03-Design-StandaloneFloatingWidget-DoubleClickToggleMaximize-v1.0.md`（悬浮窗交互设计）
- `docs/specs/2026-09-08-Review-TrayMinimizeBehavior-v1.0.md`（托盘评审，悬浮窗双击语义）
- `docs/ManualTest-StandaloneProxyPool-v1.0.md`（池手动测试文档）
- `docs/specs/2026-09-07-Spec-ConfigDialog-PoolSync-v1.0.md`（`standalone_pool` 配置同步规格，本文档模板参考）