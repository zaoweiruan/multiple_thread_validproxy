# Review: ProxyBatchTester Worker 线程停摆修复方案（v1.0）独立评审

| 项目 | 内容 |
| :--- | :--- |
| 评审对象 | `docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md`（1427 行） |
| 上游依据 | `docs/bugfix/2026-10-19-RCA-ProxyBatchTester-WorkerThreadStall-v1.0.md` |
| 现场证据 | `bin/log/ui_20261019_083638.log`（426,396 B / 4,469 行） |
| 评审日期 | 2026-10-19 |
| 评审方式 | 三视角独立评审：A 技术断言攻击 / B 可实施性 / C 严重度与文档一致性 |
| 结论 | 🟡 **CONDITIONAL PASS（有条件通过）** |

## 1. 结论表

| 视角 | 判定 | 依据摘要 |
| :--- | :--- | :--- |
| A 技术断言攻击 | 🟡 有条件通过 | D1/D1b/D3/D4/D5/F5/F6 的根因链与修法方向**全部成立**，无伪根因、无「修症状」。但技术断言层面存在 4 项可被攻击的细节：D1 计数器的「双重 ±1」脆弱构造（B3）、F5 判据只覆盖 `existing tag found` 单一形态（B5）、Task 3 期望命中数存疑（B9）、D2 去抖的集成行为未被任何断言覆盖（B4）。 |
| B 可实施性 | 🟡 有条件通过 | 9 个 Task 均为「失败测试 → 注册 → 实现 → 跑绿 → 独立提交」，锚点精确、无 placeholder，可机械执行。但**三处「期望输出」与新增测试目标后的真实输出不符**：单测计数 12/16 实为 13/17（B1）、ctest 52/53 实为 53/54（B2）、Select-String 命中数 5 实为 3（B9）。按计划执行会产生**伪失败**，执行者可能误判为回归而回退正确修复。 |
| C 严重度与文档一致性 | 🟡 有条件通过 | 严重度分级与证据吻合；§12 影响面/回滚、§13 Architect 待决 3 项、§14 登记清单齐备，符合 `AGENTS.md §6.1/§6.2`。但 2 项收口缺失：`include/ProxyBatchTester.h:62-63` 注释与 D1b 新语义冲突未修（B7）；F6 改名正好消解另一份方案的 H6 却未做跨文档收口（B8）；D5 只降日志频度、未落实 RCA F4 的重试退避半边且未声明取舍（B6）。 |

**阻断项统计：9 项**（High 3 / Medium 4 / Low 2）。三视角一致意见：**修复方向无需推翻，但须先修订 9 项后方可进入实施**（其中 B1/B2/B9 会直接导致 TDD 步骤伪失败，优先级最高）。

**评审独立性声明**：本评审仅基于方案文档全文（§0–§15 逐节通读）、RCA 文档、现场日志与 `src/` 真实源码锚点对读，不依赖方案作者的会话上下文；所有行号与原文引用均来自对 `src/ProxyBatchTester.cpp`、`src/XrayManager.cpp`/`include/XrayManager.h`、`src/NetworkMonitor.cpp`/`include/NetworkMonitor.h`、`src/XrayApi.cpp`、`src/ui/AppController.cpp`、`src/update/SubscriptionParser.cpp`、`include/XrayInstance.h`、`CMakeLists.txt`、`include/ProxyBatchTester.h` 的直接读取。

## 2. 阻断项

### B1（High，A+B）方案内单测计数三处互相矛盾，实际为 13 + 4 = 17

- **证据**：Task 1 Step 1 实际写下 **13** 个 `TEST`（D1×4：`NetworkWaitRestored_KeepsServing` / `NetworkWaitTimeout_StillKeepsServing` / `NetworkWaitCancelled_StopsWorker` / `NetworkWaitRepeatedTimeouts_StopOnlyAtCap`；D3×3：`InstanceThrottled_ClearsFlagAndKeepsServing` / `InstanceRelaunched_ClearsFlagAndKeepsServing` / `InstanceRelaunchFailed_StopsWorker`；F5×2：`StaleTagError_NotReportedAsEndpointUnreachable` / `GenericInjectionError_ReportedAfterThreeRetries`；D5×4：`ProbeFailureBelowThreshold_NoLog` / `ProbeFailureCrossing_LogsOnce` / `ProbeFailureAlreadyPastThreshold_NoRepeatLog` / `ProbeFailureDisabledThreshold_NoLog`）。
- 但 Task 1 Step 4 的 Expected 写「**12** 个用例全绿 / 12 tests from BatchWorkerPolicyTest」；Task 4 Step 1 写「16 tests 全 OK（**原 12** + 新 4）」、Task 4 Step 8 与 Task 7 Step 5、Task 8 Step 3、§11 V1 均写「**16** tests」；§15 写「**16** 条单测」；§1 写「新建（**16** 个用例）」。
- **影响**：三个数字（12 / 16 / 实际 13）互斥。按计划执行 Task 1 Step 4 时，GoogleTest 会打印 `13 tests from BatchWorkerPolicyTest`，与文档期望的 12 不符 ⇒ 执行者面临「步骤失败」的假信号；D2 的 4 例在 Task 4 追加后真实总数为 **17**，V1–V5 的「16/16 全绿」永远无法自证。
- **建议**：统一改为 Task 1 = **13** 个 TEST、总数 = **17**；Task 1 Step 4 Expected 写 `13 tests from BatchWorkerPolicyTest`；Task 4 Step 1 写「17 tests（原 13 + 新 4）」；§1/§11/§15 同步改 17。

### B2（High，A+B）新增测试目标后 ctest 总数期望未重算（52/53 → 53/54）

- **证据**：项目基线为 `ctest 52/53`（共 53 个测试，`XrayManagerStartupTest` 为已知基线失败）。Task 1 Step 2 在 `CMakeLists.txt:985` 之后新增 `add_test(NAME BatchWorkerPolicyTest COMMAND test_batch_worker_policy)`，即 **新增 1 个可发现测试**。
- 但 Task 3 Step 3 写「Expected 52/53」、Task 5 Step 8 写「**52/53**」、§11 V6 写「**ctest 52/53**，仅 XrayManagerStartupTest 失败且必须与修复前同样」。
- **影响**：Task 1 之后全量 ctest 的真实输出应为 **53/54**（54 个测试，仍只 1 个失败）。文档坚持 52/53 会被执行者读成「少了一个测试通过」= 回归信号，进而触发不必要的回退或排查。
- **建议**：把 Task 3 Step 3 / Task 5 Step 8 / §11 V6 的期望改为「**53/54**，且失败集合仍恒为 `{XrayManagerStartupTest}`」；并在 §11 V6 增补一句「修复前后失败集合必须逐字一致，总数随新增测试目标 +1」。

### B3（Medium，A）D1 超时计数器存在「双重 ±1」脆弱构造，易在实施时写成 off-by-one

- **证据**：Task 2 Step 3 的伪码顺序为「先 `++consecutiveNetworkTimeouts`（或清零）→ 再调用 `decideAfterNetworkWait(waitedOk, cancelledNow, consecutiveNetworkTimeouts - 1, kMaxConsecutiveNetworkTimeouts)`」，而 Task 1 Step 3 的 helper 内部判据是 `consecutiveTimeouts + 1 >= maxConsecutiveTimeouts`。
- **影响**：调用点的 `- 1` 与 helper 内部的 `+ 1` 必须**恰好互相抵消**，语义才是「第 10 次连续超时才停」。任何一方被后续维护者「简化」掉（这是很自然的冲动，因为 `- 1` 看起来像冗余），阈值立即退化为「第 9 次」或「第 11 次」；且该语义错误不会让任何单测失败（`NetworkWaitRepeatedTimeouts_StopOnlyAtCap` 断言的是 helper 而非调用点）。
- **建议（最小改动）**：调用点直接传 `consecutiveNetworkTimeouts`（不 `- 1`），并把 helper 形参由 `consecutiveTimeouts` 重命名为 **`priorConsecutiveTimeouts`**（语义 = 调用前已发生的连续超时数），helper 内判据改为 `priorConsecutiveTimeouts >= maxConsecutiveTimeouts`。这样调用点「先自增再比较」是单次、自明的，helper 语义也随之明确。

### B4（High，B）D2 的去抖逻辑只测了纯策略，`NetworkMonitor` 接线无任何断言

- **证据**：Task 4 Step 1 新增的 4 个用例（`CancelDebounce_PendingBeforeThreshold` / `_RaisesAfterDebounce` / `_NeverRaisesTwice` / `_ZeroDisablesDebounce`）全部针对 `batch_policy::shouldRaiseCancelNow` 这个**纯函数**；Task 1 Step 3 的 `include/BatchWorkerPolicy.h` 本身不含任何 `NetworkMonitor` 状态。
- 而 D2 的真实修复面在于**状态接线**：`include/NetworkMonitor.h` 新增的 `std::atomic<int> pendingCancelChecks_{0};`、私有方法 `bool shouldRaiseCancelOnDisconnect(int fails);`、`src/NetworkMonitor.cpp` 的「未达阈值 `fetch_add(1)` + WARN / 达阈值 `store(true)` + ERR」实现，以及 **3 个 cancel 站点替换**（`:130-139` LOST 阈值分支、`:156-165` 已断开分支、`:172-178` 从未连上分支）与 **RESTORED 分支 `pendingCancelChecks_.store(0);` 复位**。
- Task 4 Step 8 的验证仅为「`ctest -R "BatchWorkerPolicyTest|NetworkMonitorTest"` … NetworkMonitorTest 与基线一致」——即**没有新增任何 NetworkMonitorTest 用例**。
- **影响**：本方案中最需要验证的环节（跨轮次 pending 累加、恢复复位、3 个站点是否都被替换、`cancelOnDisconnect_ == nullptr` 时不得解引用）零覆盖。若某站点漏改或复位缺失，`ctest` 仍会「与基线一致」而全部通过；V10 的现场复跑也只能间接观察（要求「人为断网 40–60s 再恢复」后不出现 `AutoTask: cancelled`，但现场复跑对时序敏感且非自动化）。
- **建议（最小改动）**：在 `tests/test_network_monitor.cpp`（已注册 `NetworkMonitorTest`，链接 `src/NetworkMonitor.cpp`）中新增 2–3 个不依赖真实网络的用例，直接驱动 `shouldRaiseCancelOnDisconnect()`：
  - `PendingBelowThreshold_DoesNotRaise`：连续调用 2 次（`kCancelDebounceChecks=3`）→ `cancelOnDisconnect_` 指向的原子仍为 `false`；
  - `RaisesExactlyAtDebounce_AndNotTwice`：第 3 次调用 → 标志置 `true`；第 4 次 → 返回值 `false` 且不重复置位；
  - `RestoreClearsPending`：置 pending 后模拟 RESTORED（直接 `pendingCancelChecks_.store(0)` 或暴露一个测试可见的复位入口）→ 再次连续两次调用仍不置位。
  若 `shouldRaiseCancelOnDisconnect` 依赖 `ThreadLoop()` 内部状态而无法直接驱动，则退一步断言「3 个站点替换后 `Select-String -Pattern 'cancelOnDisconnect_->store'` = 0 命中」这一可证伪的静态不变量，并把 Step 8 的 Expected 从「与基线一致」升级为「NetworkMonitorTest 通过数 ≥ 基线 + N」。

### B5（Medium，A）F5 的 stale-tag 判据只覆盖 `existing tag found`，可能漏拦 `adding: proxy` 形态

- **证据**：Task 1 Step 3 的 `isStaleTagInjectionError(const std::string& error)` 判据为 `error.find("existing tag found") != std::string::npos`。
- 而 RCA 记录的 10:34:09–10:34:11 窗口内，同类失败同时存在**两种 output 形态**：`addOutbound FAILED ... output= adding: proxy` 与 `addOutbound FAILED ... failed to add outbound: rpc error: code = Unknown desc = app/proxyman/outbound: existing tag found: proxy`。方案 §9 的证据段亦同时引用了这两者。
- **影响**：若 xray CLI 在某些时序下只输出 `adding: proxy` 而错误串不含 `existing tag found`，则 `shouldReportEndpointUnreachable` 返回 `true`，仍会调用 `onXrayEndpointUnreachable(apiPort)` → 触发实例重启，**F5 的目标（stale tag 不得触发重启）未达成**，且日志会多出一条误导性的「instance treated as healthy」缺席。
- **建议（最小改动）**：二选一并写入方案：
  - (a) 扩大判据为多形态 OR：`error.find("existing tag found") != npos || error.find("adding: proxy") != npos`（并在 Task 1 增补一条 `StaleTagProgressOutput_NotReportedAsEndpointUnreachable` 用例）；或
  - (b) 先以实证关闭歧义：用 `bin/config.json` 复现一次 stale-tag 注入，确认 `XrayApi::getLastError()` 是否**总是**含 `existing tag found`；若总是，则在方案 §9 补一句「两形为同一失败的不同输出片段，判据以 `existing tag found` 为准」作为可追溯依据。

### B6（Medium，C）RCA 的 F4「OnlineProbe 重试退避」半边未落实，且未声明取舍理由

- **证据**：RCA §5 的修复方向 F4 原文为「relaunch 后等待实例真正 ready 再恢复注入；**OnlineProbe 加重试退避**」，并在 D5 描述中把现象记为「同一 id 每 5s 无退避重试 3 分钟（重试风暴）」。
- 本方案 Task 7 的实际改动是把两处日志判据 `cur + 1 >= probeFailStreak` 改为 `batch_policy::shouldLogProbeFailureCrossing(cur, probeFailStreak)`（即 `cur + 1 == probeFailStreak`），只**降低日志频度**；**探活重试节律（`src/ui/AppController.cpp:2295-2338` 的 `for` 循环被多频繁驱动、失败后是否立即重试）完全未改**，§12 影响面表对 D5 写「无，仅日志基数」。
- **影响**：方案的 D5 与 RCA 的 D5 描述**不是同一件事**：RCA 指向「重试风暴」（行为），方案解决「日志风暴」（可观测性）。若探活循环确实由定时器每 ~5s 驱动一次（设计意图：周期性健康检查），则方案的取舍是正确的、但必须写明；若确实存在「失败后立即重试」的紧循环，则方案漏掉真实缺陷。当前文档未提供任何判断依据，属未收口。
- **建议（最小改动）**：在 §0 D5 行与 §8 中补一段「范围与取舍」声明，二选一：
  - (a) 认定周期性探活为设计意图（给出驱动源与周期的事实依据，如 `standalone_pool`/`AutoTask` 的定时器），因此本次只消除重复 WARN，**重试节律不改**；或
  - (b) 若确认存在无退避紧循环，则新增第 10 个 Task（如 `fix(ui): back off repeated online probes for the same indexId`），并在 §11 增补对应用例。

### B7（Low，C）`include/ProxyBatchTester.h:62-63` 的注释与新语义冲突，方案未列入修正

- **证据**：`include/ProxyBatchTester.h:62-63` 对 `waitForNetworkRecovery()` 的声明注释为「Returns true if network is connected (resume testing), false if cancelled (exit)」。
- 而 Task 3（D1b）把 `waitInterruptible()` 的行为改为「网络中断时结束本次睡眠，返回 `isCancelled()`」；同时 Task 2（D1）确立了「网络等待超时**不再**意味着 exit」这一新契约。
- **影响**：头文件注释是后续维护者理解契约的第一入口；注释与实现相反会直接诱导「按注释回退修复」。属文档一致性缺陷。
- **建议**：在 Task 3 的 Files 列表中增列 `include/ProxyBatchTester.h`，并把两处注释改为与实现一致，例如 `waitForNetworkRecovery()` 注释改为「Returns true if the network is back (resume testing). Returns false when cancelled; a bounded wait timeout also returns false and must NOT be treated as a worker-exit condition (see D1).」

### B8（Low，C）F6 改名正好消解另一份方案的 H6，方案未做跨文档收口

- **证据**：Task 9（F6）把 `src/update/SubscriptionParser.cpp:807` 的日志字面串 `" byProto=v"` 改为 `" droppedByProto=v"`，其语义正是「丢弃行按协议分布」，这恰好实现了另一份方案 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` 中**次要发现 H6**（`byProto=` 极易被误读为「成功按协议」，建议改名 `droppedByProto=`）的整改项。
- **影响**：两份方案的待办清单会同时持有 H6：订阅线认为「未实施」、本方案认为「已实施」，属于典型的跨文档状态漂移。
- **建议（最小改动）**：在本方案 §0 F6 行与 §10 增补「跨文档收口」注记，明确「F6 同时关闭 `docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` 的 H6」，并在本方案落地后**同步更新该 v1.1 文档的 H6 状态**（H6 → 已由本方案 Task 9 实施）。§12「但有脚本解析 `byProto=` 的风险」一句也应点名需同步核查 `docs/` 下引用该字段串的文档。

### B9（Medium，A）Task 3 Step 2 的「期望 5 处命中」与可查证据不符

- **证据**：Task 3 Step 2 要求 `Select-String -Path src/ProxyBatchTester.cpp -Pattern 'waitInterruptible\('` 命中 **5** 处，并据此「确认调用点语义」。
- 本次评审对 `src/ProxyBatchTester.cpp` 的直接读取证据为：**定义 1 处**（`:483 bool ProxyBatchTester::waitInterruptible(int totalMs) {`）+ **调用 2 处**（`:222 if (waitInterruptible(20)) return;`、`:265 if (waitInterruptible(backoffMs)) return;`）= **3 处**（`include/ProxyBatchTester.h:67` 的声明不在该 `.cpp` 内，不计入）。
- **影响**：若真实命中数为 3 而文档断言 5，则「确认调用点语义」这一步的判据失效——执行者要么误以为漏读了调用点（浪费时间排查），要么为了凑数去搜索 `waitInterruptibleTime` 之类不存在的符号。D1b 的关键风险正是「调用点语义」，该步骤必须可靠。
- **建议（最小改动）**：把期望改为「**3 处**：定义 `:483`，调用 `:222`（注入前 20ms settle）与 `:265`（transient 退避）」，并附该 3 处的行内语义说明；若实施时发现确有第 4/5 处调用，则据实更新并说明。

---

## 3. 通过项（7 项）

| # | 事项 | 判定依据 |
|---|---|---|
| P1 | **D1 主根因与修法一致** | `src/ProxyBatchTester.cpp:158-161` 原 `if (!waitForNetworkRecovery()) break;` 确实把「30s 超时」与「取消」共用的 `false` 当作终止条件；Task 2 Step 3 改为经 `batch_policy::decideAfterNetworkWait` 决策（超时→`ContinueServing`），且 `StopWorker` 分支**已含 `LogLevel::ERR` 日志**再 `break`（故不存在「静默退出」缺陷，评审线索 #2 不成立）。 |
| P2 | **D1b 是真发现且比 D1 更严重** | `src/ProxyBatchTester.cpp:489 if (!waitForNetworkRecovery()) return true;` 使 `:222 if (waitInterruptible(20)) return;` 与 `:265 if (waitInterruptible(backoffMs)) return;` 在 30s 超时时**直接 `return` 掉整个 worker 函数**（连循环顶部都到不了）。Task 3 把契约收窄为「仅取消即 true」并附调用点核对步骤，方向正确。 |
| P3 | **D3 三态枚举确实消除歧义** | `src/XrayManager.cpp:290-292` 的「30s 冷却命中」与 `:271` 的「重启真失败」都 `return false`，被 `:155-157` 统一当「重启失败」→ 静默 `break`。Task 5 拆出 `evaluateInstanceHealthThrottledEx()`（`Throttled`/`Relaunched`/`RelaunchFailed`）并保留 `bool` 兼容包装，既消除歧义又压低回归面；Step 5 的 `StopWorkerAndLog` 分支补齐了原本零日志的 `break`。 |
| P4 | **D4 根因与修法精确且可证伪** | `include/XrayInstance.h:12` 第 5 参 `sqlite3* db = nullptr` 有默认值，故 `src/XrayManager.cpp:256`（relaunch）漏传 `db_` 会静默降级为最小配置（`:144` 首启路径传了 `db_`）。修法为单参数补全，且 Step 2 的 `Select-String make_unique<XrayInstance>` 期望「2 命中、均以 `configDir_, db_);` 结尾」是可证伪的静态判据。 |
| P5 | **D5 机理成立且边界覆盖到位** | 成功会重置 `consecutive_failures`，故 `>=` 会在阈值后每个探活周期重复打同一条 WARN（与 10:11:34–10:14:15 每 ~5s 一条吻合）；改 `==` 后每段 streak 仅一条。`shouldLogProbeFailureCrossing` 的 4 个用例覆盖 below / at / past / disabled(≤0) 四类边界。 |
| P6 | **F6 锚点精确** | `src/update/SubscriptionParser.cpp:807` 已使用 `stats.droppedByProtocol[0]`，只需改字面串 `" byProto=v"` → `" droppedByProto=v"`（`s`/`t`/`h` 后缀与数组下标均不动），无连带改动。 |
| P7 | **工程形式合规** | 9 个 Task 全为「失败测试 → 注册 → 实现 → 跑绿 → 独立提交」，每条编辑均给逐字锚点，**无 placeholder**；§12 影响面/回滚、§13 Architect 待决 3 项、§14 三处登记清单齐备，符合 `AGENTS.md §6.1/§6.2` 与 writing-plans 规范。D1/D1b/D2 属行为契约变更，已主动升级为待 Architect 定夺，符合项目约束。 |

---

## 4. 最小实施方案表

> 以下为**对方案的修订要求**（非对 `src/` 的代码修改）。建议按 B1/B2/B9 → B4/B5 → B6/B7/B8 → B3 的顺序修订后重新提交评审。

| 编号 | 视角 | 文档位置 | 修订要求 |
|---|---|---|---|
| B1 | A+B | §1 总览行、§2 Task 1 Step 4、§3 Task 4 Step 1/Step 8、§6 Task 7 Step 5、§9 Task 8 Step 3、§11 V1/V3/V4/V5、§15 变更记录 | 统一单测计数：Task 1 实为 **13** 例（D1×4 + D3×3 + F5×2 + D5×4），Task 4 追加 4 例后总数为 **17**；将「12」「16」全部改为与真实文件一致，并明确 Step 4 的期望输出为 `13 tests from BatchWorkerPolicyTest`。 |
| B2 | A+B | §4 Task 3 Step 3、§6 Task 5 Step 8、§11 V6 | ctest 期望输出需随新增测试目标重算：基线 53 个测试（52 通过）→ 新增 `BatchWorkerPolicyTest` 后为 **54** 个测试、真实输出 **53/54**（仅 `XrayManagerStartupTest` 失败）。 |
| B3 | A | §3 Task 2 Step 3 | 消除双重 ±1：调用点不要再传 `consecutiveNetworkTimeouts - 1`；建议直接把调用前累计值传入，并将 helper 形参更名为 `priorConsecutiveTimeouts`（语义=调用前已发生的连续超时数），使其与 `+1 >= max` 自洽、可单测验证「第 10 次超时才停」。 |
| B4 | B | §5 Task 4 Step 1/Step 8、§11 V2 | D2 必须补集成验证：在 `tests/test_network_monitor.cpp` 新增用例覆盖「未达阈值不置位 / 达阈值置位 / 恢复后复位 / 已置位不重复」，或退化为静态不变量检查（`cancelOnDisconnect_->store` 在 3 个站点已移除、仅存于 `shouldRaiseCancelOnDisconnect`）。 |
| B5 | A | §9 Task 8 Step 1/Step 2 | 先以 xray CLI 原始输出实证 `output= adding: proxy` 与 `existing tag found: proxy` 是否同源；若不同源，`isStaleTagInjectionError` 需扩大判据（OR 语义），否则 F5 目标在部分形态下不达成。 |
| B6 | C | §8 Task 7、§12 影响面表、§11 V10 | 对 RCA F4 的「OnlineProbe 重试退避」半边给出明确取舍：或声明「每周期探活为设计意图」并补事实依据（探活周期由 `checkIntervalMs` 决定，非无界重试），或新增 Task 做退避；不得以「影响面=无」一笔带过。 |
| B7 | C | §4 Task 3 Files/Steps | `include/ProxyBatchTester.h` 需列入 Task 3 修改清单，把 `:62-63` 注释改为「Returns true only when cancelled (network outage no longer exits the worker)」，与 D1b 新语义一致。 |
| B8 | C | §9 Task 8 Files 或 §12 | 声明 F6 同时收口另一方案的 H6（`docs/bugfix/2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` §4 H6：`byProto=` 易误读），并把 v1.1 文档中 H6 状态由「待实施」更新为「已实施（见本方案 Task 9）」；同步核查 `docs/` 内引用 `byProto=` 的文本。 |
| B9 | A | §4 Task 3 Step 2 | 修正期望命中数：`Select-String -Pattern 'waitInterruptible\('` 在 `src/ProxyBatchTester.cpp` 内为 **3** 处（定义 `:483`、调用 `:222`/`:265`）；若确认存在其它调用点，请在方案中列出具体行号而非笼统写 5。 |

---

## 5. 变更记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-10-19 | 首版独立评审。三视角（A 技术断言攻击 / B 可实施性 / C 严重度与文档一致性）逐节通读方案 §0–§15（1427 行），并结合 RCA、现场日志 `bin/log/ui_20261019_083638.log` 与 `src/` 真实源码对读。结论 **🟡 CONDITIONAL PASS（有条件通过）**：修复方向全部成立、无需推翻；列阻断项 B1–B9（High 3 / Medium 4 / Low 2），通过项 P1–P7。优先修订 B1/B2/B9（期望输出失真会直接造成执行期伪失败）。 |
