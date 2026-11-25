# Spec — 任务完成报告格式 v2（时间/时长/去日志摘录/Update methods）

- 日期：2026-11-25
- 类型：Spec（功能调整）
- 模块：任务报告（task-report-hover 后续优化）
- 版本：v1.0
- 状态：评审加固版（设计决策已与 owner 澄清：Req3=报告加 methods 字段、Req2=一并移除捕获链路）

## 1. 目标

对 2026-11-24 交付的任务完成报告（`logDir/report_<taskId>.txt`，由 `TaskReportService` 生成）做 3 项格式调整：

- **R1 时间格式**：报告中所有时间字段（createdAt/startedAt/completedAt）显示为本地墙钟 `yyyy-mm-dd hh:mm:ss`；任务时长改为**分钟**（`durationMinutes`）。
- **R2 去日志摘录**：报告中移除 `---- log excerpt ----` 节及逐行日志；**一并移除**产生该摘录的日志捕获链路（TaskQueue 的 `Logger::pushCallback/popCallback` + capturedLines + `LogLine` 类型）。
- **R3 Update methods**：订阅更新类任务（UPDATE_ALL / UPDATE_SUBSCRIPTION）的报告中新增结构化字段 `updateMethods`（如 `accelerator, proxy`），取自已算的 `config_.update_methods`（**config 派生**，非"实际尝试"派生）；与 R2 移除 log excerpt 后该信息不丢失相响应。**语义**：凡订阅更新类任务且 `config_.update_methods` 非空即填充（含早返回/间隔跳过路径），report 仅当 `updateMethods` 非空才输出该行；**非订阅类任务与 AUTO_TASK 容器**（其内嵌 UPDATE_ALL 步骤）不设置该字段（见 §9）。

## 2. 输入 / 输出

- 输入：`task::TaskHandle`（含 createdAt/startedAt/completedAt 稳态 tick 字符串）、`task::TaskStats`（计数）、订阅更新方法字符串。
- 输出：`logDir/report_<taskId>.txt`，格式如 §4。
- 不改变：任务执行/调度语义、订阅更新逻辑、UI 任务列表（其 `提交于` 列仍用 `wallClockText` 的 `HH:MM:SS`）、i18n 表。

## 3. 前置条件 / 边界

- 时间字段为 `std::chrono::steady_clock::now().time_since_epoch().count()` 十进制字符串（TaskQueue 填充）。steady_clock 无墙钟历元，故墙钟显示须经 `wallNow - (steadyNow - stamp)` 映射（复用 2026-11-03 §11 既有机制）。
- 时间字段无效（空/非数字/尾部杂讯）：R1 下**省略该行**（不显示 `- -`）。
- duration 仅当 startedAt/completedAt 均可解析为整数时输出；否则省略。
- `updateMethods` 仅订阅更新类任务非空；其它任务留空 → 报告省略该行。
- 仅 GUI 路径生成 report（CLI 直调服务不生成，R1 语义收窄保留，2026-11-24 已接受）。

## 4. 报告格式（目标）

```
taskId: <id>
type: <任务类型>
status: <状态>
createdAt: <yyyy-mm-dd hh:mm:ss>
startedAt: <yyyy-mm-dd hh:mm:ss>
completedAt: <yyyy-mm-dd hh:mm:ss>
durationMinutes: <整数分钟>
targetId: <targetId>            # 存在才输出
error: <error>                  # 存在才输出
progressCurrent: <n>            # 非 0 才输出
progressTotal: <n>              # 非 0 才输出
parentTaskId: <id>              # 存在才输出
updateMethods: accelerator, proxy   # 仅订阅更新类且非空才输出
---- statistics ----
total: <n>
success: <n>
failed: <n>
skipped: <n>
```

- **无** `---- log excerpt ----` 节。
- 时间行仅当可解析为墙钟时输出（无效省略）。
- `durationMinutes` = floor(elapsed_seconds / 60)，elapsed = (completedAt - startedAt) 稳态 tick 差值转秒。
- `updateMethods` 行仅当非空输出（其余任务省略）。

## 5. 设计决策

- **D1 时间格式化**：新增纯函数 `task::wallClockDateTime(const std::string& steadyCount, std::chrono::steady_clock::time_point steadyNow, std::chrono::system_clock::time_point wallNow) -> std::string`，返回本地墙钟 `"%Y-%m-%d %H:%M:%S"`；输入无效返回**空串**（供调用方省略）。既有 `wallClockText`（`HH:MM:SS`，UI 列表 `提交于` 列）**不动**（其无效返回 `- -`，保持调用方契约）。两函数共享解析/映射核心（核心暴露"无效"哨兵，两包装各映射为 `- -`/空串）；**既有 `wallClockText` 测试（test_task_format.cpp:68-87）必须保持绿**。`formatReport` 在内部捕获 `steady_clock::now()`/`system_clock::now()` 渲染墙钟（不新增参数；测试只断言格式合法/省略，不断言精确时刻——精确确定性由 `TaskFormatTest` 承担）。
- **D2 时长分钟**：新增纯函数 `task::durationMinutesBetween(const std::string& startSteadyCount, const std::string& endSteadyCount) -> long long`，返回 `floor((end-start)/ticksPerSecond/60)`；任一无效返回 `-1`（调用方省略）。`TaskReportService` 用它替代现有 `parseLong` 差值 + `durationMs` 输出。
- **D3 捕获链路移除**：`TaskQueue::workerLoop` 移除 `Logger::pushCallback/popCallback` 包裹与 `capturedLines`；`TaskFinishedSink` 改为 `std::function<void(const TaskHandle&, const TaskStats&, const std::string&)>`（handle, stats, updateMethods）；`TaskReportService::LogLine` 类型移除；`TaskQueue.h` 移除 `#include "task/TaskReportService.h"`（原仅为 LogLine）。
- **D4 updateMethods 注入**：`TaskContext` 增 `std::string updateMethods`（默认空）；`TaskFinishedSink` 第 3 参 = `ctx.updateMethods`（任务函数返回后读取，ctx 存活时）。`TaskReportService::formatReport/publish` 增 `const std::string& updateMethods` 参数，非空才输出。
- **D5 订阅更新出参**：`SubitemUpdaterV2::run`/`runSingle` 在既有 `task::TaskStats* stats` 之后**追加** `std::string* updateMethods = nullptr`；**在方法字符串构建后、任何早返回前**填充（config 派生语义，含间隔跳过/取消/无网络路径）。方法字符串构建复用单一文件内 `static std::string methodName(UpdateMethod)` 助手（替换现有 3 处 switch：run 的 methodsLog、run 循环 methodName、runSingleWithProxy），run 与 runSingle 均据此构建 `"accelerator, proxy"` 形式。既有调用方（只传 stats）行为不变。
- **D6 AppController 穿线**：sink 注册改 `[this](h, s, um){ if(!h.taskId.empty()) taskReportService_.publish(h, s, um); }`；UPDATE_ALL/UPDATE_SUBSCRIPTION lambda 经 `doUpdateSubscription`/`doUpdateAllSubscriptions` 穿线——**两 helper 均增尾参 `std::string* updateMethods = nullptr`**（AppController.h/.cpp），lambda 传 `&methods` → helper 转传 `runSingle(subId, &local, &methods)`/`run(&local, &methods)` → 回填 `ctx.addStats(local); ctx.updateMethods = methods;`。

## 6. 接口变更汇总

| 位置 | 变更 |
| :--- | :--- |
| `include/task/TaskFormat.h` | 增 `wallClockDateTime`、`durationMinutesBetween` 声明 |
| `src/task/TaskFormat.cpp` | 实现两函数（复用解析核心） |
| `include/task/TaskReportService.h` | 移除 `LogLine`；`formatReport(handle, stats, updateMethods)`、`publish(handle, stats, updateMethods)` |
| `src/task/TaskReportService.cpp` | 时间字段→墙钟、`durationMs`→`durationMinutes`、移除 log excerpt、增 updateMethods 输出 |
| `include/task/TaskQueue.h` | `TaskContext` 增 `updateMethods`；`TaskFinishedSink` 改三参 `(handle, stats, updateMethods)`；移除 `#include TaskReportService.h` |
| `src/task/TaskQueue.cpp` | 移除日志捕获（pushCallback/popCallback/capturedLines）；sink 调 `taskFinishedSink_(entry.handle, ctx.stats, ctx.updateMethods)` |
| `include/SubitemUpdaterV2.h` | `run`/`runSingle` 追加 `std::string* updateMethods = nullptr` |
| `src/SubitemUpdaterV2.cpp` | 汇总点填充 updateMethods（run 与 runSingle 均构建方法字符串） |
| `src/ui/AppController.h` | `doUpdateSubscription`/`doUpdateAllSubscriptions` 追加尾参 `std::string* updateMethods = nullptr` |
| `src/ui/AppController.cpp` | sink 三参；UPDATE_ALL/UPDATE_SUBSCRIPTION 穿线 `&methods`→`ctx.updateMethods` |

## 7. 测试（TDD，TEST_F）

- **TaskFormatTest**：`wallClockDateTime`（合法→`yyyy-mm-dd hh:mm:ss`；空/非数字/尾部杂讯→空串）；`durationMinutesBetween`（合法→floor 分钟；任一无效→-1）。
- **TaskReportServiceTest**：`formatReport` 输出**合法** `yyyy-mm-dd hh:mm:ss`（正则/可解析断言，不断言精确时刻——formatReport 内部捕获 now()）+ `durationMinutes`；**不含** `log excerpt`；`updateMethods` 非空输出/空省略；无效时间戳行省略；现有 publish/find 语义保留。
- **TaskQueueReportTest**：sink 三参（handle, stats, updateMethods）；`ctx.updateMethods` 传至 sink；无日志捕获（sink 不接收 lines）。
- **TaskReportIntegrationTest**：全链路 report 不含 `log excerpt`、含统计。
- **TaskStatsExposureTest**：`SubitemUpdaterV2::run/runSingle` 的 updateMethods 出参填充（可空）。

## 8. 验收标准

- 全量门禁：`scripts\build-and-test.bat` ALL TESTS PASSED；`ctest --test-dir build -V` 全绿（既有 flaky 例外按惯例）。
- 全栈禁 `auto`（全 diff 核验）；C++17。
- 既有调用方（CLI/AppController/跨服务）编译且行为不变（新尾参默认 nullptr）。
- 排除项保留：`bin/config.json`、`docs/specs/2026-11-18-Spec-VMDebuggingSetup-v1.0.md`（勿触碰）。

## 9. 明确不做（范围外）

- 不改 UI 任务列表 `提交于` 列格式（仍 `HH:MM:SS`）。
- 不改进程历史 `durationMs`（ProcessInspector/ProxyRuntimeHistory，与报告无关）。
- 不新增订阅更新方式（仅报告字段，已与 owner 澄清）。
- 不清理报告索引无界增长（2026-11-24 Spec §10 已定范围外）。
- **AUTO_TASK 容器不设置 `updateMethods`**：其 report 失去内嵌 UPDATE_ALL 步骤的方法信息（log excerpt 移除后无可携带）；owner Req3 范围仅 UPDATE_ALL/UPDATE_SUBSCRIPTION，此为明示限制而非缺陷。
