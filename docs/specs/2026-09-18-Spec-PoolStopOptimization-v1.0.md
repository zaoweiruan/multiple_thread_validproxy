# Spec — Pool Stop Optimization（池关闭加速：30-120s → 1-2s）

- 文档类型: Spec（性能优化技术方案）
- 模块: StandaloneProxyPool / ProxyHealthEvaluator / ProxyProbePool / XrayInstance
- 版本: v1.0
- 日期: 2026-09-18
- 状态: Draft（待评审后实施）
- 关联需求: 用户报告"优化功能：当代理池中代理数量多时，关闭池非常缓慢"
- 关联文档:
  - `docs/specs/2026-08-26-Spec-StandaloneProxyPool-v1.0.md`（池的原始设计）
  - `docs/bugfix/2026-09-11-Bugfix-PoolEvaluator-RecursiveLock-Hang-v1.0.md`（stop() 5s async 守护背景）
  - `docs/bugfix/2026-09-10-Bugfix-AppShutdown-PoolWatcher-UAF-v1.0.md`（UAF 教训：不采用 detach 线程 + 释放 Watcher 的方案）
- 实现约束: C++17；**全栈代码禁用 `auto` 类型推导**（AGENTS.md §1）；文档先行；TDD（Google Test，置于 `tests/`）

---

## 1. 背景与目标

用户反馈：关闭代理池非常缓慢，代理数越多越慢。

**目标**：在保留全部现有语义（成员清理、健康回写、UI 刷新、优雅移除）的前提下，把**从用户点击"关闭池"到进程完成清理**的时间从当前的 **30-120 秒** 缩短到 **1-2 秒**。

不改变任何对外行为契约（`stop()` 仍是同步语义；不引入 detach 线程；不变更 UI 关闭交互）。

---

## 2. 根因分析（代码事实）

调用链：`StandaloneFloatingWidget::onStartStopPool` (同步) → `AppController::stopProxyPool` → `StandaloneProxyPool::stop`。

| # | 位置 | 缺陷 | 耗时（100 成员 + probeWorkers=2） |
|---|------|------|-------------------------------------|
| **B1** | `src/StandaloneProxyPool.cpp:57-69` `stop()` 中 `evaluatorThread_.join()` | `evaluatorLoop` 只在 `while(running_)` 循环头检查标志，此时正在 `doProbe` 串行遍历所有 ACTIVE 成员，无法被外部打断 | 30-120 秒（主因，与成员数正相关） |
| **B2** | `src/ProxyHealthEvaluator.cpp:76-108` `doProbe` 串行 | 每次探测：SOCKS/HTTP 走 cURL（默认 `connectTimeoutMs=3000ms + totalTimeoutMs=10000ms`）；其他协议走 gRPC 注入 + cURL 探测 + gRPC 移除 | 平均 ~1s/成员 |
| **B3** | `src/ProxyProbePool.cpp:158-177` `stop()` 串行 stop worker | `for (Worker& w : workers) w.instance->stop()` | probeWorkers=8 → 12 秒 |
| **B4** | `src/XrayInstance.cpp:179-239` `stop()` 硬阻塞 | `sleep_for(500ms)` 无条件执行（即使进程已退出），随后 `WaitForSingleObject(500ms)`，STILL_ACTIVE 时再 `TerminateProcess + WaitForSingleObject(500ms)` | 单实例 1.0-1.5 秒 |
| **B5** | `src/ui/StandaloneFloatingWidget.cpp:719-732` | UI 主线程同步调用 `stopProxyPool` | UI 期间窗口无响应 |

**根因诊断**：主瓶颈是 **B1（同步 join + 循环不可中断）** 与 **B2（串行探测）**，二者耦合形成"join 等待 evaluator 完成一个完整 probe 循环"的死锁式阻塞。B3/B4 是次要放大项。B5 是历史 UI 教训相关，本次不改。

---

## 3. 优化方案（E = A + B + C 组合）

### 3.1 方案 A：doProbe 可中断（主因修复）

**思路**：给 `ProxyHealthEvaluator::probe` 增加一个可选的 `std::atomic<bool>* stopFlag` 参数。每次探测前检查该标志；一旦为 true 立即跳出循环，返回已探测到的部分结果（未探测的成员保持旧状态，不写入 `mergeHealth`，UI 侧感知不到"半个周期"的中间态）。

**改动点**：

1. `include/ProxyHealthEvaluator.h`
   - 增加 `#include <atomic>`
   - `probe(...)` 签名扩展：末尾追加 `std::atomic<bool>* stopFlag = nullptr`
2. `src/ProxyHealthEvaluator.cpp`
   - 循环头 `if (stopFlag && stopFlag->load(std::memory_order_acquire)) break;`
3. `src/StandaloneProxyPool.cpp`
   - `StandaloneProxyPool::doProbe()` 调用点传入 `&stopFlag_`
   - `StandaloneProxyPool` 新增私有成员 `std::atomic<bool> stopFlag_;`（与 `running_` 区分：`running_` 控制主循环退出，`stopFlag_` 让当前正在跑的 probe 也能提前返回）
   - `stop()` 中同时置 `running_ = false` 与 `stopFlag_ = true`

**不修改的部分**：
- `probe` 的返回值契约不变（每个传入 target 对应一个 `MemberHealth`，未探测的返回 `tested=false, alive=false, delayMs=-1`）
- `mergeHealth` 只在 `tested=true` 时更新状态（现有逻辑），因此被中断的 target 不影响成员状态
- 不引入新的取消机制（不做 cURL 二级取消 flag；粒度是"探测一个成员的间隔"，~毫秒级响应）

**预期效果**：从"必须等 probe 循环跑完"到"最坏 1-2 个成员探测后中断"，即**主阻塞从 30-120 秒降到 <2 秒**。

### 3.2 方案 B：XrayInstance::stop 用 poll 替代硬 sleep

**思路**：将 `sleep_for(500ms)` 改为**带超时的 poll 循环**：先查 `WaitForSingleObject(hProcess, 0)`（非阻塞探测），已退出则立即返回；否则每 20ms poll 一次，累计到 500ms 上限后走原有的 `TerminateProcess` 路径。

**改动点**：

1. `src/XrayInstance.cpp::stop()` 重写：

```cpp
void XrayInstance::stop() {
    if (!hProcess_ || hProcess_ == INVALID_HANDLE_VALUE) return;
    // 1. 先礼貌请求：若 api port 已就绪则尝试 graceful stop（现有逻辑）
    // 2. poll 循环：立即检测退出，最多等 GRACEFUL_SHUTDOWN_MS
    for (int elapsed = 0; elapsed < GRACEFUL_SHUTDOWN_MS; elapsed += 20) {
        DWORD wait = WaitForSingleObject(hProcess_, 0);
        if (wait == WAIT_OBJECT_0) break;              // 已退出，跳出 poll
        if (wait == WAIT_FAILED) { break; }           // 句柄无效，跳出
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 3. 若仍未退出，强制 TerminateProcess（现有逻辑）
    // 4. 最终 WaitForSingleObject(hProcess_, 500) 收尾
    CloseHandle(hProcess_);
}
```

**预期效果**：单实例 stop 从 1.0-1.5 秒 → **100-200 毫秒**（进程通常 <50ms 就退出）。

**风险与缓解**：
- 语义保持"最多等 GRACEFUL_SHUTDOWN_MS"：poll 循环上限仍是 500ms，即使进程不退也不会更快终止
- 句柄清理顺序不变（先 CloseHandle 其他句柄再关闭 hProcess）

### 3.3 方案 C：ProxyProbePool::stop 并行停止 worker

**思路**：`stop()` 内部把串行 for 循环改为 `std::async` 并行 stop，最后 `wait_for` 每个 future 最多 3 秒守护。

**改动点**：

1. `src/ProxyProbePool.cpp::stop()` 重写：

```cpp
void ProxyProbePool::stop() {
    if (!running_.load() && workers_.empty()) return;
    running_.store(false);
    std::vector<Worker> workers;
    { std::lock_guard<std::mutex> lock(workersMutex_); workers.swap(workers_); }
    
    std::vector<std::future<void>> stops;
    stops.reserve(workers.size());
    for (Worker& w : workers) {
        stops.push_back(std::async(std::launch::async, [&w]() {
            if (w.instance) w.instance->stop();
        }));
    }
    for (std::future<void>& f : stops) {
        if (f.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
            Logger::write("[ProxyProbePool] worker stop timeout, continuing", LogLevel::WARN);
        }
    }
    for (Worker& w : workers) {
        PortManager::freePort(w.socksPort);
        PortManager::freePort(w.apiPort);
    }
    Logger::write("[ProxyProbePool] stopped " + std::to_string(workers.size()) + " probe worker(s)", LogLevel::INFO);
}
```

**预期效果**：probeWorkers=8 时 stop 从 12 秒 → **~200-300 毫秒**（并行 stop 单实例，每实例 ~150ms）。

**风险与缓解**：
- `workersMutex_` 保护 `workers_` 的换出操作，`stops` 数组持有 worker 副本，异步 stop 不触碰原 map
- `PortManager::freePort` 保持串行（无竞态问题，避免 PortManager 内部锁）

---

## 4. 组合效果（预期）

| 场景 | 优化前 | 优化后 |
|------|--------|--------|
| 100 成员 + probeWorkers=2 池停止 | ~100s | **~1-2s** |
| 500 成员 + probeWorkers=8 池停止 | ~500s | **~2s** |
| UI 关闭点击到"关闭完成"日志 | 5-30s+（用户感知极慢） | **<2s** |

**关键路径缩短**：
- B1+B2 组合 → 中断 probe 循环（从 30-120s → <2s）
- B3 → 并行 worker stop（12s → <1s）
- B4 → poll 替代硬 sleep（单实例 1.5s → <200ms）

三个改动协同：主阻塞点（B1）解决后，B3/B4 决定了停止阶段（`instance_->stop()` + `probePool_->stop()`）本身的时间。

---

## 5. 边界条件与非目标

**不变量**（本次不做）：
- `stop()` 仍是同步语义（不 detach、不返回 future）
- `~StandaloneProxyPool()` 中调用 `stop()`（现有析构逻辑）
- 池成员健康状态、UI 展示、`onMembersChanged`/`onMemberRemoved` 回调语义
- `running_` 与其他外部可见接口（`isRunning()`）语义
- 不修改 `ConfigReader` 中的任何字段

**兼容**：
- 方案 A 采用默认参数，不影响现有 `probe(...)` 调用点（除 `doProbe()` 外）
- 方案 B/C 是内部实现细节，无对外 API 变更

---

## 6. TDD 测试计划（RED → GREEN）

新增测试文件：`tests/TestStandaloneProxyPool.cpp`（追加）与 `tests/TestXrayInstanceStop.cpp`（新建）

### 6.1 测试 A：`ProxyHealthEvaluator_StopFlagReturnsPromptly`（离线可测）

- **前置**：构造 1000 个 target，全部标记 configtype=4（SOCKS），address="127.0.0.1"，port="0"（无效端口，触发立即失败）
- **stopFlag**：置为 true
- **断言**：`probe(targets, url, 5, 100, &stopFlag)` 在 **100 毫秒**内返回（若循环真的探测了所有 target，即使每个 target 1ms 也要 1s+）
- **RED 预期**：现有 `probe` 不接收 stopFlag 参数，编译失败（未实现）

### 6.2 测试 B：`XrayInstance_StopReturnsFast`（真实 xray opt-in）

- **前置**：`XRAY_REAL_EXE` 环境变量设置
- **操作**：`inst.start()` → `inst.stop()`
- **断言**：stop 返回耗时 **<500ms**（现实现最坏 1.5s）
- **RED 预期**：现有 stop 硬 sleep 500ms + 后续等待，总耗时 ~1s
- **备注**：若 `XRAY_REAL_EXE` 未设置，GTEST_SKIP

### 6.3 测试 C：`ProxyProbePool_StopWorkersParallel`（真实 xray opt-in）

- **前置**：`XRAY_REAL_EXE` 设置
- **操作**：创建 4 worker 的 `ProxyProbePool`，`start()` → `stop()`
- **断言**：stop 返回耗时 **<1000ms**（现串行 4 worker × 1.5s = 6s）
- **RED 预期**：现有串行 stop 慢
- **备注**：若 `XRAY_REAL_EXE` 未设置，GTEST_SKIP

### 6.4 测试 D：`StandaloneProxyPool_StopWithManyMembersFast`（真实 xray opt-in）

- **前置**：`XRAY_REAL_EXE` 设置
- **操作**：注入 10 个成员（configtype=4 SOCKS，address 用不可达 IP）→ `stop()`
- **断言**：stop 返回耗时 **<3000ms**
- **RED 预期**：现有实现在 probe 循环中无法中断，10 个不可达 SOCKS 探测 = ~10-30 秒
- **备注**：若 `XRAY_REAL_EXE` 未设置，GTEST_SKIP

---

## 7. 实施顺序

1. **Spec 文档**（本文档） ✅
2. **TDD RED**：写 6.1-6.4 的测试用例（先看到失败）
3. **TDD GREEN A**：实现 `ProxyHealthEvaluator::probe(stopFlag)` 参数
4. **TDD GREEN B**：实现 `XrayInstance::stop` poll 逻辑
5. **TDD GREEN C**：实现 `ProxyProbePool::stop` 并行 stop
6. **构建 + 测试验证**
7. **更新 `docs/INDEX.md` 与 `docs/plans/project-plans-tracker.md`**

---

## 8. 风险与回滚

| 风险 | 影响 | 缓解 |
|------|------|------|
| A：stopFlag 提前返回导致未探测成员状态未更新 | UI 短暂不刷新一次 | `mergeHealth` 不写入 `tested=false`，成员状态保持上一次 probe 的结果（UI 无感知） |
| B：poll 循环 20ms sleep 累积到系统调度抖动 | stop 时间略长 | 上限仍是 GRACEFUL_SHUTDOWN_MS=500ms，最坏与原实现一致 |
| C：std::async 启动失败 | worker 停止不完整 | wait_for 3s 守护 + WARN 日志；析构时 `workers_` 已清空 |

**回滚**：每个改动独立可回滚（3 处修改互不依赖）；如需回滚 A，`probe` 签名保留 stopFlag 参数但 `StandaloneProxyPool::doProbe` 不传，其余不变。

---

## 9. 验收标准

1. ✅ 所有 4 个测试在真实 xray 环境下通过（GTEST_SKIP 环境下至少编译通过 + 离线测试 A 通过）
2. ✅ 构建 0 error，`ctest -R StandaloneProxyPool -V` 全绿
3. ✅ 非 UI 全量测试全绿（无回归）
4. ✅ 手动验证：池中 100+ 成员，点击关闭 → <3 秒返回
5. ✅ `docs/INDEX.md` 与 `docs/plans/project-plans-tracker.md` 更新
