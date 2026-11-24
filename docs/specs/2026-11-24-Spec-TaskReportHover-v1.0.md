# Spec 2026-11-24 任务完成报告 + 任务列表悬停显示 v1.0

- **文档编号**: `2026-11-24-Spec-TaskReportHover-v1.0`
- **文档类型**: spec
- **模块**: `task` / `Logger` / `TaskMonitorPanel`
- **日期**: 2026-11-24
- **状态**: 🟡 draft（待评审）
- **关联**: `docs/specs/2026-11-03-Spec-TaskQueueMonitorAndLogTabs-v1.2.md`（任务队列监控）、`docs/specs/2026-10-15-Spec-ToolbarLabels-v1.0.md`、`docs/architecture.md`

---

## 1. 目标（Goal）

新增功能：**每个任务完成后必须生成一份 report（从日志抽取），置于 log 目录；在任务列表中悬停某任务时显示其 report。**

本 Spec 定义该功能的输入、输出、前置条件、边界条件与验收标准。实现与测试按 `docs/plans/2026-11-24-Plan-TaskReportHover-v1.0.md` 执行。

## 2. 背景与现状（As-is）

- 任务系统：`task::TaskQueue`（`include/task/TaskQueue.h`）经 `submit(type, func, priority, callbacks, targetId)` 入队，`workerLoop()`（`src/task/TaskQueue.cpp:356`）单线程顺序执行；任务结束（`COMPLETED`/`FAILED`/`CANCELLED`）后 `archiveFinishedLocked(handle)` 入 `history_`，再调 `entry.callbacks.onComplete(handle)`。
- 任务句柄 `task::TaskHandle`：`taskId/type/status/priority/createdAt/startedAt/completedAt/error/parentTaskId/child*/progress*/targetId`。
- 任务类型 `task::TaskType`：19 种（UPDATE_SUBSCRIPTION … PROBE_MEMBER）。
- 日志：`Logger`（`include/Logger.h`）单文件/运行 `logDir/prefix_YYYYMMDD_HHMMSS.log`；GUI 日志目录 = `exeDir/log`（`src/main_gui.cpp:465`，不存在则创建）；`LogLevel` 已含 `REPORT=3`；`Logger::getLogDir()/getFilePath()/getPrefix()` 可取；`Logger::pushCallback/popCallback` 支持作用域日志回调（`LogCallback = function<void(const string&, LogLevel)>`）。
- 任务列表 UI：`src/ui/TaskMonitorPanel.cpp` 的 `wxListCtrl* pendingList_`（ID_TASK_PENDING_LIST），行序 = 当前任务 → 排队 → 已完成（history）；`pendingIds_` 并行存每行 `taskId`；刷新由 `MainFrame` 的 `bottomViewTimer_` 每秒调 `refreshNow()`（`TaskMonitorPanel` 无自有 timer）。
- 现状**无**任务级 report 生成、无 report 文件、无 per-item tooltip（仅 `StandaloneFloatingWidget` 有 hover，与本功能无关）。

## 3. 需求（Requirements）

### R1 任务完成后必须生成 report
每个经 `TaskQueue` 执行的任务（全部 19 种类型、任一终态 COMPLETED/FAILED/CANCELLED）在完成时必须生成一份 report。

- **必须**：报告生成是硬性不变量。报告写入失败不得使任务失败或崩溃——任务结果（COMPLETED/FAILED/CANCELLED）不受报告写入影响；写入失败记录 WARN 并跳过。
- **从日志抽取**：report 正文 = 任务执行窗口内（`startedAt`→`completedAt`）经 `Logger` 产生的日志行（级别 ≥ `LogLevel::INFO`），由任务运行期的**作用域日志回调**捕获。

### R2 report 置于 log 目录
report 文件写入 `Logger::getLogDir()`（与运行期日志同目录），文件名 `report_<taskId>.txt`。

### R3 report 内容
report 文件为 UTF-8 文本，至少包含：
- 元数据头：`taskId`、`type`（`taskTypeToString`）、`status`、`createdAt`/`startedAt`/`completedAt`、`durationMs`、`targetId`、`error`（有则含）、`progressCurrent/progressTotal`（有则含）、`parentTaskId`（子任务含）。
- **统计（Statistics）节**（结构化）：`total`、`success`、`failed`、`skipped`（来自结构化统计通道 D6；任务未提供时为 0/省略）。
- 日志摘录：捕获到的任务窗口日志行（原样保留时间戳+级别+正文），行数可 0（任务未写日志时正文为空，仍生成含元数据+统计的 report）。

### R4 任务列表中悬停显示 report
`TaskMonitorPanel` 的 `pendingList_` 上，悬停（mouse over）某**已完成**任务行时显示该任务的 report；离开该行时隐藏。此外，已完成行的**上下文菜单提供"查看报告"项**（键盘可达路径），点击复用同一浮层显示。

- 已完成行 = `RowFinished`（`rowKinds_` 为 `RowFinished`），其 taskId 存在 report。
- 悬停显示为**浮层弹窗**（`wxPopupTransientWindow` 内只读多行 `wxTextCtrl`，含滚动条），内容 = report 文件全文；窗口尺寸上限约 560×420 DIP，超出滚动。
- 悬停非已完成行 / 无 report 的行：不显示浮层（不误报、不伪造内容）。
- 浮层只读（`wxPopupTransientWindow` 风格 `wxPU_CONTAINS_CONTROLS` 使内部控件可交互/滚动；`Popup()` 默认 null focus 不抢焦点——`wxPU_NO_FOCUS` 在 wxWidgets 3.3.3 不存在，弃用；MSW 下 `wxPU_CONTAINS_CONTROLS`+null focus 不调 SetFocus），离开行或列表时自动隐藏（`wxEVT_LEAVE_WINDOW` 绑定触发隐藏）。

### R5 文档输出与评审
本 Spec + 对应实现计划作为交付文档输出；实现完成后按 `requesting-code-review` 提交评审。

## 4. 输入（Inputs）
- 任务完成事件（`TaskHandle`，来自 `TaskQueue::workerLoop`）。
- 任务执行窗口内的日志行（经 `Logger::pushCallback` 捕获）。
- `Logger::getLogDir()`（report 输出目录）。
- `TaskMonitorPanel::pendingList_` 悬停位置（`wxEVT_MOTION` + `HitTest` → 行号 → `pendingIds_[row]` → taskId）。

## 5. 输出（Outputs）
- `Logger::getLogDir()/report_<taskId>.txt`（每个完成任务一份）。
- 任务列表面板悬停浮层（只读文本展示 report 内容）。

## 6. 前置条件（Preconditions）
- `Logger` 已 `init`（`getLogDir()` 非空）。
- `TaskQueue` worker 已启动、任务已提交。
- `TaskMonitorPanel` 已创建、`pendingList_` 有效、`pendingIds_`/`rowKinds_` 与行号并行一致（刷新后重建）。

## 7. 边界条件（Edge Cases）
- **报告写入失败**（目录不可写/磁盘满/文件占用）：任务结果不变，记 WARN，悬停时该行无 report 可显示（不弹浮层）。
- **任务无日志行**：仍生成含元数据的 report（正文空），悬停显示元数据。
- **同 taskId 重复完成**（`forceRelease` 归档路径按 `archiveFinishedLocked` 原地替换）：report 文件覆盖重写，不产生重复文件。
- **悬停非已完成行 / 无 report 行**：不显示浮层。
- **日志捕获回调异常**：捕获以 try/catch 包裹，异常不冒泡进任务执行。
- **任务取消/失败**：`CANCELLED`/`FAILED` 同样生成 report（含 `error` 字段）。
- **浮层在列表滚动/刷新期间**：`refreshNow()` **顺序固定 + 条件隐藏**——(1) 重建前捕获 `popupTaskId_`；(2) 重建后按 `popupTaskId_` 扫描恢复 `hoveredRow_` 新行号（边界保护），该 taskId 已不在列表则隐藏；(3) 然后才用恢复后的索引做条件检查——仅当 `pendingIds_[hoveredRow_]==popupTaskId_`、仍 `RowFinished` 且 report 存在时保持浮层（静止悬停不被每秒刷新打断），否则隐藏。
- **`pendingIds_` 与行号不一致**（刷新竞态）：悬停处理器按行号越界检查，越界则忽略。

## 8. 设计决策（Decisions）
- **D1 报告生成挂点 = `TaskQueue` 级而非每个 `submit` 调用点**：避免 19 类任务在 `AppController` 逐处重复注册报告逻辑（DRY）。`TaskQueue` 新增一个**完成后回调**（`setTaskFinishedSink(std::function<void(const TaskHandle&, const std::vector<LogLine>&, const TaskStats&)>)`），`workerLoop` 在 `onComplete` 之后、`busy_.store(false)` 之前调用；`AppController` 注册 sink 到 `TaskReportService`。（评审 2026-11-24：签名由 2 参更正为 3 参，以承载 D2 日志行与 D6 统计。）
- **D2 日志抽取 = 任务执行窗口作用域回调**：`workerLoop` 执行 `entry.func(ctx)` 期间 `Logger::pushCallback` 捕获 `level >= INFO` 行；结束后 `popCallback`。窗口捕获会混入同窗口内其它线程日志（如网络监控 timer），视为可接受噪声；report 正文保留原日志行原样。
- **D3 report 文件 + 内存索引并存**：新组件 `TaskReportService`（`src/task/TaskReportService.cpp`）持有 `unordered_map<taskId, TaskReportInfo{filePath, content}>`（内存索引供悬停即时读取）+ 写盘。悬停读取内存索引，不重复读文件。
- **D4 悬停浮层 = `wxPopupTransientWindow` + 只读多行 `wxTextCtrl`**：自绘浮层可控尺寸/滚动/焦点，UI 自动化可定位（按窗口标题/类断言）。（风格 `wxPU_CONTAINS_CONTROLS`；辨识标题经 `SetLabel(wxString::FromUTF8("任务报告 "+taskId))`——`wxWindow::SetLabel` 在 MSW 映射 `SetWindowText`，UI 自动化按 `GetWindowTextW` 定位；`SetTitle` 仅 `wxTopLevelWindow` 有故弃用；`Position(ptOrigin, wxSize)` 两参 + 工作区钳制。）
- **D5 报告为 UTF-8 文本**：与现有日志一致；文件扩展名 `.txt`（区别于运行日志 `.log`）。
- **D6 结构化统计通道（用户选定，2026-11-24）**：任务操作（`SubitemUpdaterV2`（含 `syncDatabases`，SYNC_DB 实际路径）/`ProxyBatchTester`/`Deduplicator`/`RegionBatchResolver`/`AutoTaskManager` 等；Importer 仅 CLI 调用、非任务类型，CloudSyncService 非 SYNC_DB 任务路径，二者**不改造**）在既有汇总点（已计算 total/success/failed/skipped）处经**可选出参 `task::TaskStats* stats = nullptr`** 暴露统计；任务 lambda 将统计传入 `TaskContext`（新增 `task::TaskStats stats` + `addStats()` 累积）；`workerLoop` 在任务结束后读取 `ctx.stats`，经 `TaskFinishedSink` 传给 report。向后兼容：未传 `stats` 的既有调用（CLI 等）行为不变。简单任务（FIND/EXPORT/GENERATE/PROBE 等）由 lambda 从返回值直接 `addStats({1, ok, !ok, 0})`。
- **D6a 统计与 REPORT 日志同源（单源）**：结构化统计**复用**既有 REPORT 汇总行背后的**同一批已算计数变量**（如 `Deduplicator` 的 `beforeCount_/afterCount_/totalDeleted`、`SubitemUpdaterV2` 的 `totalSubs/successCount/failedSubs`），在生成汇总行处同时填充出参。**不解析日志文本、不重复计算**；REPORT 汇总行仍原样保留在日志摘录，结构化节是同一数据的机器可读形式。审计证据见 §11。

## 9. 验收标准（Acceptance）
- **A1** 每个完成任务（含 FAILED/CANCELLED）在 `Logger::getLogDir()` 生成 `report_<taskId>.txt`，含元数据头 + 统计节 + 日志摘录。
- **A2** 任务执行期间产生的 INFO+ 日志行出现在对应 report 正文中。
- **A3** 报告写入失败时任务终态不受影响，且记 WARN。
- **A3b** 结构化统计通道：任务操作经 `TaskStats*` 出参暴露 total/success/failed/skipped，写入 report 统计节；未提供统计的任务统计为 0。
- **A4** `TaskMonitorPanel` 悬停已完成任务行显示 report 浮层，内容与 report 文件一致；离开隐藏；已完成行上下文菜单"查看报告"（键盘路径）显示同一浮层。
- **A5** 悬停非已完成/无 report 行不显示浮层。
- **A6** 单元测试（`TaskStats` 累积、`TaskReportService` 生成/落盘/索引、日志捕获、任务操作统计暴露）与 UI 自动化（悬停浮层）通过。
- **A7** `scripts/build-and-test.bat` 门禁全绿（既有 `UI_FLOATINGWIDGET` 已知 flaky 单跑复验除外）。
- **A8** `auto` 计数 0（项目约束）；C++17。

## 10. 范围外（Out of Scope）
- 不改变任务队列调度/优先级/取消语义。
- 不做任务级日志标签（taskId 注入每条日志行）——窗口捕获已满足「从日志抽取」，标签化留待后续（若需精确归属）。
- 不改日志文件命名/格式/级别策略。
- 不实现 report 的导出/共享/历史清理（仅生成 + 悬停显示；清理策略另议）。

## 11. 审计证据（2026-11-24 调查：所有任务均有 report 级日志）
结论：**全部 19 类 `TaskType` 在执行窗口内都会产生 INFO+（大量为 REPORT）日志**，故 D2 窗口捕获（`level >= INFO`，含 REPORT/WARN/ERR）下 report 正文对所有任务均非空；A2 成立。

| TaskType | 执行路径 | 日志证据（文件:行） |
| :--- | :--- | :--- |
| UPDATE_SUBSCRIPTION / UPDATE_ALL | `update::SubitemUpdaterV2` | REPORT 汇总 `Update Summary`（`SubitemUpdaterV2.cpp:422-435`）+ INFO（:278/:378/:398/:463/:495） |
| TEST_SUBSCRIPTION / TEST_SINGLE_PROXY / TEST_ALL / TEST_BATCH / TEST_ONLINE | `ProxyBatchTester` | REPORT（`ProxyBatchTester.cpp:332/:819-827`）+ INFO（:996/:999/:1044/:1106/:1148） |
| FIND_FIRST / FIND_BEST / FIND_BY_INDEX | `ProxyFinder` | INFO（12 处） |
| DEDUP | `update::Deduplicator` | INFO（13 处）+ REPORT（18 处，含 `Deduplication Summary` `Deduplicator.cpp:63`） |
| SYNC_DB | `update::SubitemUpdaterV2::syncDatabases`（GUI 任务路径 `doSyncDatabases`→`syncDatabases`） | REPORT `Migration Result`（`SubitemUpdaterV2.cpp:1448` 附近） |
| EXPORT_SHARE_LINKS | `AppController::exportShareLinks` | INFO（`Exported N proxies` `AppController.cpp:1106` / `No proxies to export` `:1091`） |
| RESOLVE_REGION / RESOLVE_SINGLE_REGION | `RegionBatchResolver` | REPORT（`finalMsg` `RegionBatchResolver.cpp:147`） |
| GENERATE_CONFIG | `config::TemplateDecomposer` | REPORT（`TemplateDecomposer.cpp:467`） |
| AUTO_TASK | `AutoTaskManager` | INFO（4 处）+ REPORT（15 处，步骤/结果） |
| PROBE_POOL_MEMBERS / PROBE_MEMBER | `ProxyProbePool` | INFO（`started N workers` `ProxyProbePool.cpp:155`）+ WARN（:85/:94/:101） |

附注：`LogLevel::REPORT`（值 3）非死代码；枚举序 `TRACE=0<DEBUG=1<INFO=2<REPORT=3<WARN=4<ERR=5`，捕获 `>=INFO`（>=2）必含 REPORT。`LogPanel` 已能按 REPORT 级别过滤（`LogPanel.cpp:256/:268`）。
