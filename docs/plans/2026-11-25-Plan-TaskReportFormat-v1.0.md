# Plan — 任务完成报告格式 v2（时间/时长/去日志摘录/Update methods）

- 日期：2026-11-25
- 类型：Plan（实现计划）
- 模块：任务报告格式 v2
- 版本：v1.0
- 来源 Spec：`docs/specs/2026-11-25-Spec-TaskReportFormat-v1.0.md`
- 基线分支：`feat/task-report-format`（自 `main`@`993dd59`，2026-11-25 创建）

## 任务划分（每任务构建绿、可独立评审）

### Task 1: TaskFormat 纯函数（wallClockDateTime + durationMinutesBetween）

**Files:**
- Modify: `include/task/TaskFormat.h`（声明两函数）
- Modify: `src/task/TaskFormat.cpp`（实现，复用解析核心）
- Test: `tests/test_task_format.cpp`（增补用例）

**TDD Steps:**
1. 写失败测试：`wallClockDateTime`（合法 tick→`yyyy-mm-dd hh:mm:ss`；空/非数字/尾部杂讯→空串）；`durationMinutesBetween`（合法→floor 分钟；任一无效→-1）。
2. `ctest --test-dir build -R TaskFormatTest -V` → Expected FAIL（未定义）。
3. 实现两函数；`wallClockText` 不动。
4. `ctest --test-dir build -R TaskFormatTest -V` → Expected PASS。
5. Commit：`feat(task-report-format): TaskFormat 墙钟 yyyy-mm-dd hh:mm:ss + durationMinutes 纯函数`。

### Task 2: 报告格式重构（原子——TaskReportService + TaskQueue + SubitemUpdaterV2 + AppController 签名链）

**Files:**
- Modify: `include/task/TaskReportService.h`（移除 `LogLine`；`formatReport(handle, stats, updateMethods)`、`publish(handle, stats, updateMethods)`）
- Modify: `src/task/TaskReportService.cpp`（时间→墙钟、`durationMs`→`durationMinutes`、移除 log excerpt、增 updateMethods；移除 `parseLong` 改用 `durationMinutesBetween`；含 `<ctime>`/TaskFormat.h）
- Modify: `include/task/TaskQueue.h`（`TaskContext` 增 `std::string updateMethods`；`TaskFinishedSink` 改 `(handle, stats, updateMethods)`；移除 `#include TaskReportService.h`）
- Modify: `src/task/TaskQueue.cpp`（移除日志捕获 pushCallback/popCallback/capturedLines；sink 调 `taskFinishedSink_(entry.handle, ctx.stats, ctx.updateMethods)`）
- Modify: `include/SubitemUpdaterV2.h`（`run`/`runSingle` 追加 `std::string* updateMethods = nullptr`）
- Modify: `src/SubitemUpdaterV2.cpp`（run/runSingle 在方法字符串构建后、任何早返回前填充 updateMethods；提取单一文件内 `static std::string methodName(UpdateMethod)` 助手，替换 run methodsLog/run 循环/runSingleWithProxy 3 处 switch 并在 runSingle 新增方法字符串构建）
- Modify: `src/ui/AppController.h`（`doUpdateSubscription`/`doUpdateAllSubscriptions` 追加尾参 `std::string* updateMethods = nullptr`）
- Modify: `src/ui/AppController.cpp`（sink 三参；UPDATE_ALL/UPDATE_SUBSCRIPTION lambda → `doUpdate*`(`&methods`) → `runSingle(subId,&local,&methods)`/`run(&local,&methods)` → `ctx.addStats(local); ctx.updateMethods = methods;`；移除不再需要的 lines 变量）
- Test: `tests/test_task_report_service.cpp`（墙钟合法格式（正则/可解析）+ `durationMinutes` + **无 `log excerpt`** + `updateMethods` 输出省略；**删除** `:25 FormatReportContainsMetadataStatsAndLogLines` 的 `:35 r.find("batch done")` 断言与 `:30` 旧三参调用，改为新签名与无 excerpt 断言）
- Test: `tests/test_task_queue_report.cpp`（sink 三参 + ctx.updateMethods 传至 sink；无 lines）
- Test: `tests/test_task_report_integration.cpp`（无 log excerpt；签名更新；`success: 9` 断言保留）
- Test: `tests/test_task_stats_exposure.cpp`（run/runSingle updateMethods 出参填充，可空）

**关键（原子性，保构建绿）：** TaskReportService::publish 签名、TaskQueue sink 签名、AppController sink 注册必须在**同一提交**改——任何中间态都会断链。**签名链完整清单**：`publish` → `TaskFinishedSink` → AppController sink lambda →（UPDATE 路径）`doUpdateSubscription`/`doUpdateAllSubscriptions`（AppController.h:327-328 / .cpp:3354/3388）→ `SubitemUpdaterV2::runSingle`/`run`——`doUpdate*` 两 helper 须**同时**加尾参，否则 `&methods` 无法穿透。SubitemUpdaterV2::run/runSingle 与 `doUpdate*` 尾参默认 `nullptr`，既有调用方（只传 stats）行为不变。

**TDD Steps:**
1. 写失败测试（上述测试文件更新：断言新格式/无 excerpt/updateMethods；旧断言删除——尤其 `test_task_report_service.cpp:30` 旧三参 `formatReport(h, lines, st)` 与 `:35` `r.find("batch done")` 为编译/断言双断点）。
2. `ctest --test-dir build -R "TaskReport|TaskQueueReport|TaskStatsExposure|TaskFormat" -V` → Expected FAIL。
3. 实现（逐文件，遵循 §6 接口表；含中日韩文本改动用 `edit`）。
4. 同上测试 → Expected PASS；全量 `cmake --build build --parallel 6`（验证既有调用方编译）。
5. Commit：`feat(task-report-format): 报告时间墙钟+时长分钟+去日志摘录+Update methods 字段`。

### Task 3: 文档登记 + 全量门禁

**Files:**
- Modify: `docs/INDEX.md`（登记本 Spec + Plan）
- Modify: `docs/plans/project-plans-tracker.md`（登记本计划条目）

**Steps:**
1. 登记两文档（只加行）。
2. `scripts\build-and-test.bat` → ALL TESTS PASSED（UI_FLOATINGWIDGET 已知 flaky 单跑复验）。
3. `ctest --test-dir build -V` → 全绿（既有环境基线/flaky 例外按惯例）。
4. Commit（**pathspec**）：`docs: 登记任务报告格式 v2 Spec/计划` —— 仅 `docs/INDEX.md docs/plans/project-plans-tracker.md`，禁裸 commit（`docs/specs/2026-11-18-...` 仍暂存）。

## 全局约束（绑定）

- C++17；**全栈禁 auto**；TDD（`TEST_F`）；构建 `cmake --build build --parallel 6`。
- 含中日韩文本改动用精确 `edit`，**禁 clion apply_patch**。
- 排除项勿触碰：`bin/config.json`（=HEAD）、`docs/specs/2026-11-18-Spec-VMDebuggingSetup-v1.0.md`（仍暂存，勿 unstage/改内容/卷入提交）。
- `wallClockText`（UI 列表 `HH:MM:SS`）不动，其现有测试（test_task_format.cpp:68-87）必须保持绿；i18n 表不动。
- 不改进程历史 `durationMs`；不新增订阅更新方式；AUTO_TASK 容器不设 `updateMethods`（明示限制）。

## 自审

- **R1**：Task 1/2 时间→墙钟 `yyyy-mm-dd hh:mm:ss`、`durationMinutes` floor 分钟。
- **R2**：Task 2 移除 `---- log excerpt ----` 节 + 捕获链路（sink 三参、`LogLine` 移除）。
- **R3**：Task 2 `updateMethods` 字段（TaskContext + run/runSingle 出参 + AppController 穿线 + 非空才输出）。
- **类型一致**：`wallClockDateTime`/`durationMinutesBetween` 签名与 `TaskFormat.h` 一致；`TaskFinishedSink` 三参全链路一致。
- **向后兼容**：run/runSingle 尾参默认 nullptr；既有调用编译。
