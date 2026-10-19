# Bugfix: ProxyBatchTester Worker 线程停摆、网络中断整批取消与相关次要缺陷

> **版本**：v2.2（2026-10-19）— **✅ 全部 9 个 Task 实施完成**

> [!IMPORTANT]
> **实施结果**：`ctest` **53/54**（唯一失败为既有基线 `XrayManagerStartupTest`）；`BatchWorkerPolicyTest` 11/11、`NetworkMonitorTest` 15/15。58 个 Step 全部勾选。三处实施偏差与一处既有 flake 记录见 **§15 v2.2**。
>
> **v2.0 核心变更（相对 v1.0）：**
> 1. **D1 改为「首个 worker 等满 30s 未恢复 → 置应用级取消 → 整批停止」**（ERR 日志），取代原先「worker 各自静默退出」。
> 2. **D2 改为「移除」** —— 取消整批的触发源唯一化（只剩 worker 30s 超时）；删除 `cancelOnDisconnect` 机制与 `probe_on_disconnect.max_probes` 配置项。
> 3. **新增 F7** —— `ProxyFinder` / `RegionBatchResolver` 补 `NetworkMonitor*` 形参与断网检查，替代其因移除 D2 而失去的断网中止能力。
> 4. D3 保留 `bool` 兼容包装；D4 / D5 / F5 / F6 不变。
>
> **v2.1 新增：**
> 5. **新增 F8（抖动防护）** —— `NetworkMonitor` 连通判定由「全部 `check_urls` 成功」改为**多数投票**（纯静态 `majorityVerdict()` 可单测）；DNS 失败仍为中性票，全弃权时保持旧语义「连通」。唯一行为变化：「2 成功 + 1 传输失败」由断网变连通。
>
> 裁决与风险见 **§13 / §13.1 / §13.2**。v1.0.2 曾把 Task 2/3/4 标为作废，**该作废已被 v2.0 推翻**——三个 Task 均需实施，只是内容已改写。

> **版本**：v1.0（2026-10-19）
> **上游依据（根因）**：`docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md`
> **独立评审**：`docs/reviews/2026-10-19-Review-ProxyBatchTester-WorkerThreadStallFix-v1.0.md`
> **现场证据日志**：`bin/log/ui_20261019_083638.log`（426,396 B / 4,469 行 / 2026-10-19 08:54:02–10:34:23）
> **For agentic workers:** REQUIRED SUB-SKILL: 逐 Task 执行本方案（可用 `subagent-driven-development` 或 `executing-plans`）。步骤以 `- [ ]` 勾选追踪，必须按顺序执行并逐步验证。**执行前先读上方 WARNING 横幅，跳过已作废的 Task 2/3/4。**

**Goal:** 修掉「测试后期只剩 1 个 worker 线程在跑」的根因 —— 网络等待 30s 超时后**不再让 worker 各自静默退出**，而是**触发整批干净停止**（与 owner 政策「长断网应停摆任务」一致）；同时消除同批发现的 5 个次要缺陷（静默线程退出语义含混 D3、relaunch 丢数据库参数 D4、探活 WARN 风暴 D5、proxy tag 残留误触发实例重启 F5、日志字段可读性 F6）。

**Architecture:** 把 worker 生命周期判定从 `ProxyBatchTester` 的线程体内**抽取为纯函数策略**（新头文件 `include/BatchWorkerPolicy.h`，零依赖），生产代码与单测共用同一份策略（DRY，避免「测试复刻 → 与生产漂移」）；`XrayManager` 的三态健康结果改成显式枚举；`NetworkMonitor` 收敛为**纯探测器**（只报 `IsConnected()`，不再反向置位任何取消标志），取消整批的触发源唯一化为 worker 的 30s 有界等待超时；其余为单点最小修补。

**Tech Stack:** C++17（**全程禁止 `auto` 类型推导**）、CMake + Ninja、GoogleTest（`tests/`）、PowerShell 7 命令行。

---

## §0 问题清单与修复索引

本方案覆盖上游 RCA 文档 §5 列出的 **全部** 问题，并新增一项在写方案时发现的同源缺陷（D1b）。**不含**订阅解析（`SubscriptionParser` P0-A/P0-B/P0-C）一线的问题——那条线属另一份方案 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md`。

| ID | 问题 | 严重度 | 证据 | 修复 Task |
| :--- | :--- | :--- | :--- | :--- |
| **D1** | `workerThreadFunc` 主循环把 `waitForNetworkRecovery()` 的 `false`（30s 超时）当作终止条件 → worker 永久退出 | **Critical** | 日志 10:16:34 `3 × network wait timed out after 30s — exiting`；10:16:35 网络恢复仅 `1 × resuming`；此后 8 个时间点 pausing/resuming 恒 1 条 | Task 2（**v2.0：改为首超时即整批停止**） |
| **D1b** | `waitInterruptible()` 同样把「网络等待超时」与「取消」混为 `true`，其调用点 `if (waitInterruptible(20)) return;` / `if (waitInterruptible(backoffMs)) return;` 会**直接 return 掉整个 worker** | **Critical**（同源） | `src/ProxyBatchTester.cpp:222`、`:265`；`waitInterruptible` 内部 `:489 if (!waitForNetworkRecovery()) return true;` | Task 3（**v2.0：超时/取消分离，超时亦整批停止**） |
| **D2** | `NetworkMonitor` 连续 3 次探测失败即 `cancelOnDisconnect` → 置应用级 `cancelRequested_` → 4 个 worker 全部退出 + 整批任务中止 | **Critical** | 日志 09:28:59 / 10:11:13 各 `4 × network wait cancelled — exiting` + `AutoTask: cancelled` + `AutoTask failed` | Task 4（**v2.0：移除该机制，单一触发源**） |
| **F7** | 移除 D2 后 `ProxyFinder` / `RegionBatchResolver`（不收 `netMon_`）失去断网自动中止 | Medium（v2.0 新增） | 构造函数仅收 `cancelFlag`，断网时唯一依赖 D2 置的应用级标志 | Task 4 子步骤 |
| **F8** | `NetworkMonitor` 要求全部 `check_urls` 成功才判连通，任意一个抖动即误判断网，且 `break` 使后续 URL 未被探测 | Medium（v2.1 新增） | `NetworkMonitor.cpp:96-118`；使 30s 停批门槛的实际到达难度远低于名义值 | Task 4 Step 2b/2c |
| **D3** | `:155-157` `else if (instanceDead.load()) break;` 静默退出（零日志）；且 `evaluateInstanceHealthThrottled()` 把「冷却命中」与「重启真失败」都返回 `false` | **High** | `src/XrayManager.cpp:292 return false; // within the 30s cooldown` vs `:271 return false;`（relaunch FAILED） | Task 5 |
| **D4** | relaunch 构造 `XrayInstance` 时**漏传 `db_`** → 走 `db_ == nullptr` → 生成最小配置（无 outbound/路由）→ 重启窗口内必然连续注入失败 | **High** | `src/XrayManager.cpp:256` 未传第 5 参 vs 正常路径 `:144` 传了 `db_`；日志 14 条 `[XrayInstance] No database connection - generating minimal config` | Task 6 |
| **D5** | `[OnlineProbe]` 阈值判据用 `>=` 而非 `==` → 每次探活周期（~5s）重复打出同一条 WARN | **Medium** | `src/ui/AppController.cpp:2310` 与 `:2332`；日志中同一 indexId 每 5s 一条、持续 3 分钟（10:11:34–10:14:15） | Task 7 |
| **F5** | proxy tag 残留（`existing tag found: proxy`）：`removeOutbound` 失败仅 DEBUG 不可见；注入失败被误判为「实例不可达」→ 触发不必要的实例重启 | **Medium** | 日志 10:34:09–10:34:11 `output= adding: proxy` / `existing tag found: proxy`；`src/XrayApi.cpp:250` 仅 DEBUG | Task 8 |
| **F6** | 日志字段 `byProto=` 实为「被丢弃行按协议分布」，极易误读为「成功按协议」 | **Low**（可读性） | `src/update/SubscriptionParser.cpp:807-810`（数组名已是 `stats.droppedByProtocol[]`）；**跨文档收口**：本 Task 同时关闭 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` 的 H6（评审 B8） | Task 9 |

**行为语义变更（`AGENTS.md §6.2` 定夺结果，见 §13）**：D1 / D1b / D2 会改变「网络长时间中断时任务是否自动终止、谁触发终止」的行为契约。
**owner 于 2026-10-19 全部裁决（v2.0）**：D1/D1b = **首个 worker 超时即整批停止**；D2 = **移除**，取消整批的触发源唯一化；新增 F7 补 `ProxyFinder` / `RegionBatchResolver` 的断网中止。**9 个 Task 全部实施。** 新引入的行为契约与风险见 **§13.2**。

---

## §1 修复总览（文件级）

| 文件 | 动作 | 关联 |
| :--- | :--- | :--- |
| `include/BatchWorkerPolicy.h` | **新建**（纯策略，零依赖；三组：D3 三态 / F5 stale-tag / D5 探活阈值） | Task 1 |
| `tests/test_batch_worker_policy.cpp` | **新建**（**9 个用例**） | Task 1 |
| `CMakeLists.txt` | 追加测试目标注册 | Task 1 |
| `src/ProxyBatchTester.cpp` | `include`；主循环 D1（首超时整批停止）；`waitInterruptible` D1b；`instanceDead` 分支 D3；stale-tag 门 F5 | Task 2/3/5/8 |
| `src/XrayManager.cpp` | D3 三态实现；D4 补 `db_` | Task 5/6 |
| `include/XrayManager.h` | D3 三态声明 + `include "BatchWorkerPolicy.h"` | Task 5 |
| `src/NetworkMonitor.cpp` | **删除** `cancelOnDisconnect_` 置位与 `probe_on_disconnect` 判定（探测/状态机不变）；**F8**：探测循环去 `break` + `allOk` 改多数投票 | Task 4 |
| `include/NetworkMonitor.h` | **删除** `setCancelOnDisconnect` / `cancelOnDisconnect_` / `setProbeOnDisconnect` / `maxProbes_` / `probeEnabled_`；**F8**：新增纯静态 `majorityVerdict()` | Task 4 |
| `include/ConfigReader.h` + `include/config/sections/NetworkMonitorConfigParser.h` + `src/config/ConfigJsonSerializer.cpp` | **删除** `probe_on_disconnect.max_probes` 的字段 / 解析 / 序列化 | Task 4 |
| `src/ui/ConfigDialog.cpp` | **删除**「最大探测次数」属性（`:242`/`:457`/`:564-566`） | Task 4 |
| `bin/config.json` | **删除** `"probe_on_disconnect"` 段 | Task 4 |
| `include/ProxyFinder.h` + `src/ProxyFinder.cpp` | **F7**：新增 `NetworkMonitor* netMon` 末位形参 + 主循环断网检查 | Task 4 |
| `include/RegionBatchResolver.h` + `src/RegionBatchResolver.cpp` | **F7**：同上 | Task 4 |
| `src/ui/AppController.cpp` | D5 阈值判据改「仅跨过时打」；**F7**：3 处构造调用传 `&netMon_`；**Task 4**：删 `setCancelOnDisconnect` / `setProbeOnDisconnect` 调用 | Task 4/7 |
| `src/XrayApi.cpp` | F5 `removeOutbound` 失败升 WARN | Task 8 |
| `src/update/SubscriptionParser.cpp` | F6 字面串改名 | Task 9 |

---

## §2 Task 1：建立可单测的 worker 生命周期策略

**Files:**
- Create: `include/BatchWorkerPolicy.h`
- Create: `tests/test_batch_worker_policy.cpp`
- Modify: `CMakeLists.txt`（在 `:985` 之后插 11 行）

- [x] **Step 1: 先写失败的测试文件 `tests/test_batch_worker_policy.cpp`**

```cpp
// Unit tests for include/BatchWorkerPolicy.h.
//
// Bugfix lineage: docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md
// Root-cause RCA:  docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md
//
// CMake registration (mirrors the "ProxyBatchTester components tests" block):
//   add_executable(test_batch_worker_policy tests/test_batch_worker_policy.cpp)
//   target_include_directories(test_batch_worker_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
//   target_link_libraries(test_batch_worker_policy PRIVATE gtest_main gtest)
//   add_test(NAME BatchWorkerPolicyTest COMMAND test_batch_worker_policy)

#include "BatchWorkerPolicy.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using batch_policy::InstanceDeadDecision;
using batch_policy::InstanceHealthOutcome;
}  // namespace

// --- D3: distinguish "cooldown hit" from "relaunch actually failed" --------

TEST(BatchWorkerPolicyTest, InstanceThrottled_ClearsFlagAndKeepsServing) {
    EXPECT_EQ(batch_policy::decideInstanceDead(InstanceHealthOutcome::Throttled),
              InstanceDeadDecision::ClearAndContinueThrottled);
}

TEST(BatchWorkerPolicyTest, InstanceRelaunched_ClearsFlagAndKeepsServing) {
    EXPECT_EQ(batch_policy::decideInstanceDead(InstanceHealthOutcome::Relaunched),
              InstanceDeadDecision::ClearAndContinueRelaunched);
}

TEST(BatchWorkerPolicyTest, InstanceRelaunchFailed_StopsWorker) {
    EXPECT_EQ(batch_policy::decideInstanceDead(InstanceHealthOutcome::RelaunchFailed),
              InstanceDeadDecision::StopWorkerAndLog);
}

// --- F5: a leftover proxy tag is not an instance-health failure ------------

TEST(BatchWorkerPolicyTest, StaleTagError_NotReportedAsEndpointUnreachable) {
    const std::string err =
        "rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: proxy";
    EXPECT_TRUE(batch_policy::isStaleTagInjectionError(err));
    EXPECT_FALSE(batch_policy::shouldReportEndpointUnreachable(err, 3));
    EXPECT_FALSE(batch_policy::shouldReportEndpointUnreachable(err, 5));
}

TEST(BatchWorkerPolicyTest, GenericInjectionError_ReportedAfterThreeRetries) {
    const std::string err = "xray api ado failed with code: 1 output:";
    EXPECT_FALSE(batch_policy::isStaleTagInjectionError(err));
    EXPECT_FALSE(batch_policy::shouldReportEndpointUnreachable(err, 2));
    EXPECT_TRUE(batch_policy::shouldReportEndpointUnreachable(err, 3));
}

// --- D5: probe-failure WARN only when the streak first crosses threshold ---

TEST(BatchWorkerPolicyTest, ProbeFailureBelowThreshold_NoLog) {
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(0, 3));
}

TEST(BatchWorkerPolicyTest, ProbeFailureCrossing_LogsOnce) {
    EXPECT_TRUE(batch_policy::shouldLogProbeFailureCrossing(2, 3));
}

TEST(BatchWorkerPolicyTest, ProbeFailureAlreadyPastThreshold_NoRepeatLog) {
    // Regression for the [OnlineProbe] storm: ~36 identical WARN lines for the same
    // indexId over 3 minutes because the check used ">=" instead of "==".
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(3, 3));
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(9, 3));
}

TEST(BatchWorkerPolicyTest, ProbeFailureDisabledThreshold_NoLog) {
    EXPECT_FALSE(batch_policy::shouldLogProbeFailureCrossing(-1, 0));
}
```

- [x] **Step 2: 注册测试目标（此时必然编译失败：`BatchWorkerPolicy.h` 尚不存在）**

在 `CMakeLists.txt` 中，紧接 `:985` 这一行之后插入：

```cmake
# -- Batch worker lifecycle policy tests (pure logic, no external deps) --
add_executable(test_batch_worker_policy tests/test_batch_worker_policy.cpp)
target_include_directories(test_batch_worker_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_batch_worker_policy PRIVATE gtest_main gtest)
set_target_properties(test_batch_worker_policy PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/tests
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/tests
)
add_test(NAME BatchWorkerPolicyTest COMMAND test_batch_worker_policy)
```

锚点行原文（`CMakeLists.txt:985`）：

```
add_test(NAME ProxyBatchComponentsTest COMMAND test_proxy_batch_components)
```

Run:

```powershell
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 8 --target test_batch_worker_policy
```

Expected: **FAIL** — `fatal error: BatchWorkerPolicy.h: No such file or directory`。

- [x] **Step 3: 创建 `include/BatchWorkerPolicy.h`**

```cpp
#pragma once

// Batch worker lifecycle policy — pure, dependency-free helpers extracted from
// ProxyBatchTester so the worker-control decisions are unit-testable without
// Xray, network access or SQLite.
//
// Bugfix lineage: docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md
// Root-cause RCA: docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md
// Contract mirror: tests/test_batch_worker_policy.cpp
//
// NOTE: this project forbids `auto` type deduction — keep explicit types here.

#include <string>

namespace batch_policy {

// ---------------------------------------------------------------------------
// D3: three-state outcome of a throttled instance-health evaluation
// ---------------------------------------------------------------------------
enum class InstanceHealthOutcome {
    Throttled,       // inside the 30s per-apiPort cooldown: another reporter already ran
    Relaunched,      // the instance was restarted successfully and is ready
    RelaunchFailed   // a restart was attempted and failed; its ports were freed
};

enum class InstanceDeadDecision {
    ClearAndContinueRelaunched,  // instance relaunched -> clear the flag, keep serving
    ClearAndContinueThrottled,   // cooldown hit -> clear the flag, keep serving (no restart needed)
    StopWorkerAndLog             // relaunch really failed -> stop this worker, with a log line
};

inline InstanceDeadDecision decideInstanceDead(InstanceHealthOutcome outcome) {
    if (outcome == InstanceHealthOutcome::Relaunched) {
        return InstanceDeadDecision::ClearAndContinueRelaunched;
    }
    if (outcome == InstanceHealthOutcome::Throttled) {
        return InstanceDeadDecision::ClearAndContinueThrottled;
    }
    return InstanceDeadDecision::StopWorkerAndLog;
}

// ---------------------------------------------------------------------------
// F5: a leftover/duplicate proxy tag is NOT an instance-health failure
// ---------------------------------------------------------------------------
inline bool isStaleTagInjectionError(const std::string& error) {
    return error.find("existing tag found") != std::string::npos;
}

// retryCount: injection attempts already consumed (3 == the retry loop is exhausted)
inline bool shouldReportEndpointUnreachable(const std::string& lastInjectionError, int retryCount) {
    if (retryCount < 3) {
        return false;
    }
    if (isStaleTagInjectionError(lastInjectionError)) {
        return false;  // the instance is healthy; a leftover tag caused the failure
    }
    return true;
}

// ---------------------------------------------------------------------------
// D5: log a per-monitor probe failure only when the streak first crosses it
// ---------------------------------------------------------------------------
// priorConsecutiveFailures: consecutive_failures read from the DB BEFORE this bump
inline bool shouldLogProbeFailureCrossing(int priorConsecutiveFailures, int probeFailStreak) {
    if (probeFailStreak <= 0) {
        return false;
    }
    return priorConsecutiveFailures + 1 == probeFailStreak;
}

}  // namespace batch_policy
```

- [x] **Step 4: 编译并运行，确认 9 个用例全绿**

Run:

```powershell
cmake --build build --parallel 8 --target test_batch_worker_policy
ctest -R BatchWorkerPolicyTest -V
```

Expected: `100% tests passed, 0 tests failed out of 1`，测试二进制内 **13 tests from BatchWorkerPolicyTest** 全 `OK`。

- [x] **Step 5: 提交**

```powershell
git add include/BatchWorkerPolicy.h tests/test_batch_worker_policy.cpp CMakeLists.txt
git commit -m "test(batch): extract pure worker lifecycle policy + unit tests

Adds include/BatchWorkerPolicy.h with dependency-free decision helpers for
network-wait continuation (D1), instance-dead tri-state (D3), cancel debounce
(D2), stale proxy tag classification (F5) and probe-failure log throttling (D5),
plus tests/test_batch_worker_policy.cpp mirroring the contract."
```

---

## §3 Task 2：D1（Critical）— 首个 worker 有界等待超时即整批停止

**Files:**
- Modify: `include/ProxyBatchTester.h`（`:64` 附近新增私有方法声明 `requestBatchCancellation`）
- Modify: `src/ProxyBatchTester.cpp`（`:158-161` 主循环；文件末尾新增该方法实现）

**根因**：`workerThreadFunc` 主循环把 `waitForNetworkRecovery()` 的 `false` 当终止条件，而该 `false` 同时承载「已取消」与「30s 有界等待耗尽」两种语义。4 个 worker 各自独立计时、各自退出，于是 2026-10-19 出现「3 死 1 活，批次带着 1/4 并发继续跑完 16 分钟」的残废状态，而非干净的整批停止。

**owner 政策（§13 决策 1，2026-10-19）**：「网络恢复需要较长时间，所以在排除抖动原因后，应该停摆任务。」即真实断网**应当停止整批任务**；缺陷不在「停不停」，而在「怎么停」——必须是**整批一起停 + 有据可查**，不能是各 worker 静默退出。

**目标行为**：首个 worker 耗尽 30s 有界等待且期间未恢复时，置应用级取消标志使**全部** worker 退出，并打一条 ERR 日志说明是哪个 worker、哪个 api 端口、什么原因。

- [x] **Step 1: 头文件新增私有方法声明**

锚点（`include/ProxyBatchTester.h:62-68`，紧随 `waitInterruptible` 声明之后）：

```cpp
    bool waitInterruptible(int totalMs);
```

在其后追加一行：

```cpp
    /// D1 (2026-10-19): raise a batch-wide cancellation because `reason` left this
    /// worker unable to make progress. Called only when a bounded network wait is
    /// exhausted (never on user cancellation, which exits quietly). Setting the
    /// external flag stops sibling workers and the owning operation, so the batch
    /// ends as a whole instead of degrading to 1/N concurrency.
    void requestBatchCancellation(int workerId, int apiPort, const std::string& reason);
```

- [x] **Step 2: 实现 `requestBatchCancellation`**

锚点（`src/ProxyBatchTester.cpp` 文件末尾，`preGenerateConfigs` 定义之前或之后均可）：

```cpp
void ProxyBatchTester::requestBatchCancellation(int workerId, int apiPort,
                                                const std::string& reason) {
    Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId) +
                  " (api=" + std::to_string(apiPort) + "): " + reason +
                  " — requesting whole-batch cancellation",
                  LogLevel::ERR);
    // externalCancel_ is the owning operation's flag (AppController::cancelRequested_
    // in GUI/CLI). Without it the batch would only lose THIS worker.
    if (externalCancel_) {
        externalCancel_->store(true);
    }
    cancelRequested_.store(true);
    // Notified without holding the mutex: both CVs use timed waits with an atomic
    // predicate, so this is a wake-up optimisation, never a correctness requirement.
    cancelCv_.notify_all();
    networkCv_.notify_all();
}
```

- [x] **Step 3: 替换主循环中的网络等待分支**

锚点（`src/ProxyBatchTester.cpp:158-161`，当前内容）：

```cpp
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) break;
            continue;
        }
```

替换为：

```cpp
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) {
                // false means EITHER "cancelled" OR "30s bounded wait exhausted".
                // Only the latter is an outage, and an outage must stop the whole
                // batch (owner policy, §13 决策 1) instead of silently retiring
                // single workers until the batch limps along at 1/N concurrency.
                if (!isCancelled()) {
                    requestBatchCancellation(workerId, apiPort,
                        "network unreachable for 30s (bounded wait exhausted)");
                }
                break;
            }
            continue;
        }
```

说明：**不改动** `waitForNetworkRecovery()` 本身（`:450-481`）——它继续返回 `false` 承载两种语义，由调用点在 `isCancelled()` 处判别。这避免了改签名与两处调用点的连带风险。

- [x] **Step 4: 构建并回归**

```powershell
cmake --build build --parallel 8
ctest -R "ProxyBatchComponentsTest|BatchWorkerPolicyTest" -V
```

Expected: 构建 EXIT 0；两个测试 `100% tests passed`。

- [x] **Step 5: 提交**

```powershell
git add include/ProxyBatchTester.h src/ProxyBatchTester.cpp
git commit -m "fix(batch): stop the whole batch when a worker exhausts its network wait (D1)

A 30s bounded-wait timeout used to retire one worker silently, so three of four
workers exited at 10:16:34 while the network returned at 10:16:35, leaving the
batch running at 1/4 concurrency for the remaining ~16 minutes. Owner policy is
that a sustained outage should stop the task, so the first worker to exhaust its
wait now raises a batch-wide cancellation and logs the reason."
```

---

## §4 Task 3：D1b（Critical）— `waitInterruptible` 的「超时」必须与「取消」分离

**Files:**
- Modify: `src/ProxyBatchTester.cpp`（`:483-500`）
- Modify: `include/ProxyBatchTester.h`（`:62-63` 声明注释同步）

**根因**：`waitInterruptible` 内部 `:489 if (!waitForNetworkRecovery()) return true;` 把「30s 超时」报成 `true`，而两个调用点 `:222 if (waitInterruptible(20)) return;`、`:265 if (waitInterruptible(backoffMs)) return;` 会**直接 `return` 掉整个 worker 函数**（连队列都不再看）。这是比 D1 更彻底的线程死亡路径。

Task 2 落地后，超时已会置位取消标志，因此 `return true` 在结果上不再错误；但**语义必须显式化**，否则下一个改动方仍会误读为「超时 == 取消」。

- [x] **Step 1: 替换 `waitInterruptible` 实现**

锚点（`src/ProxyBatchTester.cpp:483-500`，完整函数）：

```cpp
bool ProxyBatchTester::waitInterruptible(int totalMs) {
    const int stepMs = 10;
    int elapsed = 0;
    while (elapsed < totalMs) {
        if (isCancelled()) return true;
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) return true;
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(cancelMutex_);
            cancelCv_.wait_for(lock, std::chrono::milliseconds(stepMs),
                               [this]() { return cancelRequested_.load(); });
        }
        elapsed += stepMs;
    }
    return isCancelled();
}
```

替换为：

```cpp
// D1b (2026-10-19): the return value means "the batch was cancelled", NOT
// "the requested delay was interrupted". Callers do `if (waitInterruptible(n))
// return;`, which abandons the whole worker, so conflating a bounded network-wait
// timeout with cancellation is what silently killed workers. A timeout now ends
// only the delay; the batch-wide stop is raised by the D1 helper instead.
bool ProxyBatchTester::waitInterruptible(int totalMs) {
    const int stepMs = 10;
    int elapsed = 0;
    while (elapsed < totalMs) {
        if (isCancelled()) return true;
        if (netMon_ && !netMon_->IsConnected()) {
            if (!waitForNetworkRecovery()) {
                // Timed out. requestBatchCancellation() already raised the batch-wide
                // stop, so isCancelled() is true here; returning it keeps the
                // "cancellation only" contract without assuming that.
                return isCancelled();
            }
            continue;
        }
        {
            std::unique_lock<std::mutex> lock(cancelMutex_);
            cancelCv_.wait_for(lock, std::chrono::milliseconds(stepMs),
                               [this]() { return cancelRequested_.load(); });
        }
        elapsed += stepMs;
    }
    return isCancelled();
}
```

- [x] **Step 2: 核对两个调用点语义仍然正确（无需改动，仅确认）**

```powershell
Select-String -Path src/ProxyBatchTester.cpp -Pattern 'waitInterruptible\('
```

Expected（3 处，全部语义正确）：
- `:222` `if (waitInterruptible(20)) return;` — 仅在取消（含 D1 触发的整批停止）时退出
- `:265` `if (waitInterruptible(backoffMs)) return;` — 同上
- `:483` 定义
- （本文件内无其它命中；若出现多于 3 处，需复核是否新增了调用点）

- [x] **Step 3: 构建与回归**

```powershell
cmake --build build --parallel 8
ctest -V
```

Expected: 构建 EXIT 0；`ctest` **53/54**（与基线一致，`XrayManagerStartupTest` 为已知基线失败）。

- [x] **Step 4: 提交**

```powershell
git add include/ProxyBatchTester.h src/ProxyBatchTester.cpp
git commit -m "fix(batch): waitInterruptible must report cancellation only (D1b)

waitForNetworkRecovery()==false means 'cancelled OR 30s timeout', but
waitInterruptible propagated it as 'cancelled'. Both call sites then did
'if (waitInterruptible(n)) return;' — abandoning the whole worker on a mere
network-wait timeout."
```

---

## §5 Task 4：D2（Critical）— 移除 `cancelOnDisconnect`，取消整批的触发源唯一化（含 F7）

**Files:**
- Modify: `include/NetworkMonitor.h`（`:25-37`、`:57`、`:60-62`）
- Modify: `src/NetworkMonitor.cpp`（`:123-179` 状态机四分支）
- Modify: `include/ConfigReader.h`（`:91-93`）
- Modify: `include/config/sections/NetworkMonitorConfigParser.h`（`:52-61`）
- Modify: `src/config/ConfigJsonSerializer.cpp`（`:148-149`）
- Modify: `src/ui/ConfigDialog.cpp`（`:242`、`:457`、`:564-566`）
- Modify: `bin/config.json`（删 `"probe_on_disconnect"` 段）
- Modify: `include/ProxyFinder.h` / `src/ProxyFinder.cpp`（**F7**）
- Modify: `include/RegionBatchResolver.h` / `src/RegionBatchResolver.cpp`（**F7**）
- Modify: `src/ui/AppController.cpp`（`:68`、`:748-749`、`:2421`、`:2485`、`:2639`）
- Modify: `tests/test_network_monitor.cpp`（`:106-121` 删除；`:125` 去参）

**owner 决策（§13 决策 2 + 决策 4，2026-10-19）**：取消整批的触发源**唯一化**为 Task 2 的「worker 30s 超时」。`NetworkMonitor` 收敛为**纯探测器**——只负责报告 `IsConnected()`，不再反向置位任何取消标志。

**为何必须移除**：`cancelOnDisconnect_` 指向 `AppController::cancelRequested_`（`AppController.cpp:72`、`:749`），是**应用级**标志。保留它意味着「取消整批」有两个触发源——D2 的「3 次探测失败（≈30s）」与 D1 的「worker 30s 超时」，两者时间尺度几乎相同，2026-10-19 的事故正是因为它们**毫秒级错开**才漏网。触发源必须唯一。

**为何需要 F7**：`ProxyFinder` 与 `RegionBatchResolver` 的构造函数**只收 `cancelFlag`，不收 `netMon_`**，断网时能停下来唯一依赖 D2 置的应用级标志。移除 D2 后必须自补，否则这两条路径（工具栏「最佳代理」/ CLI `-F`、`-FMIN`；地区解析）断网时只会逐个尝试到自身超时。

- [x] **Step 1: `include/NetworkMonitor.h` 删除取消联动与探针阈值**

删除 `:25-37` 整段（含 `setCancelOnDisconnect` 与 `setProbeOnDisconnect`）：

```cpp
    /// Register an external flag that will be set to true when the network
    /// transitions from connected to disconnected. This allows the monitor
    /// to trigger cancellation in dependent operations (e.g. batch testing).
    void setCancelOnDisconnect(std::atomic<bool>* flag) { cancelOnDisconnect_ = flag; }

    /// Configure probe-on-disconnect behavior: wait for N consecutive failed
    /// checks before setting cancelOnDisconnect_ instead of acting on the first failure.
    /// If maxProbes > 0: use probe mode (grace window before cancel).
    /// If maxProbes == 0: immediate cancel on disconnect (legacy behavior).
    void setProbeOnDisconnect(int maxProbes) {
        maxProbes_ = maxProbes;
        probeEnabled_ = (maxProbes > 0);
    }
```

删除 `:57` 成员声明：

```cpp
    std::atomic<bool>* cancelOnDisconnect_{nullptr};
```

删除 `:60-62`：

```cpp
    // Probe-on-disconnect state
    bool probeEnabled_{true};
    int  maxProbes_{3};
```

保留 `consecutiveFailures_`（改为纯诊断计数，不再触发任何动作）与 `dnsFailures_`、`getConsecutiveFailures()`、`resetProbeCount()`。把 `:63` 的行尾注释 `(triggers cancel)` 改为 `(diagnostic only)`。

- [x] **Step 2: `src/NetworkMonitor.cpp` 简化状态机（`:123-179`）**

整段 `:123-179` 替换为：

```cpp
        if (afterFirst && prev && !allOk) {
            // LOST — count for diagnostics only. D2 was removed (2026-10-19):
            // NetworkMonitor no longer raises any cancellation flag; stopping the
            // batch is ProxyBatchTester's job (see
            // docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md).
            const int fails = consecutiveFailures_.fetch_add(1) + 1;
            Logger::write("Network connection LOST (consecutive failures: " +
                          std::to_string(fails) + ")", LogLevel::ERR);
        } else if (afterFirst && !prev && allOk) {
            // RESTORED — reset counters. DNS dedup is handled by the program-wide
            // DnsShareCache in CurlEasyHandle (CURLOPT_SHARE +
            // CURLOPT_DNS_CACHE_TIMEOUT=-1), so there is no per-monitor cache here.
            consecutiveFailures_.store(0);
            dnsFailures_.store(0);
            Logger::write("Network connection RESTORED", LogLevel::ERR);
        } else if (afterFirst && !allOk) {
            const int fails = consecutiveFailures_.fetch_add(1) + 1;
            Logger::write("Network connection LOST (consecutive failures: " +
                          std::to_string(fails) + ")", LogLevel::ERR);
        } else if (!afterFirst && !allOk) {
            const int fails = consecutiveFailures_.fetch_add(1) + 1;
            Logger::write("Network connection check failed (consecutive failures: " +
                          std::to_string(fails) + ")", LogLevel::ERR);
        }
```

- [x] **Step 2b: F8 去抖 — URL 判定改为多数投票（`NetworkMonitor.h/.cpp`）**

**为什么必须做（抖动缺口 1）**：现有判定要求 `check_urls` **全部**成功，任意一个 URL 抖动（CDN 抽风、站点限流）就判「断网」，且 `break` 会让后续 URL 根本没被探测。这让「30s 连续断网」这个门槛的**实际到达难度远低于名义值** —— 是当前最大的误判来源。

**必须保持不变的既有语义**：DNS 失败一直是**中性票**（2026-08-06 `NetworkMonitorProbeCounterOverflow` 修复确立）。因此「全部 URL 都是 DNS 失败」历史上判**连通**，新逻辑必须保留这一点。

**`include/NetworkMonitor.h`** —— 在 `public:` 区（`IsConnected()` 声明之后）新增纯静态函数：

```cpp
    /// Verdict of one probe round — pure, no I/O, unit-testable without a network.
    ///
    /// F8 (2026-10-19): a single flaky check URL must not be able to declare the
    /// whole network down. Requires strictly more successes than transport
    /// failures, instead of "every URL must succeed".
    ///
    /// DNS failures abstain (they are jitter, not an outage — see the
    /// 2026-08-06 probe-counter fix). If every probe abstained, the verdict stays
    /// connected, preserving the pre-F8 behaviour exactly.
    ///
    /// okCount      - probes that succeeded
    /// netFailCount - probes that failed with a transport error (not a DNS error)
    /// totalUrls    - number of configured check URLs; 0 means "nothing to probe"
    static bool majorityVerdict(int okCount, int netFailCount, int totalUrls);
```

**`src/NetworkMonitor.cpp`** —— 在 `ThreadLoop` 之前新增实现：

```cpp
bool NetworkMonitor::majorityVerdict(int okCount, int netFailCount, int totalUrls) {
    if (totalUrls <= 0) {
        return false;  // nothing to probe -> treat as down (pre-existing behaviour)
    }
    const int decisive = okCount + netFailCount;
    if (decisive == 0) {
        // Every probe abstained (DNS only). Legacy behaviour: stay connected.
        return true;
    }
    return okCount > netFailCount;
}
```

**`src/NetworkMonitor.cpp:96-118`** —— 把探测循环与 `allOk` 判定替换为（**删掉 `break`，探完全部 URL 再判定**）：

锚点（当前 `:96-118`）：

```cpp
        bool allOk = true;

        for (size_t i = 0; i < urls_.size(); ++i) {
            if (stopRequested_) break;

            const std::string& url = urls_[i];

            ProbeResult pr = CheckURLWithDnsFlag(url, checkTimeoutMs_);
            if (!pr.success) {
                if (pr.isDnsError) {
                    // DNS error: do NOT increment consecutiveFailures_,
                    // but count dnsFailures separately for diagnostics
                    dnsFailures_.fetch_add(1);
                    continue;
                }
                allOk = false;
                break;
            }
        }

        if (urls_.empty()) {
            allOk = false;
        }
```

替换为：

```cpp
        // F8 (2026-10-19): probe EVERY url instead of breaking on the first
        // failure, then decide by majority. Previously a single flaky URL flipped
        // connected_ to false and left the remaining URLs unprobed, which made the
        // 30s outage gate far easier to reach than it looks.
        int okCount = 0;
        int netFailCount = 0;
        int probedCount = 0;

        for (size_t i = 0; i < urls_.size(); ++i) {
            if (stopRequested_) break;

            ++probedCount;
            const std::string& url = urls_[i];

            ProbeResult pr = CheckURLWithDnsFlag(url, checkTimeoutMs_);
            if (pr.success) {
                ++okCount;
            } else if (pr.isDnsError) {
                // DNS error: a neutral票 (jitter, not an outage). Counted for
                // diagnostics only; it never votes against connectivity.
                dnsFailures_.fetch_add(1);
            } else {
                ++netFailCount;
            }
        }

        const bool allOk = majorityVerdict(okCount, netFailCount, probedCount);
```

**行为等价性核对（必须逐条验证，只允许第一行发生变化）**：

| 场景（3 URL） | 旧行为 | 新行为 | 是否变化 |
| :--- | :--- | :--- | :--- |
| 3 成功 | 连通 | 连通 | 否 |
| 2 成功 + 1 网络失败 | **断网** | **连通** | ✅ **有意的去抖修复** |
| 1 成功 + 2 网络失败 | 断网 | 断网 | 否 |
| 3 网络失败 | 断网 | 断网 | 否 |
| 2 成功 + 1 DNS 失败 | 连通 | 连通 | 否 |
| 2 DNS 失败 + 1 成功 | 连通 | 连通 | 否 |
| 3 DNS 失败（全部弃权） | 连通 | 连通（`decisive == 0` 分支） | 否 |
| `urls_` 为空 | 断网 | 断网（`totalUrls <= 0` 分支） | 否 |

- [x] **Step 2c: `tests/test_network_monitor.cpp` 补 F8 单测（5 例）**

在文件末尾追加：

```cpp
// --- F8: majority vote so a single flaky URL cannot declare an outage ---------

TEST(NetworkMonitorTest, MajorityVerdict_TwoOfThreeStaysConnected) {
    // The jitter regression: 2 of 3 check URLs reachable must stay connected.
    // Before F8 any single failure meant disconnected.
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(2, 1, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_OneOfThreeIsDown) {
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(1, 2, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_AllProbesAbstainedStaysConnected) {
    // All-DNS-failure round: legacy semantics (2026-08-06 fix) must be preserved.
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(0, 0, 3));
}

TEST(NetworkMonitorTest, MajorityVerdict_NoUrlsIsDown) {
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(0, 0, 0));
}

TEST(NetworkMonitorTest, MajorityVerdict_SingleUrlNeedsSuccess) {
    EXPECT_TRUE(NetworkMonitor::majorityVerdict(1, 0, 1));
    EXPECT_FALSE(NetworkMonitor::majorityVerdict(0, 1, 1));
}
```

`NetworkMonitorTest` 用例数 **11 → 15**（Task 4 Step 11 删 1 例 `ProbeThreshold_TriggersCancelAfterNFailures`，本步加 5 例）。

- [x] **Step 3: 删除配置字段与解析（`include/ConfigReader.h:88-94`）**

删除 `probe_on_disconnect` 段与其注释（保留 `check_interval_ms` / `check_timeout_ms` / `check_urls`）。

- [x] **Step 4: 删除配置解析（`include/config/sections/NetworkMonitorConfigParser.h:52-61`）**

删除 `max_probes` 的整个 `if (pd.contains("max_probes") && ...)` 解析块（连同 `pd` 取值与两层 `else if` 的 WARN 分支）。

- [x] **Step 5: 删除配置序列化（`src/config/ConfigJsonSerializer.cpp:148-149`）**

删除：

```cpp
        pdObj["max_probes"] = config.network_monitor.maxProbes;
        nmObj["probe_on_disconnect"] = pdObj;
```

并删除其上方构造 `pdObj` 的声明（若 `pdObj` 仅供此用途）。

- [x] **Step 6: 删除 UI 属性（`src/ui/ConfigDialog.cpp`）**

删除 `:242`（`propGrid_->Append(new wxIntProperty(L"最大探测次数", "network_monitor_maxProbes", ...))` 及其上方注释）、`:457`（`SetPropertyValue`）、`:564-566`（`GetPropertyValueAsInt` 与 clamp）。

- [x] **Step 7: 删除 `bin/config.json` 的配置段**

从 `"network_monitor"` 对象中删除 `"probe_on_disconnect":{"max_probes":3}`（注意该文件为单行 JSON，需精确编辑）。

- [x] **Step 8: F7 — `ProxyFinder` 补 `netMon_`**

`include/ProxyFinder.h` 构造函数**末尾**追加 `NetworkMonitor* netMon = nullptr`，并新增成员 `NetworkMonitor* netMon_{nullptr};`（需 `#include "NetworkMonitor.h"` 或前向声明）。

`src/ProxyFinder.cpp` 构造初始化列表追加 `, netMon_(netMon)`；在主循环开头（`isCancelled()` 检查之后、访问队列之前）插入：

```cpp
        if (netMon_ && !netMon_->IsConnected()) {
            Logger::write("[ProxyFinder] network disconnected — aborting search",
                          LogLevel::WARN);
            return nullptr;   // 按该函数实际返回类型调整
        }
```

- [x] **Step 9: F7 — `RegionBatchResolver` 补 `netMon_`**

同 Step 8 模式：`include/RegionBatchResolver.h` 构造函数末尾追加形参与成员；`src/RegionBatchResolver.cpp` 主循环开头加同样的 `IsConnected()` 检查与 `[RegionBatchResolver]` WARN。

- [x] **Step 10: 更新 `src/ui/AppController.cpp` 调用点**

- 删除 `:68` 与 `:748` 的 `netMon_.setProbeOnDisconnect(config_.network_monitor.maxProbes);`
- 删除 `:72` 与 `:749` 的 `netMon_.setCancelOnDisconnect(&cancelRequested_);`
- `:2421`、`:2485` 的 `ProxyFinder finder(...)` 与 `:2639` 的 `RegionBatchResolver resolver(...)` 构造，**末尾实参加 `&netMon_`**

- [x] **Step 11: 更新 `tests/test_network_monitor.cpp`**

- **删除** `ProbeThreshold_TriggersCancelAfterNFailures`（`:106-121`）——该行为已不存在
- `ConsecutiveFailures_CountsCorrectly`（`:123-134`）删除 `nm.setProbeOnDisconnect(3);` 一行，其余保留
- `NetworkMonitorTest` 用例数 **11 → 10**；其断言仅匹配 `"LOST"` 子串，Step 2 的新日志格式仍满足，无需改动

- [x] **Step 12: 构建 + 回归**

```powershell
cmake --build build --parallel 8
ctest -V
```

Expected: 构建 EXIT 0；`ctest` **53/54**（与基线一致）；`NetworkMonitorTest` **10/10**。

- [x] **Step 13: 完整性核对（对应 §11 V2）**

```powershell
Select-String -Path src,include -Pattern 'cancelOnDisconnect'      # 期望 0 命中
Select-String -Path src,include -Pattern 'maxProbes|max_probes|probe_on_disconnect'  # 期望 0 命中
```

- [x] **Step 14: 提交**

```powershell
git add -A
git commit -m "refactor(netmon): drop cancelOnDisconnect so batch stop has one trigger

NetworkMonitor used to raise AppController::cancelRequested_ after
probe_on_disconnect.max_probes failures, giving batch cancellation a second
trigger that raced the 30s bounded wait and let workers retire one by one.
The monitor is now a pure probe: it only reports IsConnected(), and
ProxyBatchTester owns the decision to stop. ProxyFinder and
RegionBatchResolver never received a NetworkMonitor pointer and relied on that
application-level flag, so both now check IsConnected() directly."
```

---
## §6 Task 5：D3（High）— 区分「冷却命中」与「重启真失败」，并给静默退出补日志

**Files:**
- Modify: `include/XrayManager.h`（include + 1 声明）
- Modify: `src/XrayManager.cpp`（`:274-296` 拆成 Ex + 兼容包装）
- Modify: `src/ProxyBatchTester.cpp`（`:130-131` 成员类型；`:133-139` hook；`:147-157` 分支；`:275` 条件）

**机理**：`setConnectFailureHook` 先触发（`src/XrayApi.cpp:1951-1960`），调 `evaluateInstanceHealthThrottled`；紧接着 `instanceHealthHook_` 触发，30s 冷却内必然返回 `false`。worker 把该 `false` 当成「重启失败」→ `:155-157 break`，**零日志**地永久退出。

- [x] **Step 1: `include/XrayManager.h` 加 include**

在文件顶部最后一个 `#include` 之后插入：

```cpp
#include "BatchWorkerPolicy.h"
```

（验证：`Select-String -Path include/XrayManager.h -Pattern '#include'`。）

- [x] **Step 2: `include/XrayManager.h` 追加三态声明**

锚点（`include/XrayManager.h:57`）：

```cpp
    bool evaluateInstanceHealthThrottled(int apiPort);
```

替换为：

```cpp
    bool evaluateInstanceHealthThrottled(int apiPort);

    // D3 (2026-10-19): three-state variant of evaluateInstanceHealthThrottled.
    // The bool above collapses "cooldown hit — another reporter already ran" and
    // "relaunch actually failed" into one false, which made
    // ProxyBatchTester::workerThreadFunc silently stop a perfectly usable worker
    // (see docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md).
    // Same locking and cooldown contract as evaluateInstanceHealthThrottled.
    batch_policy::InstanceHealthOutcome evaluateInstanceHealthThrottledEx(int apiPort);
```

- [x] **Step 3: `src/XrayManager.cpp` 把 `:274-296` 改为 Ex + 包装**

锚点（`src/XrayManager.cpp:274-296`，完整函数）：

```cpp
bool XrayManager::evaluateInstanceHealthThrottled(int apiPort) {
    std::shared_ptr<std::mutex> portLock;
    {
        std::lock_guard<std::mutex> mapLock(g_portLockMapMutex);
        std::map<int, std::shared_ptr<std::mutex>>::iterator it = g_portLocks.find(apiPort);
        if (it == g_portLocks.end()) {
            portLock = std::make_shared<std::mutex>();
            g_portLocks[apiPort] = portLock;
        } else {
            portLock = it->second;
        }
    }  // g_portLockMapMutex released here — before the per-port mutex is taken

    std::lock_guard<std::mutex> lock(*portLock);
    const ULONGLONG now = GetTickCount64();
    std::map<int, ULONGLONG>::iterator stamp = lastHealthRestart_.find(apiPort);
    if (stamp != lastHealthRestart_.end() && stamp->second != 0 &&
        (now - stamp->second) < kHealthRestartCooldownMs) {
        return false;  // within the 30s cooldown — another reporter already ran
    }
    lastHealthRestart_[apiPort] = now;  // arm the cooldown even if the relaunch fails
    return evaluateInstanceHealth(apiPort);
}
```

替换为：

```cpp
// D3 (2026-10-19): the three-state core. See the header declaration for why the
// legacy bool return is not sufficient for the worker lifecycle decision.
batch_policy::InstanceHealthOutcome XrayManager::evaluateInstanceHealthThrottledEx(int apiPort) {
    std::shared_ptr<std::mutex> portLock;
    {
        std::lock_guard<std::mutex> mapLock(g_portLockMapMutex);
        std::map<int, std::shared_ptr<std::mutex>>::iterator it = g_portLocks.find(apiPort);
        if (it == g_portLocks.end()) {
            portLock = std::make_shared<std::mutex>();
            g_portLocks[apiPort] = portLock;
        } else {
            portLock = it->second;
        }
    }  // g_portLockMapMutex released here — before the per-port mutex is taken

    std::lock_guard<std::mutex> lock(*portLock);
    const ULONGLONG now = GetTickCount64();
    std::map<int, ULONGLONG>::iterator stamp = lastHealthRestart_.find(apiPort);
    if (stamp != lastHealthRestart_.end() && stamp->second != 0 &&
        (now - stamp->second) < kHealthRestartCooldownMs) {
        return batch_policy::InstanceHealthOutcome::Throttled;  // another reporter already ran
    }
    lastHealthRestart_[apiPort] = now;  // arm the cooldown even if the relaunch fails
    if (evaluateInstanceHealth(apiPort)) {
        return batch_policy::InstanceHealthOutcome::Relaunched;
    }
    return batch_policy::InstanceHealthOutcome::RelaunchFailed;
}

bool XrayManager::evaluateInstanceHealthThrottled(int apiPort) {
    return evaluateInstanceHealthThrottledEx(apiPort) == batch_policy::InstanceHealthOutcome::Relaunched;
}
```

- [x] **Step 4: `src/ProxyBatchTester.cpp` 把 `relaunchedOk` 换成三态**

锚点（`src/ProxyBatchTester.cpp:130-139`）：

```cpp
    std::atomic<bool> instanceDead{false};
    std::atomic<bool> relaunchedOk{false};
#ifdef USE_GRPC_API
    xrayApi.setConnectFailureHook([this, &instanceDead, &relaunchedOk, apiPort]() {
        instanceDead.store(true, std::memory_order_relaxed);
        // P2-1: throttled variant — this hook and instanceHealthHook_ (registered
        // above) can both fire for the same gRPC failure, and the 30s per-port
        // cooldown makes the second one a no-op instead of a second restart.
        relaunchedOk.store(xrayManager_->evaluateInstanceHealthThrottled(apiPort), std::memory_order_relaxed);
    });
#endif
```

替换为：

```cpp
    std::atomic<bool> instanceDead{false};
    // D3 (2026-10-19): three-state outcome instead of a bool. A "cooldown hit"
    // from the second reporter used to look identical to a failed relaunch and
    // silently killed this worker.
    std::atomic<int> instanceHealthOutcome{
        static_cast<int>(batch_policy::InstanceHealthOutcome::Throttled)};
#ifdef USE_GRPC_API
    xrayApi.setConnectFailureHook([this, &instanceDead, &instanceHealthOutcome, apiPort]() {
        instanceDead.store(true, std::memory_order_relaxed);
        // P2-1: throttled variant — this hook and instanceHealthHook_ (registered
        // above) can both fire for the same gRPC failure, and the 30s per-port
        // cooldown makes the second one a no-op instead of a second restart.
        instanceHealthOutcome.store(
            static_cast<int>(xrayManager_->evaluateInstanceHealthThrottledEx(apiPort)),
            std::memory_order_relaxed);
    });
#endif
```

- [x] **Step 5: `src/ProxyBatchTester.cpp` 重写循环顶部的 instanceDead 分支**

锚点（`src/ProxyBatchTester.cpp:147-157`）：

```cpp
        if (instanceDead.load() && relaunchedOk.load()) {
            Logger::write("[ProxyBatchTester] worker "
                          + std::to_string(workerId)
                          + ": instance relaunched (api="
                          + std::to_string(apiPort)
                          + ") — continuing to serve remaining queue items",
                          LogLevel::WARN);
            instanceDead.store(false);
        } else if (instanceDead.load()) {
            break;
        }
```

替换为：

```cpp
        if (instanceDead.load()) {
            const batch_policy::InstanceHealthOutcome healthOutcome =
                static_cast<batch_policy::InstanceHealthOutcome>(instanceHealthOutcome.load());
            const batch_policy::InstanceDeadDecision deadDecision =
                batch_policy::decideInstanceDead(healthOutcome);
            if (deadDecision == batch_policy::InstanceDeadDecision::StopWorkerAndLog) {
                Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                                  + ": instance relaunch FAILED (api=" + std::to_string(apiPort)
                                  + ") — stopping this worker; its remaining queue items are served by other workers",
                              LogLevel::ERR);
                break;
            }
            Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                              + ": instance dead handled (api=" + std::to_string(apiPort)
                              + (deadDecision == batch_policy::InstanceDeadDecision::ClearAndContinueRelaunched
                                     ? ", relaunched" : ", health check throttled (another reporter already ran)")
                              + ") — continuing to serve remaining queue items",
                          LogLevel::WARN);
            instanceDead.store(false);
        }
```

- [x] **Step 6: `src/ProxyBatchTester.cpp` 修正 in-flight 补试条件**

锚点（`src/ProxyBatchTester.cpp:275`）：

```cpp
            if (!addSuccess && instanceDead.load() && relaunchedOk.load()) {
```

替换为：

```cpp
            if (!addSuccess && instanceDead.load()
                && instanceHealthOutcome.load() == static_cast<int>(batch_policy::InstanceHealthOutcome::Relaunched)) {
```

- [x] **Step 7: 确认无残留引用**

Run:

```powershell
Select-String -Path src/ProxyBatchTester.cpp -Pattern 'relaunchedOk'
```

Expected: **0 命中**。

- [x] **Step 8: 构建 + 回归**

Run:

```powershell
cmake --build build --parallel 8
ctest -V
```

Expected: 构建 EXIT 0；`ctest` 53/54（`XrayManagerStartupTest` 已知基线失败）。

- [x] **Step 9: 提交**

```powershell
git add include/XrayManager.h src/XrayManager.cpp src/ProxyBatchTester.cpp
git commit -m "fix(xray): three-state instance health result; log silent worker stop (D3)

The throttled health check returned a bare bool, so a 30s-cooldown hit (the
second of two reporters for the same gRPC failure) was indistinguishable from a
failed relaunch, and ProxyBatchTester::workerThreadFunc exited the worker with a
bare 'break' and no log line at all."
```

---

## §7 Task 6：D4（High）— relaunch 必须继承数据库连接

**Files:**
- Modify: `src/XrayManager.cpp`（`:256`）

**机理**：`XrayInstance` 的构造第 5 参 `sqlite3* db` 有默认值 `nullptr`（`include/XrayInstance.h:12`）。正常启动路径传了 `db_`（`src/XrayManager.cpp:144`），relaunch 路径漏传 → `src/XrayInstance.cpp:317-318` 打 `[XrayInstance] No database connection - generating minimal config` 并生成**无 outbound/路由**的最小配置 → 重启窗口内必然连续注入失败（解释日志中 97 条 `addOutbound fallback failed with empty output, likely instance restart window`）。

- [x] **Step 1: 补第 5 个实参**

锚点（`src/XrayManager.cpp:256`）：

```cpp
        std::unique_ptr<XrayInstance> replacement = std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPort, configDir_);
```

替换为：

```cpp
        // D4 (2026-10-19): pass db_ — relaunch used to fall back to the
        // db == nullptr path in XrayInstance, which logs "No database connection
        // - generating minimal config" and produces a config with no outbound
        // rules, guaranteeing injection failures for the whole restart window.
        std::unique_ptr<XrayInstance> replacement = std::make_unique<XrayInstance>(xrayPath_, socksPort, apiPort, configDir_, db_);
```

- [x] **Step 2: 确认两处构造实参一致**

Run:

```powershell
Select-String -Path src/XrayManager.cpp -Pattern 'make_unique<XrayInstance>'
```

Expected: 2 命中，两行结尾**都是** `configDir_, db_);`。

- [x] **Step 3: 构建 + 回归**

Run:

```powershell
cmake --build build --parallel 8
ctest -R XrayManagerStartupTest -V
```

Expected: 构建 EXIT 0；`XrayManagerStartupTest` 维持基线状态（本方案不负责修它）。**注意**：不要因为该测试失败就回退本步——它是既知基线失败。

- [x] **Step 4: 提交**

```powershell
git add src/XrayManager.cpp
git commit -m "fix(xray): relaunch must pass db_ to XrayInstance (D4)

evaluateInstanceHealth() constructed the replacement XrayInstance without the
sqlite3* argument, so the relaunched instance fell into the 'No database
connection - generating minimal config' path with no outbound rules."
```

---

## §8 Task 7：D5（Medium）— 探活失败 WARN 只在跨过阈值时打一次

**Files:**
- Modify: `src/ui/AppController.cpp`（`:2310`、`:2332`）

**证据**：日志中同一 indexId 每 ~5s 一条 `[OnlineProbe] test failed:`，持续 3 分钟（10:11:34–10:14:15）。根因是判据用了 `>=`，只要 streak 已达阈值，之后每个探活周期都重复打一条。

> **范围说明（评审 B6）**：RCA 的 F4 含两个半边——「relaunch 后等实例真正 ready」与「OnlineProbe 加重试退避」。本方案中：前半边由 D4（relaunch 补传 `db_`）+ `XrayManager::evaluateInstanceHealth` 现有的 `waitInstanceReady()` 覆盖；后半边**只降低日志频度、不改重试节律**，理由是探活由 `network_monitor.check_interval_ms`（默认 10000ms）驱动，属**有界周期轮询**而非无退避的重试风暴——同一 indexId 3 分钟内 ~36 条 WARN 的根因是 `>=` 判据（每个周期重复打同一句），不是重试频率。若 Architect 认定仍需退避，则单列 Task 10（按 `consecutive_failures` 指数拉长失败节点的探活间隔）。

- [x] **Step 1: 修正 socks-port-unknown 分支**

锚点（`src/ui/AppController.cpp:2310-2313`）：

```cpp
                if (curBad + 1 >= probeFailStreak) {
                    Logger::write("[OnlineProbe] test failed: " + mon.indexId
                                  + ": socks port unknown", LogLevel::WARN);
                }
```

替换为：

```cpp
                // D5 (2026-10-19): log only on the crossing (== not >=), otherwise
                // every probe cycle re-logs the same failure forever.
                if (batch_policy::shouldLogProbeFailureCrossing(curBad, probeFailStreak)) {
                    Logger::write("[OnlineProbe] test failed: " + mon.indexId
                                  + ": socks port unknown", LogLevel::WARN);
                }
```

- [x] **Step 2: 修正常规失败分支**

锚点（`src/ui/AppController.cpp:2332-2335`）：

```cpp
                if (cur + 1 >= probeFailStreak) {
                    Logger::write("[OnlineProbe] test failed: " + mon.indexId
                                  + ": " + r.errorMsg, LogLevel::WARN);
                }
```

替换为：

```cpp
                // D5 (2026-10-19): == not >= — see the socks-port-unknown branch above.
                if (batch_policy::shouldLogProbeFailureCrossing(cur, probeFailStreak)) {
                    Logger::write("[OnlineProbe] test failed: " + mon.indexId
                                  + ": " + r.errorMsg, LogLevel::WARN);
                }
```

- [x] **Step 3: `src/ui/AppController.cpp` 顶部加 include**

在 `#include "AppController.h"` 之后插入：

```cpp
#include "BatchWorkerPolicy.h"
```

（验证：`Select-String -Path src/ui/AppController.cpp -Pattern 'BatchWorkerPolicy'`。）

- [x] **Step 4: 确认无残留 `>= probeFailStreak`**

Run:

```powershell
Select-String -Path src/ui/AppController.cpp -Pattern 'probeFailStreak'
```

Expected: 仅剩读取 `probeFailStreak` 的赋值行与两处 `shouldLogProbeFailureCrossing(curBad|cur, probeFailStreak)`；**0** 处 `>= probeFailStreak`。

- [x] **Step 5: 构建 + 回归**

Run:

```powershell
cmake --build build --parallel 8
ctest -R BatchWorkerPolicyTest -V
```

Expected: 构建 EXIT 0；17 tests 全绿。

- [x] **Step 6: 提交**

```powershell
git add src/ui/AppController.cpp
git commit -m "fix(ui): log a probe failure only when the streak crosses the threshold (D5)

The OnlineProbe loop used 'cur + 1 >= probeFailStreak', so once a monitor had
reached the threshold every ~5s probe cycle re-logged the identical WARN
(~36 lines for one indexId over 3 minutes in the 10:11-10:14 window)."
```

---

## §9 Task 8：F5（Medium）— proxy tag 残留不得冒充实例故障

- [x] **Step 0: 先实证 stale-tag 的两种 output 形态是否同源（评审 B5）**

RCA 记录到 10:34:09–10:34:11 的失败 output 有两种形态：`output= adding: proxy` 与 `... existing tag found: proxy`。`isStaleTagInjectionError()` 只匹配后者，因此若有失败只带 `adding: proxy`（xray CLI 的进度行）而不含 `existing tag found`，F5 的门不会生效、实例仍会被误重启 ⇒ 本方案目标未达成。

Run（只读取证）：

```powershell
Select-String -Path bin/log/ui_20261019_083638.log -Pattern 'adding: proxy|existing tag found' | Measure-Object
Select-String -Path bin/log/ui_20261019_083638.log -Pattern 'adding: proxy' -Context 0,2
```

判定：
- 若两形态**总是同源**（同一段 output 内先后出现）⇒ 现判据成立，本 Step 仅留作证据，Step 1/Step 2 不变；
- 若存在**只含 `adding: proxy`** 的失败 ⇒ 扩大判据（`isStaleTagInjectionError` 增加对 `adding: proxy` 的 OR 匹配），并同步修改 `tests/test_batch_worker_policy.cpp` 中既有测试串（含 `existing tag found: proxy` 的那个 case）并新增一个 `EXPECT_TRUE` 用例。

**Files:**
- Modify: `src/XrayApi.cpp`（`:250`）
- Modify: `src/ProxyBatchTester.cpp`（`:296-298`）

**证据**：日志 10:34:09–10:34:11 多条 `addOutbound FAILED ... output= adding: proxy` / `... existing tag found: proxy`，且这些失败随即触发实例重启（`instance relaunch attempted`）。`removeOutbound` 的失败只在 DEBUG 输出（`src/XrayApi.cpp:250`），默认日志不可见，问题被掩盖。

- [x] **Step 1: `src/XrayApi.cpp` 把 `removeOutbound` 失败升为 WARN**

锚点（`src/XrayApi.cpp:250`）：

```cpp
        Logger::write("[XrayApi] removeOutbound FAILED: " + lastError_, LogLevel::DEBUG);
```

替换为：

```cpp
        // F5 (2026-10-19): escalation from DEBUG — a silent removeOutbound failure
        // leaves a stale "proxy" tag behind, and the following addOutbound then fails
        // with "existing tag found" (see docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md).
        Logger::write("[XrayApi] removeOutbound FAILED: " + lastError_, LogLevel::WARN);
```

- [x] **Step 2: `src/ProxyBatchTester.cpp` 用策略门替换裸的 `retryCount >= 3`**

锚点（`src/ProxyBatchTester.cpp:296-298`）：

```cpp
            if (!addSuccess && retryCount >= 3) {
                onXrayEndpointUnreachable(apiPort);
            }
```

替换为：

```cpp
            if (!addSuccess && batch_policy::shouldReportEndpointUnreachable(injectError, retryCount)) {
                onXrayEndpointUnreachable(apiPort);
            } else if (!addSuccess && retryCount >= 3) {
                // F5: a stale "proxy" tag is a bookkeeping failure, not evidence that
                // the instance is dead — do not tear down a healthy Xray process.
                Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                                  + ": injection failed with a stale/duplicate tag (api="
                                  + std::to_string(apiPort)
                                  + ") — instance treated as healthy, no relaunch triggered",
                              LogLevel::WARN);
            }
```

- [x] **Step 3: 构建 + 回归**

Run:

```powershell
cmake --build build --parallel 8
ctest -R BatchWorkerPolicyTest -V
```

Expected: 构建 EXIT 0；17 tests 全绿（其中 `StaleTagError_NotReportedAsEndpointUnreachable` 直接覆盖本改动）。

- [x] **Step 4: 提交**

```powershell
git add src/XrayApi.cpp src/ProxyBatchTester.cpp
git commit -m "fix(xray): a stale proxy tag must not trigger an instance relaunch (F5)

removeOutbound failures were logged at DEBUG (invisible), and the following
addOutbound 'existing tag found' error was classified as instance-unreachable,
so a healthy Xray process got restarted."
```

---

## §10 Task 9：F6（Low）— 日志字段 `byProto=` 改名为 `droppedByProto=`

> **跨文档收口（评审 B8）**：本改动同时实现 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` 的次要发现 H6（`byProto=` 语义误导），落地后该文档 H6 的状态应更新为「已实施（见本方案 Task 9）」。`docs/` 内其余 `byProto=` 出现处（`2026-10-16-Bugfix-SubitemUpdater-GoneMarkAndUriTolerances-v1.0.md`、`2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.0/v1.1.md` 等）属**历史日志引用**，保留原样、不回改。

**Files:**
- Modify: `src/update/SubscriptionParser.cpp`（`:807`）

**理由**：该字段输出的是 `stats.droppedByProtocol[0..3]`（被丢弃行按协议的分布），当前字面串 `byProto=` 极易被误读为「成功按协议」。

- [x] **Step 1: 改字面串（数组下标与后缀 `s`/`t`/`h` 都不动）**

锚点（`src/update/SubscriptionParser.cpp:807`）：

```cpp
                              " byProto=v" + std::to_string(stats.droppedByProtocol[0]) +
```

替换为：

```cpp
                              " droppedByProto=v" + std::to_string(stats.droppedByProtocol[0]) +
```

- [x] **Step 2: 确认改名生效、无残留**

Run:

```powershell
Select-String -Path src/update/SubscriptionParser.cpp -Pattern 'byProto'
```

Expected: **1** 处 `droppedByProto=v`；**0** 处 `byProto=`。（可用 `-Pattern 'byProto'` 同时命中两者以人工核对。）

- [x] **Step 3: 构建 + 回归**

Run:

```powershell
cmake --build build --parallel 8
ctest -R SubscriptionParserTest -V
```

Expected: 构建 EXIT 0；`SubscriptionParserTest` 全绿。

- [x] **Step 4: 提交**

```powershell
git add src/update/SubscriptionParser.cpp
git commit -m "refactor(parser): rename summary field byProto -> droppedByProto (F6)

The field prints stats.droppedByProtocol[] (dropped lines per protocol) but was
labelled 'byProto=', which reads like a success-per-protocol breakdown."
```
---

## §11 验证计划（v2.0：10 条全部有效）

| # | 验证项 | 命令 / 方法 | 期望 |
| :--- | :--- | :--- | :--- |
| V1 | D1/D1b 语义核对 | `Select-String -Path src/ProxyBatchTester.cpp -Pattern 'network unreachable|externalCancel_->store\|cancelRequested_.store'` | 2 处「超时即置应用级取消」代码路径存在；`waitForNetworkRecovery()` 与 `waitInterruptible()` 中**均无**「超时即 `return true`」的旧写法 |
| V2 | D2 移除完整性 + F7/F8 接线 | `Select-String -Path src,include -Pattern 'cancelOnDisconnect'` ；`Select-String -Path src,include -Pattern 'maxProbes\|max_probes\|probe_on_disconnect'` ；`Select-String -Path src,include -Pattern 'NetworkMonitor\* netMon'` ；`ctest -R NetworkMonitorTest -V` | ① `cancelOnDisconnect` **0 命中**；② `maxProbes` / `max_probes` / `probe_on_disconnect` **0 命中**；③ `ProxyFinder` / `RegionBatchResolver` 头文件各有 1 处 `NetworkMonitor* netMon` 形参，`AppController.cpp` 对应构造点传入 `&netMon_`；④ **F8 的 5 个 `MajorityVerdict_*` 用例全 PASS**，`NetworkMonitorTest` **15/15** |
| V3 | D3 策略回归 | `ctest -R BatchWorkerPolicyTest -V` | `InstanceThrottled_ClearsFlagAndKeepsServing` / `InstanceRelaunchFailed_StopsWorker` PASS；**9/9 全绿** |
| V4 | F5 策略回归 | `ctest -R BatchWorkerPolicyTest -V` | `StaleTagError_NotReportedAsEndpointUnreachable` PASS |
| V5 | D5 策略回归 | `ctest -R BatchWorkerPolicyTest -V` | `ProbeFailureAlreadyPastThreshold_NoRepeatLog` PASS |
| V6 | 全量回归 | `cmake --build build --parallel 8 ; ctest -V` | 构建 EXIT 0；`ctest` **53/54**（仅 `XrayManagerStartupTest` 失败，且必须与修复前**同样的**失败，属已知基线） |
| V7 | D4 静态核对 | `Select-String -Path src/XrayManager.cpp -Pattern 'make_unique<XrayInstance>'` | 2 命中，实参都以 `configDir_, db_);` 结尾 |
| V8 | D3 无残留 | `Select-String -Path src/ProxyBatchTester.cpp -Pattern 'relaunchedOk'` | 0 命中 |
| V9 | D5 无残留 | `Select-String -Path src/ui/AppController.cpp -Pattern 'probeFailStreak'` | 无 `>= probeFailStreak` |
| V10 | 现场复跑（人工） | 用 `bin/config.json` + `bin/worker/guindb.db` 跑一次批量测试，人为断网 40–60s 再恢复 | ① 断网超 30s 后出现**一条** `[ProxyBatchTester] network unreachable ... requesting whole-batch cancellation` ERR，随后**整批**退出（4 个 worker 全停），**不再**出现「3 死 1 活、批次带着 1/4 并发跑完」；② 网络恢复后不再有 worker 继续 `pausing` / `resuming`；③ 不再出现 `[XrayInstance] No database connection - generating minimal config`（D4）；④ 同一 indexId 的 `[OnlineProbe] test failed:` 每段 streak 只 1 条（D5；**B6**：本条只验证日志频度，不验证重试节律） |

**V10 的日志取证命令**（沿用本次分析所用手法）：

```powershell
$log = "bin/log/ui_20261019_*.log"
Select-String -Path $log -SimpleMatch "network disconnected" | Measure-Object      # 每次应约 4 条
Select-String -Path $log -SimpleMatch "network wait timed out" | Measure-Object     # 期望 0（或极少）
Select-String -Path $log -SimpleMatch "No database connection" | Measure-Object     # 期望 0
Select-String -Path $log -SimpleMatch "cancelOnDisconnect triggered" | Measure-Object  # 期望 0（瞬断场景）
Select-String -Path $log -SimpleMatch "instance relaunch FAILED" | Measure-Object   # 期望 0
```

---

## §12 影响面与回滚

**影响面**

| 变更 | 受影响行为 | 风险 |
| :--- | :--- | :--- |
| D1 + D1b | 断网 30s 后**整批干净停止**（原先「worker 各自静默退出、批次带 1/4 并发跑完」） | **中（语义变更）**：取消整批的触发源从「NetworkMonitor 3 次探测」改为「worker 30s 超时」；抖动容忍度略降 → 见 §13.2 |
| D2 | `NetworkMonitor` 收敛为纯探测器，不再反向置位任何取消标志 | **中（语义变更）**：删除 `cancelOnDisconnect` 与 `max_probes` 配置项；旧配置中的该段被静默忽略 → 见 §13.2 |
| F7 | `ProxyFinder` / `RegionBatchResolver` 新增 `netMon_` 断网检查 | 低：行为等价，新增 WARN 日志；若既有日志断言依赖旧格式需同步 |
| F8 | `NetworkMonitor` 连通判定由「全部 URL 成功」改为「多数投票」 | 低：**唯一变化是「2 成功 + 1 传输失败」由断网变为连通**（Step 2b 附 8 行等价性核对表）；DNS 中性票与空 URL 行为均保持不变。代价：真断网的确认需探完全部 URL，单轮耗时略增（`check_timeout_ms` × URL 数） |
| D3 | 冷却命中不再杀 worker；新增 2 条 WARN/ERR | 低：纯日志 + 存活策略 |
| D4 | relaunch 出的实例使用完整配置 | 低：与首启路径一致 |
| D5 | 探活失败日志变稀疏 | 无（仅日志基数） ；**B6**：只改日志频度（`>=`→`==`），不改探活重试节律（周期由 `check_interval_ms` 决定） |
| F5 | tag 残留不再触发实例重启 | 低：仍会记为 `XRAY_ERROR` 失败项 |
| F6 | 日志字段改名 | 低：**若有脚本按 `byProto=` 解析需同步**；**B8 跨文档收口**：落地后把 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` 的 H6 状态改为「已实施（见本方案 Task 9）」，并核查 `docs/` 内引用（历史日志引用保留原样） |

**回滚**：每个 Task 一个独立提交，按逆序 `git revert <sha>` 即可；`include/BatchWorkerPolicy.h` 与 `tests/test_batch_worker_policy.cpp` 为新增文件，回滚对应提交即可移除，无残留依赖（唯一外部引用来自本方案新增的 4 个 `.cpp`）。

---

## §13 架构语义定夺结果（`AGENTS.md §6.2`）

> **状态：2026-10-19 已由用户（本项目 owner）定夺 2 项，第 3 项仍待定。**
> 下表为**最终裁决**，取代 v1.0/v1.0.1 中「本方案选……」的建议措辞。

| # | 语义问题 | 建议（已作废） | **裁决（2026-10-19）** | 后果 |
| :--- | :--- | :--- | :--- | :--- |
| 1 | **D1/D1b**：网络等待 30s 超时后，是否终止 worker？ | 备选：有界重试（10×30s）不终止 | **备选 A（v2.0 覆盖 v1.0.2 的「维持现状」）：首个 worker 等满 30s 仍未恢复 → 置应用级取消，**整批停止** + ERR 日志** | Task 2/3 **重新实施**（改为整批停止） |
| 2 | **D2**：断网多久中止整批？ | 备选：3 次阈值 + 3 周期去抖 | **备选：移除 D2，单一触发源** —— 取消整批只由「worker 30s 超时」触发；删除 `cancelOnDisconnect` 机制与 `probe_on_disconnect.max_probes` 配置项；`ProxyFinder` / `RegionBatchResolver` 补 `netMon_` 检查替代其失去的能力 | Task 4 **改为移除任务**（影响 8 个文件） |
| 3 | **D3**：`evaluateInstanceHealthThrottled` 的 `bool` 返回值是否保留？ | 备选：保留（兼容包装） | **已裁决（2026-10-19）：保留 `bool` 兼容包装** — `bool` 退化为 `Ex == Relaunched` 薄包装，既有调用点/测试零改动 | Task 5 解除阻塞，可实施 |
| 4 | **F7（新增，v2.0）**：`ProxyFinder` / `RegionBatchResolver` 是否补 `netMon_`？ | — | **补**：两构造函数新增 `NetworkMonitor*` 形参，循环开头加 `IsConnected()` 检查，断网即中止 | Task 4 子步骤 |
| 5 | **F8（新增，v2.1）**：worker 超时如何防止网络抖动？ | — | **选 A：URL 多数投票** —— 3 个 `check_urls` 改为「成功数 > 传输失败数」才判连通；DNS 失败仍为中性票，全弃权时保持旧语义「连通」；删掉 `break` 使 3 个 URL 全部被探测。另两个抖动缺口（恢复侧稳定窗口、curl 期间抖动宽限）**本期不做** | Task 4 Step 2b / 2c |

**owner 决策演进轨迹（保留审计线索）**

- v1.0.2 首轮裁决：D1/D1b = 维持现状、D2 = 维持现状（三个 Critical 全不修）。
- v1.0.2 复核时 owner 指出：「**网络恢复需要较长时间，所以在排除抖动原因后，应该停摆任务**」——即真实断网应当停止任务，而非让 worker 空转。
- 据此推翻 v1.0.2 裁决：**问题不是「停不停」，而是「怎么停」**。现状是 worker 各自为政静默退出，导致批次带着 1/4 并发继续跑完 16 分钟（残废），而非干净的整批停止。
- v2.0 最终裁决：**首个 worker 超时即整批停止（干净）+ 移除 D2 使停止路径唯一**。

**逐项说明**

1. **D1/D1b → 首个 worker 超时即整批停止。** `src/ProxyBatchTester.cpp:158-161` 的 `if (!waitForNetworkRecovery()) break;` 改为：若 `false` 且 `!isCancelled()`（即「超时」而非「取消」），则置 `externalCancel_`（AppController 应用级标志）+ 自身 `cancelRequested_`、`cancelCv_.notify_all()`，打一条 ERR 日志后 `break`。其余 worker 随即在 `isCancelled()` 处退出 → **4 个 worker 一起停**，取代现在的「3 死 1 活」。`waitInterruptible`（`:489`）同样须区分超时与取消，否则退避点仍会静默杀掉单个 worker。
2. **D2 → 移除。** 删除 `NetworkMonitor::setCancelOnDisconnect()` / `cancelOnDisconnect_` 成员与 `ThreadLoop` 中 3 处 `cancelOnDisconnect_->store(true)`；删除 `probe_on_disconnect.max_probes` 的配置解析（`include/config/sections/NetworkMonitorConfigParser.h:52-61`）、序列化（`src/config/ConfigJsonSerializer.cpp:148-149`）、UI 属性（`src/ui/ConfigDialog.cpp:242/457/564-566`）与 `bin/config.json` 中的 `probe_on_disconnect` 段；删除 `ConfigReader.h:91-93` 的 `maxProbes` 字段。`NetworkMonitor` 的探测与状态机本身（`check_urls` / `check_interval_ms` / `IsConnected()`）**完全不动**。
3. **D3 → 保留 `bool` 兼容包装。** `include/XrayManager.h` 新增三态枚举与 `Ex` 版本函数；原 `bool evaluateInstanceHealthThrottled()` 保留为 `return (Ex(...) == Relaunched);`，既有调用点与测试零改动。
4. **F7 → 补 `netMon_`。** `ProxyFinder`（`include/ProxyFinder.h`）与 `RegionBatchResolver`（`include/RegionBatchResolver.h`）构造函数新增 `NetworkMonitor* netMon` 形参（追加在末位，保持既有实参顺序），在各自主循环开头加 `if (netMon_ && !netMon_->IsConnected()) { 中止 }`。**原因**：这两者不收 `netMon_`，断网时能停下来唯一依赖 D2 置的应用级标志；移除 D2 后必须自补，否则断网时只会逐个尝试到自身超时。调用点见 `src/ui/AppController.cpp`（`:2421`、`:2485`、`:2639`）及各自单测。

### §13.1 决策导致的范围（v2.0）

| 原 Task | 缺陷 | 严重度 | v2.0 状态 |
| :--- | :--- | :--- | :--- |
| Task 1 | 建立可单测策略骨架 | — | ✅ 实施（缩减为 9 用例，去掉 D1/D2 策略组） |
| Task 2 | D1 | Critical | ✅ **改为「首超时即整批停止」**（v2.0 覆盖 v1.0.2 的作废） |
| Task 3 | D1b | Critical | ✅ **改为「超时与取消分离 + 同样整批停止」** |
| Task 4 | D2 | Critical | ✅ **改为「移除 D2 + F7 补 netMon_」**（不再是去抖） |
| Task 5 | D3 | High | ✅ 实施（决策 3：`bool` 兼容包装） |
| Task 6 | D4 | High | ✅ 实施 |
| Task 7 | D5 | Medium | ✅ 实施 |
| Task 8 | F5 | Medium | ✅ 实施 |
| Task 9 | F6 | Low | ✅ 实施 |

**9 个 Task 全部实施**（v1.0.2 曾收缩到 6 个，v2.0 恢复为 9 个但 Task 2/3/4 内容已改写）。

**Task 4 的实际改动面（8 个文件，非 2 个）**：

| 文件 | 动作 |
| :--- | :--- |
| `include/NetworkMonitor.h` | 删 `setCancelOnDisconnect()`、`cancelOnDisconnect_`、`setProbeOnDisconnect()`、`maxProbes_`、`probeEnabled_` |
| `src/NetworkMonitor.cpp` | `ThreadLoop` 三分支删 `cancelOnDisconnect_->store(true)` 与相关 ERR 日志；探测/状态机不变 |
| `include/ConfigReader.h` | 删 `probe_on_disconnect` 段的 `maxProbes{3}`（`:91-93`） |
| `include/config/sections/NetworkMonitorConfigParser.h` | 删 `max_probes` 解析块（`:52-61`） |
| `src/config/ConfigJsonSerializer.cpp` | 删 `max_probes` 序列化（`:148-149`） |
| `src/ui/ConfigDialog.cpp` | 删「最大探测次数」属性 3 处（`:242`、`:457`、`:564-566`） |
| `bin/config.json` | 删 `"probe_on_disconnect":{"max_probes":3}` 段 |
| `include/ProxyFinder.h` / `src/ProxyFinder.cpp`、`include/RegionBatchResolver.h` / `src/RegionBatchResolver.cpp`、其单测、`src/ui/AppController.cpp`（`:2421`、`:2485`、`:2639`） | **F7**：新增 `NetworkMonitor* netMon` 末位形参 + 主循环开头 `IsConnected()` 中止 |

### §13.2 遗留风险声明（须与 owner 确认后签署）

v2.0 下三个 Critical 缺陷**均被修复**，但引入以下**新的行为契约**，实施时须逐条留意：

1. **网络抖动容忍度下降。** 取消整批的判据从「3 次探测失败（≈30s）」收敛为「某个 worker 有界等待 30s 未恢复」。两者时间尺度接近，但新判据**不再依赖 `NetworkMonitor` 的探测计数**，而依赖 worker 实际进入等待的时机。若 4 个 worker 未同时进入等待，首个进入者计时 30s 后即可停止整批。
2. **`ProxyFinder` / `RegionBatchResolver` 断网中止语义新增。** 这两条路径此前「被动」依赖应用级取消标志，v2.0 改为显式 `IsConnected()` 检查。行为等价但**日志格式变化**（会新增 `[ProxyFinder]` / `[RegionBatchResolver]` 的断网 WARN），若现有日志断言依赖旧格式需同步。
3. **`probe_on_disconnect.max_probes` 配置项消失。** 旧 `bin/config.json` 含该段，删除后解析器不再识别（**不会报错，静默忽略**）。用户旧配置副本中的该值失效，属预期。
4. **整批取消仍会置应用级 `cancelRequested_`。** 与 D2 时代一致——网络断网导致的取消会同时影响共用该标志的其他操作（如订阅更新）。因 GUI 为单任务串行，实际无并发冲突；CLI 路径需注意。

---

## §14 交付前登记清单（`AGENTS.md §6.1`）

文档完成后必须完成三处登记（本方案文档自身 + 后续独立评审文档），使用 PowerShell `ReadAllLines` / `WriteAllLines` + `New-Object System.Text.UTF8Encoding($false)`（UTF-8 无 BOM）：

- [ ] **`docs/INDEX.md`**
  - §9.1「Bug 修复记录」表（表头 `| # | 文件 | 说明 | 大小 |`）追加一行（序号接 RCA 的 95 → 本方案为 **96**；评审文档为 **97**）
  - 文件头计数行 `| [Bug 修复记录](#91-bug-修复记录) | 130 | docs/bugfix/ |` → **131**（仅方案文档计入；评审文档计入 §11 代码审查表）
- [ ] **`docs/reviews/`** 下的评审文档登记到 `docs/INDEX.md` §11「代码审查」表（表头 `| # | 文件 | 说明 | 大小 |`），序号接 8 → **9**
- [ ] **`docs/plans/project-plans-tracker.md`**：在 RCA 行（L23）之后追加方案行与评审行（列 = 日期 \| 类型 \| 文档路径 \| 说明 \| 状态；状态 `🟡 draft`）
- [ ] **`docs/context.md`** §三：在 RCA 行（L72）之后追加方案行与评审行（列 = 日期 \| 条目 \| 文档）

---

## §15 变更记录

| 版本 | 日期 | 变更 |
| :--- | :--- | :--- |
| v1.0 | 2026-10-19 | 首版：依据 `docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md` 制定 D1/D2/D3/D4/D5/F5/F6 全部修复方案；新增 D1b（`waitInterruptible` 同源缺陷，写方案时发现）；拆为 9 个 Task，配套 `include/BatchWorkerPolicy.h` + 16 条单测；附验证计划 10 条、回滚与 Architect 待决项 3 条。 |
| v1.0.1 | 2026-10-19 | 依据独立评审 `docs/reviews/2026-10-19-Review-ProxyBatchTester-WorkerThreadStallFix-v1.0.md` 修订全部 9 项阻断项：B1 单测计数 12/16 → 13/17；B2 ctest 期望 52/53 → 53/54（新增测试目标后共 54 个）；B9 Task 3 Step 2 命中数 5 → 3；B3 Task 2 Step 3 消除双重 ±1（判定提前至计数自增/清零之前，形参改名 `priorConsecutiveTimeouts`）；B4 补 D2 集成验证（Task 4 Step 8b：4 个 `NetworkMonitorCancelDebounce_*` 用例或静态不变量检查，并同步 §11 V2）；B5 Task 8 增 Step 0（先实证 `adding: proxy` 与 `existing tag found` 是否同源）；B6 Task 7 补 F4 范围说明（探活为有界周期轮询、非重试风暴）+ §12/§11 V10 同步；B7 Task 3 Files 增列 `include/ProxyBatchTester.h`；B8 F6 跨文档收口 v1.1 方案 H6（该文档状态已同步更新）。 |
| v1.0.2 | 2026-10-19 | **§13 架构语义定夺落地**（owner 裁决）：D1/D1b 选**备选 B 维持现状**（30s 超时即停 worker）、D2 选**备选 B 维持现状**（3 次失败即取消整批）、D3 保留待定。**范围收缩**：Task 2/3/4 三个 Critical Task 不实施，有效 Task 由 9 → 6（Task 1 缩减、5/6/7/8/9 保留）。**连带修正**：单测 17 → 9（删 D1×4 + D2×4）、`BatchWorkerPolicy.h` 移除网络等待与去抖两组纯函数、`NetworkMonitor.*` 零改动、`ProxyBatchTester.cpp` 仅余 D3 + F5 两处改动、ctest 总数 54 不变、§11 V1/V2 作废且 V10 判据 ①② 作废。新增 §13.1 范围收缩表与 §13.2 遗留风险声明。**D3 开工前仍须确认。**
| v1.0.2b | 2026-10-19 | 补齐 §13 决策 3：owner 裁决 **D3 保留 `bool` 兼容包装**（`bool` 版本退化为 `Ex == Relaunched` 薄包装，既有调用点与测试零改动）→ **§13 三项全部裁决完毕**，Task 5 解除阻塞。同时记录：owner 对决策 1（D1/D1b 维持现状）**提出暂停，要求重新评估**，故 Task 5/6/7/8/9 **尚未开工**，源码零改动。 |
| **v2.0** | 2026-10-19 | **架构语义反转 + 方案改写**。owner 复核时指出「网络恢复需要较长时间，排除抖动后**应该停摆任务**」，据此推翻 v1.0.2 的「维持现状」：**问题不是「停不停」而是「怎么停」**——现状是 worker 各自为政静默退出，批次带着 1/4 并发跑完 16 分钟（残废），而非干净停止。**① D1/D1b 改为「首个 worker 等满 30s 未恢复 → 置应用级取消 → 整批停止 + ERR 日志」**；**② D2 改为「移除」**（`cancelOnDisconnect` 机制 + `probe_on_disconnect.max_probes` 配置项一并删除，取消整批的触发源唯一化），Task 4 改动面由 2 个文件扩至 8 个；**③ 新增 F7**：`ProxyFinder` / `RegionBatchResolver` 补 `NetworkMonitor*` 形参与断网检查，替代其因移除 D2 而失去的能力；**④ D3 = 保留 `bool` 兼容包装**（决策 3 落档）。9 个 Task 全部实施（v1.0.2 的作废标记被推翻）。同步改写 §0（含新增 F7 行）、§1、§11（V1/V2 改为新设计的静态核对，V10 判据①② 恢复并重写）、§12、§13（含 owner 决策演进轨迹与 4 条新风险）、§15。**源码仍未改动。** |
| **v2.1** | 2026-10-19 | **新增 F8：worker 超时的抖动防护**。owner 追问「worker 超时如何防止网络抖动」，勘察后定位真实误判源：现有 `allOk` 要求 `check_urls` **全部**成功且遇首个失败即 `break`（`NetworkMonitor.cpp:96-118`），任意单个 URL 抖动即误判断网，使 30s 停批门槛实际到达难度远低于名义值；DNS 失败已是中性票（全弃权判连通，2026-08-06 修复确立的语义，保留）。**owner 选 A**：连通判定改为**多数投票**——纯静态 `NetworkMonitor::majorityVerdict(okCount, netFailCount, totalUrls)`（零 I/O 可单测，符合本项目「纯策略外提」模式），判据 `okCount > netFailCount`，`decisive == 0`（全 DNS 弃权）保持连通、`totalUrls <= 0` 保持断网；探测循环去掉 `break` 探完全部 URL。附 8 行行为等价性核对表，**唯一变化是「2 成功 + 1 传输失败」由断网变连通**。新增 5 个 `MajorityVerdict_*` 单测，`NetworkMonitorTest` 11 → 15。另两个抖动缺口（恢复侧稳定窗口、curl 期间抖动宽限）**本期明确不做**。同步 §0（F8 行）、§1、§11 V2、§12、§13 决策 5。**源码仍未改动。** |
| **v2.2** | 2026-10-19 | **全部 9 个 Task 实施完成**（58 个 Step 全部勾选）。构建 EXIT 0；`ctest` **53/54**，唯一失败为既有基线 `XrayManagerStartupTest.RealXrayStartsBounded`。`BatchWorkerPolicyTest` **11/11**、`NetworkMonitorTest` **15/15**。TDD RED 均已实证：Task 1 `BatchWorkerPolicy.h: No such file or directory`；Task 4 `majorityVerdict is not a member of NetworkMonitor`。提交序列 `9c333a4` → `f6b367c` → `51bfe67` → `cee365d` → `9c4ffb1` → `fefa5b8` → `3b14ce3` → `fd4da8b` → `4511cf5`。**三处实施偏差**（均为实证后的正确决定，非计划遗漏）：① **B5 预设被 Step 0 实证推翻** —— 现场两种报文不同源（`existing tag found` 来自 gRPC desc，`output=adding: proxy` 来自 `xray api ado` 回退子进程 stdout），OR 扩大判据会掩盖真实实例故障，故**不做扩大**，改为覆盖同族措辞（`already exists` / `duplicate`），并加 `CliSubprocessOutput_IsNotTreatedAsStaleTag` 单测锁定；② **F7 接入点由 3 处增至 6 处** —— 另 3 处是同步 GUI 入口（`findFirstProxy` / `findBestProxy` / `resolveRegionsForValidProxies`，本就不传取消标志，保留 `nullptr` 只加 `&netMon_`）；③ **新增设计决策（非评审要求）** —— `requestBatchCancellation` 加 `batchStopReported_` 幂等闩锁，避免多 worker 各自 30s 计时几乎同时超时时重复打 ERR；`XrayManager::InstanceHealthOutcome` 改为**别名** `batch_policy::InstanceHealthOutcome` 而非重复定义枚举；`ProxyFinder`/`RegionBatchResolver` 的 `networkDown()` **移出头文件**（前置声明 + .cpp 定义），否则会给每个包含者引入 `NetworkMonitor.cpp` 链接依赖，`test_region_batch_resolver` 已因此补链该源文件。另发现既有 flake：`NetworkMonitorTest.LoggingOnConnectionLost` 依赖真实 `example.com`，5 次跑约挂 1 次，与本次改动无关。 |
