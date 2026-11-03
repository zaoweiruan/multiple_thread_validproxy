# Spec — 统一代理测试反馈与队列化方案 (ProxyTestUnified)

- **日期**: 2026-10-29
- **类型**: Spec (SDD)
- **模块**: ProxyTest / ProxyBatchTester / TaskQueue / MainFrame / ProxyListPanel
- **版本**: v1.0
- **作者**: AI Assistant
- **状态**: 🟡 reviewed (B1/B2/B3 已修订，待 Phase A 实施)

## 1. 背景与问题

### 1.1 现状（来自日志 ui_20261029_093445.log）

用户多选 69 个代理 → 右键「测试选中代理」→ 69 条 `Task queued: task-...-N`，随后批量测试开始（`Testing 69 proxies total`）。观察到：

1. **每次 queued 单代理测试都重启 xray 实例**：日志 `XrayManager::start: count=1` → `Creating config` → `Started successfully, socks=10000` → `XrayManager::stopAll cleared 1 instance(s)`，每个代理一次完整 start/stop，无复用。
2. **测试反馈、日志信息不统一**：单代理、批量、多选、在线探测的进度/结果/汇总格式各不相同，无法直观对比。
3. **测试完成（自行结束 / 强制终止 / 程序关闭）后无统一汇总**：缺乏 `Total/Success/Failed/Remaining` 的统一尾部汇总。

### 1.2 根因

- `ProxyTestService::runSingleProxy` 创建**栈局部** `ProxyBatchTester`，每次 `runWithIndexId` → `startXrayInstances(1)` → `XrayGuard` 析构 `stopAll()`。虽然 `keepXrayAlive_` 优化已让实例在析构时保留，但 queued 单代理测试仍各自创建/销毁 `ProxyBatchTester`，无法共享一个池。
- 多选场景下，每个代理独立排队到 `TaskQueue`（priority 2），每个都启动 count=1 的 xray 实例，**无"存活池"复用**。
- 日志格式分散在 `ProxyBatchTester`（`[Worker-N] [n/total]`）、`ProxyTester`、`AppController`（`Single proxy test succeeded`）、`OnlineProbe`（`Online proxies test finished`）等多处，格式不一致。
- `cancelTest()` / 程序退出 / 正常完成的收尾路径各自打不同的汇总（或不打），无统一 "exit reason"。

### 1.3 目标

1. **所有测试代理均检查是否有存活队列（池）**：有则按测试类型决定注入哪个队列并更新队列汇总；无则按类型创建池。
2. **测试过程中反馈、日志形式统一**（进度/结果/汇总）。
3. **测试完成（自行结束 / 强制终止 / 程序关闭）后均对队列测试情况（总数/成功/失败/剩余）进行汇总及日志**。

## 2. 任务类型与互斥（现有矩阵，参考）

`TaskType`（20 值）分三类（`MutexPolicy::decide()`）：

| 类别 | 类型 | 互斥策略 |
|---|---|---|
| Worker 任务（16） | TEST_SUBSCRIPTION, TEST_SINGLE_PROXY, TEST_ALL, TEST_BATCH, UPDATE_SUBSCRIPTION, UPDATE_ALL, DEDUP, SYNC_DB, RESOLVE_REGION, RESOLVE_SINGLE_REGION, FIND_FIRST, FIND_BEST, FIND_BY_INDEX, AUTO_TASK, PROBE_POOL_MEMBERS, PROBE_MEMBER | 共享单 worker TaskQueue，彼此 REJECT（串行） |
| 只读导出（2） | EXPORT_SHARE_LINKS, GENERATE_CONFIG | 可与其他并发（ALLOW） |
| 在线探测（1） | TEST_ONLINE | 独立线程（probeThread_），可与 worker 并行；探测互斥由 onlineProbeRunning_ 守护 |

特例：TEST_SINGLE_PROXY 与 TEST_ALL/TEST_BATCH 可共存（ALLOW，细粒度交错）。

## 3. 方案设计

### 3.1 Phase A — 统一"存活队列"检查（最高优先级）

**目标**：消除 queued 单代理测试重复启动 xray 实例。

**现状**：
- `testSingleProxyAsync(indexId, wxHandler)`：fast path 检查 `proxyBatchTester_`；若 running_ 且非 null → `insertProxy(indexId)`（注入 running pool）；否则 submit TEST_SINGLE_PROXY priority 2 → `doTestSingleProxy` → `ProxyTestService::runSingleProxy` → 栈局部 `ProxyBatchTester` → `runWithIndexId` → `startXrayInstances(1)` + `XrayGuard` stopAll（已通过 `keepXrayAlive_` 跳过 stopAll，但实例仍 count=1 新建）。

**问题**：无 running pool 时，每个单代理测试都创建 count=1 的池。queued 多选 = N 个 count=1 池，重复 start/stop。

**方案**（修订 B1/B2）：

#### 3.1.1 新增 AppController 入口：`testSelectedProxiesAsync`

为支持多选 count=N 池创建，新增入口（替代 `onTestProxy` 逐个 `testSingleProxyAsync`）：

```cpp
// AppController.h
void testSelectedProxiesAsync(const std::vector<std::string>& indexIds, wxEvtHandler* wxHandler);
```

实现（AppController.cpp）：
```cpp
void AppController::testSelectedProxiesAsync(const std::vector<std::string>& indexIds, wxEvtHandler* wxHandler) {
    if (indexIds.empty()) return;
    ProxyBatchTester* pbt = proxyBatchTester_.load();
    if (isRunning_ && pbt != nullptr) {
        // 有存活池 → 全部 inject（Scheme B）
        for (const auto& id : indexIds) {
            if (pbt->insertProxy(id)) {
                Logger::write("High-priority proxy inserted into running pool: " + id, LogLevel::REPORT);
                if (wxHandler) wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "Inserted high-priority proxy: " + id));
            }
        }
        return;
    }
    // 无存活池 → 创建 count=N 的池（把所有选中代理放入 proxies_）
    cancelRequested_ = false;
    taskQueue_.submit(task::TaskType::TEST_ALL, [this, indexIds, wxHandler](task::TaskContext& ctx) {
        (void)ctx;
        ScopeGuard<std::atomic<bool>> _guard{isRunning_};
        isRunning_ = true;
        ScopeGuard<std::atomic<int>> _taskGuard{currentTaskType_};
        currentTaskType_.store(static_cast<int>(task::TaskType::TEST_ALL));
        auto tester = std::make_unique<ProxyBatchTester>(db_, config_, "", &cancelRequested_, &netMon_);
        tester->loadProxies(indexIds);   // NEW: 接受代理列表
        proxyBatchTester_.store(tester.get());
        tester->setRowsUpdatedCallback([wxHandler](const std::vector<std::string>& ids) {
            if (wxHandler) wxQueueEvent(wxHandler, new ProxyRowsUpdatedEvent(ids));
        });
        doTestAllProxies(wxHandler, tester.get());
        proxyBatchTester_.store(nullptr);
    }, 1, task::TaskCallbacks{ .onQueued = [wxHandler](const task::TaskHandle& h) {
        Logger::write("Task queued: " + h.taskId, LogLevel::INFO);
        if (wxHandler) wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "Queued: " + h.taskId));
    }});
}
```

#### 3.1.2 新增 `ProxyBatchTester::loadProxies` 接口（B1）

```cpp
// ProxyBatchTester.h
void loadProxies(const std::vector<std::string>& indexIds);  // 从 DB 加载指定 indexId 列表到 proxies_
```

实现（ProxyBatchTester.cpp）：从 `ProfileitemDAO::getAll()` 过滤 indexId ∈ indexIds，填充 `proxies_`/`totalProxies_`，与 `runWithIndexId` 的加载逻辑一致。

#### 3.1.3 `testSingleProxyAsync` 无 running pool 分支（B2）

```cpp
void AppController::testSingleProxyAsync(const std::string& indexId, wxEvtHandler* wxHandler) {
    ProxyBatchTester* pbt = proxyBatchTester_.load();
    if (isRunning_ && pbt != nullptr && pbt->insertProxy(indexId)) {
        // 注入 running pool（已有）
        ...
        return;
    }
    // 无 running pool：检查 TaskQueue 是否已有排队的 TEST_SINGLE_PROXY / TEST_ALL 任务
    if (taskQueue_.hasPending(task::TaskType::TEST_SINGLE_PROXY) ||
        taskQueue_.hasPending(task::TaskType::TEST_ALL)) {
        // 追加到 pending 任务的代理列表（更新队列汇总信息，总数+1）
        // 需要 TaskQueue 扩展：pendingTasks() 返回 pending 列表
        auto pending = taskQueue_.pendingTasks();
        for (auto& t : pending) {
            if (t.type == task::TaskType::TEST_SINGLE_PROXY || t.type == task::TaskType::TEST_ALL) {
                // 通过 TaskHandle.taskId 反查 AppController 维护的 pendingIndexIds_
                // （TaskQueue 不直接持有代理列表；代理列表由 AppController 管理）
                break;
            }
        }
        pendingIndexIds_.push_back(indexId);  // AppController 成员
        pendingIndexIdsMutex_.lock();
        Logger::write("Proxy " + indexId + " queued for pending task (pending total: " + std::to_string(pendingIndexIds_.size()) + ")", LogLevel::INFO);
        pendingIndexIdsMutex_.unlock();
        return;
    }
    // 无 pending 任务 → 创建新池（count=1，keepXrayAlive_=true）
    cancelRequested_ = false;
    taskQueue_.submit(task::TaskType::TEST_SINGLE_PROXY, [this, indexId, wxHandler](task::TaskContext& ctx) {
        ...
        doTestSingleProxy(indexId, wxHandler, true);
    }, 2, task::TaskCallbacks{ .onQueued = ... });
}
```

#### 3.1.4 TaskQueue 接口扩展（B2）

```cpp
// TaskQueue.h 新增
bool hasPending(TaskType type) const;                          // 是否有排队的指定类型任务
std::vector<TaskHandle> pendingTasks() const;                  // 返回所有 pending 任务列表
```

实现：在 `submit()` 入队时记录 TaskHandle（status=PENDING/QUEUED）；`workerLoop` 取任务时从 pending 列表移除。`hasPending(type)` 遍历 pending 列表检查 type 匹配。

#### 3.1.5 多选入口更新（ProxyListPanel.cpp）

`onTestProxy` 改为收集选中 indexId 列表后调用 `controller_->testSelectedProxiesAsync(indexIds, this)`，而非逐个 `testSingleProxyAsync`。

**关键数据结构**：
- `ProxyBatchTester` 新增 `std::vector<std::string> pendingIndexIds_`（queued 但未入池的代理）+ `std::mutex pendingMutex_`。
- `AppController` 新增 `std::atomic<ProxyBatchTester*> livePool_`（统一命名，替代 proxyBatchTester_）。

### 3.2 Phase B — 统一日志格式

所有测试类型统一使用以下日志行：

| 事件 | 格式 |
|---|---|
| 测试开始 | `[Test] START type=<single|batch|multiselect|online> total=N` |
| 单代理进度 | `[Testing] i/total: addr:port (protocol)` |
| 单代理结果 | `[Result] addr:port (protocol) OK latencyMs ms` / `[Result] addr:port (protocol) FAIL reason` |
| 汇总 | `[Summary] Total: X / Success: Y / Failed: Z / Remaining: W` |

- `ProxyBatchTester` 的 worker 日志（当前 `[Worker-N] [n/total]`）改为 `[Testing] n/total: addr:port (protocol)`。
- `ProxyTester::test()` 的 curl 失败日志统一为 `[Result] ... FAIL reason`。
- `doTestOnlineProxies` 的 `Online proxies test finished` 改为 `[Summary] ...` 格式。

### 3.3 Phase C — 统一完成汇总（所有退出路径）（修订 B3）

所有测试完成路径（正常/强制终止/程序关闭）均打印：

```
[Summary] Total: X / Success: Y / Failed: Z / Remaining: W (exit: normal|cancelled|crash|shutdown)
```

**汇总打印位置**（修订 B3）：统一在 `ProxyBatchTester` **析构函数**中打印，exit reason 由析构上下文推断：

- 正常完成：`runInternal()` 末尾设置 `exitReason_ = ExitReason::Normal`，析构时打印 `[Summary] ... (exit: normal)`。
- 强制终止（cancelTest）：`cancelRequested_` 置位 → teardown 完成后设置 `exitReason_ = ExitReason::Cancelled`，析构时打印 `[Summary] ... (exit: cancelled)`。
- 程序关闭：`main_gui.cpp` cleanup 调用 `XrayManager::release()` 前，若 `proxyBatchTester_` 非空，先设置 `exitReason_ = ExitReason::Shutdown`，析构时打印 `[Summary] ... (exit: shutdown)`。

`ExitReason` 枚举（`ProxyBatchTester.h`）：
```cpp
enum class ExitReason { Normal, Cancelled, Crash, Shutdown };
```

`printSummary()` 签名改为 `printSummary(ExitReason reason)`，析构函数调用 `printSummary(exitReason_)`。

**注意**：detach 的 worker 计入 "Remaining"（I3）——有界 teardown detach 后，detach 的 worker 未更新计数，summary 反映实际处理数（Total - Success - Failed = Remaining）。

### 3.4 Phase D — 队列汇总信息

`TaskQueue` 新增：
- `queueDepth()` 已有（pending 数）。
- 新增 `QueueStats { int queued; int running; int completed; int failed; int cancelled; } getStats();`
- `AppController` 在状态栏显示 `Queue: N pending`（已有 `Task queued: <id>` INFO 日志）。

## 4. 实施顺序

| 阶段 | 内容 | 优先级 | 预估 |
|---|---|---|---|
| A | 统一存活队列检查 + 多选创建 count=N 池 | P0 | 2-3 天 |
| B | 统一日志格式 | P1 | 1-2 天 |
| C | 统一完成汇总（所有退出路径） | P1 | 1 天 |
| D | 队列汇总信息 | P2 | 0.5 天 |

## 5. 验收标准

1. 多选 69 个代理 → 右键测试 → 日志只出现 **1 次** `XrayManager::start: count=69`（或 count=4 池），而非 69 次 `count=1`。
2. 所有测试类型日志使用统一的 `[Testing]`/`[Result]`/`[Summary]` 前缀。
3. 测试完成后（正常/取消/关闭）均打印 `[Summary] Total: X / Success: Y / Failed: Z / Remaining: W (exit: ...)`。
4. 单代理测试 queued 时复用存活实例（日志 `reusing instance 0`）。

## 6. 风险与回滚

- Phase A 改动 `ProxyBatchTester` 的 `proxies_`/`proxiesQueue_` 初始化逻辑，需确保 `totalProxies_` 在 inject 时正确增长（现有 `totalProxies_.fetch_add(1)` 机制可复用）。
- Phase B/C 改动日志字符串，需同步更新 UI 面板的正则/解析（如有）。
- 回滚：每个 Phase 独立 commit，可单独 revert。