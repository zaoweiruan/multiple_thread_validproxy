# 2026-10-20 Bugfix — 日志级别口径（`Network connection RESTORED` / `AutoTask: cancelled`）

> **类型**：Bugfix（日志卫生）　**范围**：2 处日志级别常量，零控制流改动
> **来源**：`docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md` §16 遗留问题第 3 项
> **owner 指令（2026-10-20）**：`Network connection RESTORED` → **info**；`AutoTask: cancelled` → **report**

---

## 1. 背景与现象

V10 现场复跑日志 `bin/worker/log/ui_20261020_092411.log`（2026-10-20 09:24:11–09:51:18）在批量测试被整批取消后出现：

```
[2026-10-20 09:40:27] [ERROR] AutoTask: cancelled
[2026-10-20 09:40:27] [ERROR] AutoTask failed
```

网络恢复时出现：

```
[2026-10-20 09:41:59] [ERROR] Network connection RESTORED
[2026-10-20 09:42:31] [ERROR] Network connection RESTORED
```

**「网络恢复」与「任务取消」都不是错误**，却记为 `ERROR` 级。后果：

1. 生产 `log.file_level` 常配 `WARN`/`ERR` 时，这两类**正常生命周期事件**会污染错误计数与告警；
2. 日志统计（LogPanel「日志统计」）会把正常事件计入 ERROR 原因明细，误导排障；
3. 真正的失败（`AutoTask failed`）被同级的正常事件稀释。

---

## 2. 根因与关键辨析

两处 `LogLevel` 赋值与其语义不符。**但同一文案族存在多处不同语义的日志，必须逐点区分**，否则会改错地方：

| # | 位置 | 文案 | 当前级别 | 语义 | 处置 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **A** | `src/NetworkMonitor.cpp:153` | `Network connection RESTORED` | `ERR` | 网络恢复（正常事件） | **→ INFO** |
| **B** | `src/AutoTaskManager.cpp:62-63` | `AutoTask: cancelled`（由三元表达式在 `state_.cancelled==true` 时产生） | `ERR` | 任务被取消（正常事件） | **→ REPORT** |
| C | `src/AutoTaskManager.cpp:120` | `AutoTask: cancelled at step N` | `REPORT` | 同一语义 | 不改（已正确） |
| D | `src/AutoTaskManager.cpp:126-127` | `AutoTask: aborted at step N [..] - network disconnected` | `ERR` | 断网中止 | 不改（属失败） |
| E | `src/ui/AppController.cpp:2834` | `AutoTask failed` | `ERR` | `run()` 返回 false 的汇总量 | 不改（真实失败；取消时同样会打，属既有口径，另行评估） |
| F | `src/NetworkMonitor.cpp:145` / `:156` | `Network connection LOST (consecutive failures: N)` | `ERR` | 断网 | 不改（本次未要求） |

> **关键辨析（否则必然改错）**：现场日志中的 `AutoTask: cancelled` **不带** `at step`，因此它来自 **B（`AutoTaskManager::run()` 尾部的三元表达式）**，而**不是** C（`runSteps()` 内的 `cancelled at step N`）。C 早已是 `REPORT`；若只改 C，现场问题一点没变。这也是本条遗留问题最初被误记为「全部 ERROR」的原因——存在两个同名来源。

---

## 3. 变更点

### 3.1 `src/NetworkMonitor.cpp:153`

```cpp
Logger::write("Network connection RESTORED", LogLevel::ERR);
```

改为：

```cpp
Logger::write("Network connection RESTORED", LogLevel::INFO);
```

### 3.2 `src/AutoTaskManager.cpp:62-63`

现状：

```cpp
    Logger::write("AutoTask: " + std::string(result ? "completed" : (state_.cancelled ? "cancelled" : "failed")),
        result ? LogLevel::REPORT : LogLevel::ERR);
```

改成「取消也算正常结局，只有真失败才 ERR」：

```cpp
    // A cancellation (user stop, or a whole-batch network stop) is a normal
    // outcome, not a failure — report it at REPORT and keep ERR for a real
    // failure. Note this is the summary line in run(); runSteps() already
    // reports its own "cancelled at step N" at REPORT.
    const bool cancelledOutcome = !result && state_.cancelled;
    Logger::write("AutoTask: " + std::string(result ? "completed" : (state_.cancelled ? "cancelled" : "failed")),
        (result || cancelledOutcome) ? LogLevel::REPORT : LogLevel::ERR);
```

> 显式 `const bool`，遵守项目禁令（**禁止 `auto` 类型推导**）。

---

## 4. 影响面与回滚

| 项 | 说明 |
| :--- | :--- |
| 控制流 | **无**。仅日志级别常量与一个局部 bool |
| 返回值 / UI | **无**。`run()` 返回值、状态文件、状态栏文案均不变 |
| 日志可见性 | `log.file_level=ERROR` 时这两条不再出现（**这正是目的**） |
| 既有断言 | 无。`tests/` 全仓 grep `Network connection RESTORED` / `AutoTask: cancelled` / `AutoTask: completed` → **0 命中**，无测试依赖其文案或级别 |
| 回滚 | 两处常量改回 `LogLevel::ERR`（并删去 `cancelledOutcome`）即可 |

---

## 5. 验证计划

| # | 方法 | 期望 |
| :--- | :--- | :--- |
| V1 | `Select-String -Path src/NetworkMonitor.cpp -Pattern 'Network connection RESTORED'` | 该行为 `LogLevel::INFO` |
| V2 | `Select-String -Path src/AutoTaskManager.cpp -Pattern 'cancelledOutcome'` | 存在；cancelled 分支走 `REPORT`，`failed` 分支仍走 `ERR` |
| V3 | `cmake --build build --parallel 8` | EXIT 0 |
| V4 | `ctest --test-dir build` | 与基线一致 **52/54**（`XrayManagerStartupTest` 既有基线失败；`UI_FLOATINGWIDGET` 为 `docs/context.md` 记载的并行 flaky） |

### 5.1 新增回归单测（`tests/test_autotask.cpp`）

项目**已有**日志捕获设施：`Logger::pushCallback` / `Logger::popCallback`（`include/Logger.h:37-40`）与 `LogCapture`（`tests/test_utils.h:52`，按 `{message, level}` 记录，且 `LoggerInstance::write` 的 `logCallback_` 与是否 `init` 无关，无需先 init 文件日志）。据此新增：

`TEST(AutoTaskLogLevelTest, CancelledSummaryIsReportNotError)`

- 可行性关键：`cancel()` 置的是 `cancelRequested_`（**独立 atomic**），而 `run()` 只重置 `state_.cancelled`、**不**重置它 ⇒ `run({"update"})` 的首次 `isCancelled()` 即命中，在触碰数据库**之前**退出 ⇒ 用 `nullptr` db 也能确定性复现，无网络/DB/时序依赖；
- 断言：捕获到的 `AutoTask: cancelled` 条目其 `level == LogLevel::REPORT`。

**RED → GREEN 实证**：把该行临时改回 `result ? REPORT : ERR`，测试 **FAIL** 且逐字复现现场日志签名 `[ERROR] AutoTask: cancelled`；恢复后 **GREEN**。

### 5.2 `NetworkMonitor` `RESTORED` 行未加单测（说明）

该行要触发需驱动「首检连通 → 断网 → 恢复」三阶段状态机迁移，属时序测试；本项目 UI/网络测试已有 flaky 史（见 `docs/context.md` 「并行 flaky」记录），**引入时序测试的维护成本高于收益**。故该行改由静态核对 V1 + 全量回归覆盖，并在 §6 记录取舍。

---

## 6. 实施状态

- [x] 文档先行（本文件）
- [x] A `NetworkMonitor.cpp:153` → `LogLevel::INFO`（现 `:155`）
- [x] B `AutoTaskManager.cpp:62-63` → 取消记 `REPORT`，失败仍 `ERR`（现 `:67-69`）
- [x] 新增单测 `AutoTaskLogLevelTest.CancelledSummaryIsReportNotError`，**RED→GREEN 已实证**（RED 时复现 `[ERROR] AutoTask: cancelled`）
- [x] 构建 EXIT 0
- [x] 静态核对 V1/V2 通过
- [x] 全量回归 `ctest --test-dir build` → **53/54**（唯一失败为既有基线 `XrayManagerStartupTest`；`UI_FLOATINGWIDGET` 本轮通过，再次印证其 flaky 性质，与本改动无关）
