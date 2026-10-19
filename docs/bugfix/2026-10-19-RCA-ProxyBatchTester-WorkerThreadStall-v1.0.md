# RCA: 批量测试 worker 线程停摆（网络恢复等待 30s 超时致线程永久退出）

> 版本 v1.0 · 2026-10-19 · 类型：**根因分析（RCA）**
> **本文档仅做根因分析与诊断，不包含任何代码改动**（用户决策：「只出 RCA 文档」）。

---

## 0. 结论摘要

| 项 | 结论 |
| :--- | :--- |
| **现象** | 批量测试进行一段时间后并行度下降，用户观察到「最后只有 Worker-2 在工作，其它线程停摆」 |
| **主根因 D1** | `ProxyBatchTester::workerThreadFunc` 在**网络恢复等待超时（30s）后 `break` 永久退出线程**。`waitForNetworkRecovery()` 的 `false` 返回值同时覆盖「取消」与「超时」两种语义，调用方 `src/ProxyBatchTester.cpp:159` 一律当作终止条件处理 |
| **直接触发** | `bin/log/ui_20261019_083638.log` 行 4255–4267：10:16:01–10:16:35 一次约 33 秒的网络瞬断，4 个 worker 同时进入等待；10:16:34 有 **3 个恰好等满 30s 判超时** → 3 个线程永久死亡；网络 10:16:35 恢复时只剩 1 个线程 |
| **后果** | 10:16:35 → 10:33 该 run 剩余约 16 分钟内并行度恒为 1（此后所有 `pausing`/`resuming` 日志每次只剩 1 条），共享队列由唯一存活线程串行消费 |
| **第二根因 D2** | `NetworkMonitor` 连续 3 次探测失败即触发 `cancelOnDisconnect` → 取消整批任务（09:28:59、10:11:13 各一次）。一次约 20–40 秒瞬断即废掉整批测试（同一 run） |
| **潜伏缺陷 D3** | `src/ProxyBatchTester.cpp:147-157` 的 `instanceDead && !relaunchedOk → break` **完全静默无日志**；而 `XrayManager::evaluateInstanceHealthThrottled` 在 30s 冷却期内直接 `return false`（**冷却命中 ≠ 重启失败**），两种语义被混为一谈，可能造成另一条静默线程死亡路径（本日志走 CLI 路径，未触发） |
| **严重度** | **中—高**（批量测试吞吐严重退化、且**无任何日志提示**，用户只能观察到并行度下降；不损坏数据、不影响正确性） |
| **范围** | 仅 `src/ProxyBatchTester.cpp` 的 worker 生命周期与 `NetworkMonitor` 取消语义；不涉及解析/存储/注入编码正确性 |

---

## 1. 症状与现场证据

### 1.1 线程拓扑

批量测试仅创建 **4 个 worker 线程 `Worker-0..3`**，每个线程绑定一对**固定、不可迁移**的本地端口（`socksPort` 供 `ProxyTester` 走代理测连通性，`apiPort` 供 `XrayApi` 注入 outbound），实例数与线程数一一对应：

| 线程 | socks 端口 | api 端口 | 绑定 Xray 实例 |
| :--- | :--- | :--- | :--- |
| Worker-0 | 10000 | 10080 | xray_config_10000 |
| Worker-1 | 10001 | 10081 | xray_config_10001 |
| Worker-2 | 10002 | 10082 | xray_config_10002 |
| Worker-3 | 10003 | 10083 | xray_config_10003 |

**关键结构**：4 个 worker 竞争消费**同一个共享队列** `proxiesQueue_`（`src/ProxyBatchTester.cpp:163-169`，`queueMutex_` 保护，`empty()` 即 `break`）。因此**任一 worker 退出后，其未处理的任务会被其它 worker 悄悄取走，日志中不留痕迹**，唯一可观察的表现就是「并行度下降」——这正是用户所见「只有 Worker-2 在工作，其它线程停摆」。

> 佐证：`bin/worker/config/xray_stdout_10000..10003.log` 与 `xray_stderr_1000*.log` 四组文件存在（对应 4 个实例），与本表一致。

### 1.2 三次网络中断事件时间线（`bin/log/ui_20261019_083638.log`）

`NetworkMonitor` 连续探测失败会打印 `Network connection LOST (probe N/M)`；每个 worker 进入恢复等待时打印 `[ProxyBatchTester] network disconnected — pausing, waiting for recovery`（**该行数 = 当前存活 worker 数**），等待结束打印 `resuming` / `cancelled — exiting` / `timed out after 30s — exiting`。

**(A) 第一次：09:28:38–09:28:59（行 3630–3646）→ 整批任务被取消**

```
09:28:38  [NetworkMonitor] probe failed ... (SSL connect error)
09:28:38  Network connection LOST (probe 1/3)
09:28:40  ×2  [ProxyBatchTester] network disconnected — pausing, waiting for recovery
09:28:41  ×2  [ProxyBatchTester] network disconnected — pausing, waiting for recovery   ← 4 worker 全部进入等待
09:28:49  Network connection LOST (probe 2/3)
09:28:59  Network connection LOST (probe 3/3)
09:28:59  [NetworkMonitor] cancelOnDisconnect triggered after 3 failed probes
          ×4  [ProxyBatchTester] network wait cancelled — exiting   ← 4 worker 同时退出
          AutoTask: cancelled
          AutoTask failed
```

**(B) 第二次：10:10:52–10:11:13（行 4156–4176）→ 整批任务被取消**

```
10:10:52  Network connection LOST (probe 1/3)
10:10:52/53/54  ×4  network disconnected — pausing, waiting for recovery   ← 4 worker 全部进入等待
10:11:13  Network connection LOST (probe 3/3)
10:11:13  [NetworkMonitor] cancelOnDisconnect triggered after 3 failed probes
          ×4  network wait cancelled — exiting
（10:11:23/33/44/54… 每 ≈10s 重复 LOST 3/3 + cancelOnDisconnect，cancel 幂等；
  10:14:20  Network connection RESTORED (probes reset)）
```

**(C) 第三次：10:16:01–10:16:35（行 4255–4267）→ 3 worker 永久死亡、仅剩 1（用户症状的直接成因）**

```
10:16:01  [NetworkMonitor] probe failed ... (Timeout)
10:16:01  Network connection LOST (probe 1/3)
10:16:02  ×2  network disconnected — pausing, waiting for recovery
10:16:03  ×1  network disconnected — pausing, waiting for recovery
10:16:04  ×1  network disconnected — pausing, waiting for recovery   ← 4 worker 全部进入等待
10:16:17  probe failed (www.qq.com, Timeout) + Network connection LOST (probe 2/3)
10:16:34  ×3  [ProxyBatchTester] network wait timed out after 30s — exiting   ← 3 个 worker 判超时永久退出
10:16:35  Network connection RESTORED (probes reset)
10:16:35  ×1  [ProxyBatchTester] network restored — resuming   ← 仅 1 个 worker 恢复
```

> **竞态说明**：网络实际于 10:16:34.9 恢复，而 3 个 worker 的 30s 预算恰好先到期（毫秒级边界），故被判超时退出。这是一次纯粹的边界竞态，不是配置错误。

### 1.3 并行度塌缩证据（决定性）

10:16:35 之后，全日志中所有 `pausing` / `resuming` **每次只剩 1 条**：

| 时间 | 事件 |
| :--- | :--- |
| 10:17:06 | pausing ×1 |
| 10:17:32 | resuming ×1 |
| 10:17:47 | pausing ×1 |
| 10:18:02 | resuming ×1 |
| 10:26:21 | pausing ×1 |
| 10:26:32 | resuming ×1 |
| 10:31:49 | pausing ×1 |
| 10:32:01 | resuming ×1 |

⇒ 单线程持续约 16 分钟直至该 run 结束（10:32–10:33）。

### 1.4 全日志模式计数（`Select-String -SimpleMatch`）

| 模式 | 计数 | 解读 |
| :--- | ---: | :--- |
| `network disconnected`（pausing） | 16 | 峰值 4/次（4 worker），末段恒 1/次 |
| `network wait cancelled` | 8 | 09:28:59 ×4 + 10:11:13 ×4 |
| **`network wait timed out after 30s`** | **3** | 10:16:34 ×3 —— **本次停摆的直接证据** |
| `network restored` | 5 | 10:16:35、10:17:32、10:18:02、10:26:32、10:32:01 |
| `cancelOnDisconnect triggered` | 43 | 幂等重复 |
| `Network connection LOST` | 56 | — |
| `Network connection RESTORED` | 7 | — |
| `instance relaunch attempted after injection failures` | 14 | 全部成功（**0 次 `relaunch FAILED`**）|
| `continuing to serve remaining queue items` | **0** | D3 路径在本日志从未触发 |
| `retrying proxy` | **0** | in-flight 补试路径未触发 |
| `[Worker-N]` 注入失败（成对 ERR） | W0=20 / W1=10 / W2=18 / W3=22 | = 35 次注入失败 × 2 行 |

> **注意**：ERROR 只在失败时打印，正常进度为 INFO/DEBUG 级（本日志 WARN-only 不可见）。因此「只有 Worker-2」**不能**由尾部 ERROR 行的分布直接证明，但**可以**由 1.3 的 `pausing/resuming` 条数塌缩确凿证明「只剩 1 个线程在工作」。存活者具体是哪一个无法从日志判定（正常进度不可见）。

---

## 2. 代码路径溯源

### 2.1 主循环（`src/ProxyBatchTester.cpp:142-169`）

```cpp
while (true) {
    // Check for cancellation
    if (isCancelled()) {
        break;
    }
    if (instanceDead.load() && relaunchedOk.load()) {
        Logger::write("[ProxyBatchTester] worker " + std::to_string(workerId)
                      + ": instance relaunched (api=" + std::to_string(apiPort)
                      + ") — continuing to serve remaining queue items", LogLevel::WARN);
        instanceDead.store(false);
    } else if (instanceDead.load()) {
        break;                                              // ← D3：静默退出（无日志）
    }
    if (netMon_ && !netMon_->IsConnected()) {
        if (!waitForNetworkRecovery()) break;               // ← D1：超时也走 break（永久退出）
        continue;
    }

    int profileIdx = -1;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (proxiesQueue_.empty()) break;                   // ← 唯一正常退出（队列空）
        profileIdx = proxiesQueue_.front();
        proxiesQueue_.pop();
    }
    ...
```

worker 退出主循环的**全部路径**只有 4 条：
1. `isCancelled()`（整批取消）— `:144-146`；
2. `instanceDead && !relaunchedOk`（实例重拉失败）— `:155-157`（**静默**）；
3. **`!waitForNetworkRecovery()`（网络等待超时/取消）— `:158-161`（本根因）**；
4. `proxiesQueue_.empty()`（队列耗尽，正常结束）— `:166`。

退出后由 `testProxiesMultiThreaded` **无条件 `join`** 全部线程（`numWorkers = portPairs.size()`），**退出线程本学期内不会复活**。

### 2.2 `waitForNetworkRecovery()`（`src/ProxyBatchTester.cpp:450-481`）

```cpp
bool ProxyBatchTester::waitForNetworkRecovery() {
    Logger::write("[ProxyBatchTester] network disconnected — pausing, waiting for recovery", LogLevel::WARN);
    const int totalWaitMs = 30000;
    int elapsedMs = 0;
    while (!isCancelled() && elapsedMs < totalWaitMs) {
        {
            std::unique_lock<std::mutex> lock(networkStateMutex_);
            if (!netMon_ || netMon_->IsConnected()) {
                networkDown_ = false;
                networkCv_.notify_all();
                Logger::write("[ProxyBatchTester] network restored — resuming", LogLevel::WARN);
                return true;
            }
            networkDown_ = true;
            networkCv_.wait_for(lock, std::chrono::milliseconds(250),
                                [this]() { return cancelRequested_.load(); });
        }
        elapsedMs += 250;
    }
    if (isCancelled()) {
        Logger::write("[ProxyBatchTester] network wait cancelled — exiting", LogLevel::WARN);
    } else {
        Logger::write("[ProxyBatchTester] network wait timed out after 30s — exiting", LogLevel::WARN);
    }
    return false;                                            // ← 取消与超时共用同一 false
}
```

**缺陷**：函数只有一个 `false` 出口值，却承载两种完全不同的语义（「整批被取消」vs「等待超时」）。**调用方 `:159` 无法区分，一律 `break`**。语义上「等待超时」只意味着「这次没等到网络恢复」，**绝不应等于「线程永久下岗」**。

（对照：`waitInterruptible`（`:483-500`）也调用 `waitForNetworkRecovery()`，但把 `false` 当作「本次睡眠被打断」处理（`return true`），并未退出 worker —— 同一函数的两个调用方对返回值语义的理解不一致，进一步印证契约模糊。）

### 2.3 `NetworkMonitor` 取消链（D2）

`NetworkMonitor` 连续 3 次探测失败 → `cancelOnDisconnect triggered` → 置 `cancelRequested_ = true` → 所有 worker 在 `isCancelled()`（`:144`）或在 `waitForNetworkRecovery` 轮询的 CV 谓词处立即退出 → 整批 `AutoTask: cancelled` / `AutoTask failed`。**一次 20–40 秒的瞬断即中止整批测试**，而非「暂停后继续」。

### 2.4 `XrayManager::evaluateInstanceHealthThrottled`（`src/XrayManager.cpp:274-296`）（D3）

```cpp
std::lock_guard<std::mutex> lock(*portLock);
const ULONGLONG now = GetTickCount64();
std::map<int, ULONGLONG>::iterator stamp = lastHealthRestart_.find(apiPort);
if (stamp != lastHealthRestart_.end() && stamp->second != 0 &&
    (now - stamp->second) < kHealthRestartCooldownMs) {
    return false;  // within the 30s cooldown — another reporter already ran
}
lastHealthRestart_[apiPort] = now;  // arm the cooldown even if the relaunch fails
return evaluateInstanceHealth(apiPort);
```

`false` 既表示「**冷却命中，未执行重启**」（`:292`），也表示「**重启真正失败**」（`evaluateInstanceHealth` 的 `:271 return false`）。而 `connectFailureHook`（`src/ProxyBatchTester.cpp:133-139`）把该返回值直接存为 `relaunchedOk`，主循环顶部 `:155-157` 据此 `break`。**「冷却命中」被当作「重启失败」→ 线程静默死亡**。触发点位于 `src/XrayApi.cpp`（第 2 次连续 gRPC 连接失败时先调 `connectFailureHook_` 再调 `instanceHealthHook_`；CLI spawn 失败点只调后者——见 `include/ProxyBatchTester.h:73-79` 注释）。本 build 走 CLI 路径（`addOutbound`），故本日志 `continuing to serve remaining queue items` = 0，D3 未触发，但为潜在缺陷。

---

## 3. 根因判定

| 编号 | 判定 | 证据 | 置信度 |
| :--- | :--- | :--- | :--- |
| **R1 / D1** | **网络恢复等待超时被当作线程终止条件**（`:158-161` + `:478-480`）→ 线程永久退出、本 run 不再复活 | §1.2(C) 3 × `timed out`；§1.3 并行度塌缩；§1.4 计数 | **确证（最高）** |
| **R2 / D2** | **`NetworkMonitor` 3 次失败即取消整批任务**，瞬断废掉整个 run | §1.2(A)(B) 各 4 × `cancelled — exiting` + `AutoTask failed`；§1.4 计数 8 | **确证** |

---

## 4. 次要发现（同批，均未改码）

- **D3（潜伏，未触发）**：`:147-157` 静默 break + `evaluateInstanceHealthThrottled` 的「冷却命中/重启失败」语义混淆（§2.4）。
- **D4（已实证）**：`[XrayInstance] No database connection - generating minimal config`（14 条）⇒ relaunch 出的实例使用**最小配置**（无 outbound/路由），重启窗口内必然连续注入失败——解释 97 条 `addOutbound fallback failed with empty output, likely instance restart window`。
- **tag 残留（已实证）**：10:34:09–10:34:10 多次 `output= adding: proxy` / `failed to add outbound: ... existing tag found: proxy`，说明 CLI 报失败时 tag `proxy` 实际已存在（首次 add 可能已成功但返回空/失败），`removeOutbound` 未生效或与实例重启交错。
- **D5（已实证）**：`[OnlineProbe] test failed: 5854986468419022565: curl_easy_perform failed: SSL connect error` 对同一 id **每 5 秒无退避重试长达 3 分钟**（10:11:34–10:14:15）——重试风暴。
- **可读性**：`byProto=` 实为「被丢弃行按协议分布」而非「成功按协议」，易误读（详见关联文档 `2026-10-19-Bugfix-SubscriptionParser-MislabeledSsPayload-v1.1.md` H6）。

---

## 5. 修复方向（**仅方案，未实施**）

> 以下均涉及 worker 生命周期与取消语义（**是否允许无限等待、是否更改取消语义**），按 `AGENTS.md` 属架构级行为变更，**实施前需先咨询 Architect**。

| 编号 | 目标 | 方案要点 |
| :--- | :--- | :--- |
| **F1** | 修 R1（主体，最小改动） | `src/ProxyBatchTester.cpp:158-161` 改为「**只有取消才 break**」：`if (!waitForNetworkRecovery() && isCancelled()) break; continue;`；超时不再终止线程。建议另加**连续超时上限**（如 N 次，防永久空转）并将超时降级为 WARN/INFO + 计数 |
| **F2** | 修 R2 | `NetworkMonitor::cancelOnDisconnect` 不应直接取消整批；瞬断应仅触发「暂停 + 有界重试」，仅在超过可配置阈值时取消，或改由用户手动终止 |
| **F3** | 修 D3 | `:155-157` 的 `break` **至少打一条 WARN/ERR**（含 workerId/apiPort/instanceDead/relaunchedOk）；并区分返回语义——「冷却命中」应返回独立状态（或让 hook 在冷却命中时不置 `instanceDead=false` 之外的破坏性结果），不得与「重启失败」共用 `false` |
| **F4** | 修 D4/D5 | relaunch 后等待实例真正 ready 再恢复注入；`OnlineProbe` 重试增加指数退避 |

---

## 6. 验证计划（修复时执行）

1. **单测（新增）**：模拟 `netMon_->IsConnected()==false` 且 30s 内不恢复（缩短为测试时限）→ 断言 worker **不退出**、超时后仍能消费队列残留项（覆盖 R1）。
2. **单测（新增）**：`NetworkMonitor` 3 次失败但网络随即恢复 → 断言任务**未被取消**（覆盖 R2）。
3. **单测（新增）**：`evaluateInstanceHealthThrottled` 冷却命中返回状态与「重启失败」可区分（覆盖 D3）。
4. **回归**：`ctest` 全量（基线 52/53，`XrayManagerStartupTest` 为已知基线失败）。
5. **真实数据**：复跑一次批量测试，日志中 `network wait timed out after 30s` 后 `pausing/resuming` 仍应维持 4 条（并行度不塌缩）。

---

## 7. 影响面与回滚

- **影响模块**：`src/ProxyBatchTester.cpp`（worker 循环 / `waitForNetworkRecovery`）、`NetworkMonitor`（取消策略）、`src/XrayManager.cpp`（健康评估返回值语义）。
- **不改动**：解析、存储、注入编码、UI 层（UI 层无任何 worker 状态消费代码）。
- **回滚成本**：低（改动集中在 worker 生命周期，可用原 `break` 语义一键回退）。

---

## 8. 后续待办

1. 按 `AGENTS.md §6.1` 就 F1/F2 的架构语义（是否允许无限等待、取消语义变更）咨询 Architect。
2. 确认用户「只有 Worker-2」的观察来源（GUI 面板？哪一次 run？）——UI 层无 worker 索引/状态消费，需据此校正文档措辞。
3. 修复实施后按 §6 补齐单测并登记 `docs/plans/project-plans-tracker.md`。

---

## 9. 变更记录

| 版本 | 日期 | 说明 |
| :--- | :--- | :--- |
| v1.0 | 2026-10-19 | 初版。基于 `bin/log/ui_20261019_083638.log` 实证与 `src/ProxyBatchTester.cpp`（:108-177、:440-500）、`src/XrayManager.cpp`（:248-296）源码对读，判定主根因 D1（网络等待超时致线程永久退出）与第二根因 D2（瞬断取消整批），并记录潜伏缺陷 D3/D4/D5。仅 RCA，不含代码变更。 |
