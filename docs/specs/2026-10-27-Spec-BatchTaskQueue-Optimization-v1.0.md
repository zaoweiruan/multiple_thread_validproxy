---
title: "Batch Task Queue Optimization — 模块拆分 / 批量任务队列管理 / 互斥任务关系清理"
date: 2026-10-27
type: spec
status: draft
version: v1.0
---

# 批量任务队列优化方案 (Batch Task Queue Optimization)

## 0. 背景与目标

本项目（`validproxy`）当前的长时异步操作（批量测试、订阅更新、去重、数据库同步、自动任务流水线、地区解析、查找代理）全部共享 `AppController::workerThread_` 单线程 + `isRunning_` 原子标志，**天然互斥**；各操作入口各自做 `AsyncOperationGuard` 重入防护，UI 通过 `OperationType`/`setOperationState()` 禁用冲突按钮。这种设计在功能上可用，但存在三类结构性问题：

1. **模块过大或职责过多**：`AppController`（~180KB）、`ProxyBatchTester`、`SubitemUpdaterV2`（66KB）、`XrayApi`（112KB）同时承担门面、编排、底层实现、协议细节、持久化、线程调度、UI 事件适配；已有 `docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` 提出拆分但未落地，且未覆盖批量任务队列。
2. **批量任务无队列管理**：测试/更新/去重/同步/导出/地区解析各自有独立入口与取消标志，任务类型散落在 `AutoTaskStepType`、`OperationType`、`ProxyBatchTester` 内部状态；无法排队、无法优先级、无法观测、无法在操作结束后自动衔接下一步。
3. **互斥任务关系混乱**：`isRunning_` 与 `onlineProbeRunning_`、`probeThread_`、`adoptInFlight_`、`cancelRequested_`、各 `std::mutex`（`standaloneMutex_`、`poolMutex_`、`configMutex_`、`staleConfigMutex_`）语义重叠；"用户操作 vs 周期探活"、"纳管 vs 启动"、"读 vs 写" 的互斥/并存关系散落在代码注释与 bugfix 文档中，缺乏单一真源。

**目标**：在不破坏现有 CLI/GUI 调用链的前提下，建立统一的批量任务队列与任务类型体系，拆分过大的模块，清理互斥任务关系，使任务调度、取消、观测、互斥判定都有单一真源。

## 1. 现状分析（证据）

### 1.1 模块过大/职责过多（代码事实）

| 模块 | 当前规模 | 混杂职责 |
|------|---------:|---------|
| `src/ui/AppController.h/.cpp` | ~182KB / ~4700 行 | UI 门面 + DB 协调 + 订阅/代理/测试/查找/同步/导出/去重/AutoTask/独立代理/代理池/网络监控/纳管 |
| `src/SubitemUpdaterV2.cpp` | 66KB | 订阅更新编排 + 解析 + 去重 + 导入 + DB 同步 + Xray 生命周期 |
| `src/XrayApi.cpp` | 112KB | gRPC/CLI 注入 + 编码 + 协议 + 错误处理 + 重试 |
| `src/ProxyBatchTester.cpp` | 35KB | SQL 模板替换 + Xray 生命周期 + 线程池 + 单代理测试 + DB 写入 + 取消 + 汇总 |
| `src/RegionBatchResolver.cpp` | ~20KB | 目标加载 + 工作队列 + worker + 缓冲写入 + 进度 |

已有 `docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` 提出 5 阶段拆分（ConfigReader → ShareLink → ConfigGenerator → ProxyBatchTester → AppController），但**未落地到批量任务队列层**，且未定义任务类型/互斥矩阵。

### 1.2 批量任务现状（代码事实）

- 共享执行通道：`AppController::workerThread_` + `isRunning_` + `cancelRequested_`；所有 async 入口走 `ui::AsyncOperationGuard`（`include/ui/AsyncOperationGuard.h`）。
- 任务种类分散：
  - `AutoTaskStepType`（`include/AutoTaskManager.h:13`）：`UPDATE_ALL / TEST_ALL / DEDUP / SYNC / EXPORT / RESOLVE_REGION`
  - `OperationType`（`src/ui/MainFrame.h:29`）：`NONE / TEST / UPDATE / FIND / SYNC / AUTOTASK / RESOLVE_REGION`
  - `ProxyBatchTester`：`run / runWithSubId / runWithIndexId`
  - `RegionBatchResolver::run(subId)`
  - `AppController` async：`updateSubscriptionAsync / updateAllSubscriptionsAsync / testSubscriptionAsync / testSingleProxyAsync / testAllProxiesAsync / testOnlineProxiesAsync / findFirstProxyAsync / findBestProxyAsync / findProxyByIndexIdAsync / syncDatabasesAsync / runAutoTaskAsync / resumeAutoTaskAsync / resolveRegionsBatchAsync / resolveSingleProxyRegionAsync`
- 取消标志：`cancelRequested_`（用户操作）、`externalCancel_`（AutoTask/RegionBatchResolver 共享）、网络恢复 30s 超时置应用级取消（`docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md`）。
- 队列：`ProxyBatchTester::proxiesQueue_`、`RegionBatchResolver::workQueue_` 仅在单次批处理内部使用，**不跨任务**。

### 1.3 互斥任务关系现状（代码事实）

| 关系 | 机制 | 代码位置 |
|------|------|---------|
| 用户操作互斥 | `workerThread_` + `isRunning_` + `AsyncOperationGuard` | `AppController.cpp:356-370` 等 |
| 周期探活独立 | `probeThread_` + `onlineProbeRunning_`，不占 `workerThread_` | `AppController.cpp:625-660` |
| 用户操作优先探活 | 手动测试前 `cancelRequested_=true; probeThread_.join()` | `AppController.cpp:649-655` |
| 纳管 in-flight | `adoptInFlight_` + `ui::ScopeGuard` | `AppController.h:312` |
| 配置互斥 | `configMutex_`（写与快照读） | `AppController.h:296` |
| 独立代理/代理池 | `standaloneMutex_` / `poolMutex_` | `AppController.h:314,320` |
| 配置陈旧报告 | `staleConfigMutex_` | `AppController.h:335` |
| DB 切换守卫 | `isRunning()` ∨ `isOnlineProbeRunning()` | `MainFrame.cpp:1194`（`DatabaseSwitchPolicy`） |
| 在线探活与手动测试 | `onlineProbeRunning_` 活标志 + 手动优先 join | `docs/specs/2026-09-16-Spec-ProbeNonBlocking-v1.0.md` |

问题：互斥判定分散在 UI 层（`MainFrame` 弹窗）、Controller 层（`AsyncOperationGuard`）、服务层（`NetworkMonitor`）三处，且 `isBusy_`（计划中）与 `isRunning_`（实际）语义重叠；`OperationType`（UI 按钮态）与 `AutoTaskStepType`（流水线步骤）与运行时任务类型三套并存。

## 2. 设计方案

### 2.1 模块拆分（继承并落地已有 spec）

在 `docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` 基础上，**新增任务队列层**作为 AppController 与具体业务之间的调度中枢：

```
UI (MainFrame / Panels)
   │  TaskEnqueueRequest (TaskType, params, priority, onProgress, onComplete)
   ▼
TaskQueue (new)  ←── TaskScheduler (new)  ←── MutexPolicy (new)
   │
   ├── SubscriptionService
   ├── ProxyTestService  → ProxyBatchTester (facade) → ProxyBatchQuery / XrayWorkerPool / ProxyTestWorker / Sink
   ├── DatabaseMaintenanceService (dedup/sync)
   ├── ShareLinkExportService
   ├── RegionResolutionService
   └── AutoTaskService (steps → TaskQueue 子任务)
```

**依赖方向**：`TaskQueue → NetworkMonitor`（只读查询），`NetworkMonitor` 不依赖 `TaskQueue`，避免循环依赖。

**拆分原则**（继承已有 spec §3 约束）：
- C++17，禁止 `auto` 类型推导。
- 保留现有 public API（`AppController` 门面方法不变），内部委托。
- 先拆纯逻辑，再拆线程/Xray。
- 先写 characterization tests，再迁移。

**新增服务层文件**（继承已有 spec §5.2，并扩展）：

| 新服务 | 职责 | 来源 |
|--------|------|------|
| `TaskQueue` / `TaskScheduler` | 统一任务入队、调度、取消、观测 | 新增（本方案核心） |
| `MutexPolicy` | 互斥矩阵的单一真源 | 新增 |
| `SubscriptionService` | 订阅 CRUD + 更新 | `AppController` |
| `ProxyTestService` | 批量/单代理测试、Find | `AppController` + `ProxyBatchTester` |
| `RegionResolutionService` | 地区解析批处理 | `RegionBatchResolver` |
| `DatabaseMaintenanceService` | dedup / sync | `AppController` |
| `ShareLinkExportService` | 导出分享链接 | `AppController` |
| `AutoTaskService` | 流水线编排 → 子任务 | `AutoTaskManager` |

### 2.2 任务队列管理（核心）

#### 2.2.1 统一任务类型 `TaskType`

合并 `AutoTaskStepType` 与 `OperationType` 为单一枚举（新增 `include/task/TaskType.h`）：

```cpp
enum class TaskType {
    NONE,
    UPDATE_SUBSCRIPTION,   // 单订阅更新
    UPDATE_ALL,            // 全部订阅更新
    TEST_SUBSCRIPTION,     // 单订阅测试
    TEST_SINGLE_PROXY,     // 单代理测试
    TEST_ALL,              // 全部代理测试（容器任务，拆解为 TEST_SINGLE_PROXY 子任务）
    TEST_BATCH,            // 批量测试容器任务（生成列表 + 聚合结果）
    TEST_ONLINE,           // 在线代理探测
    FIND_FIRST,            // 查找首个可用
    FIND_BEST,             // 查找最佳
    FIND_BY_INDEX,         // 按 indexId 查找
    DEDUP,                 // 去重
    SYNC_DB,               // 数据库同步
    EXPORT_SHARE_LINKS,    // 导出分享链接
    RESOLVE_REGION,        // 地区解析
    GENERATE_CONFIG,       // 生成配置
    AUTO_TASK,             // 自动任务流水线（组合任务）
};
```

映射规则：`AutoTaskStepType::UPDATE_ALL → TaskType::UPDATE_ALL`；`OperationType::TEST → TaskType::TEST_ALL/TEST_SUBSCRIPTION`（按调用点区分）。

#### 2.2.2 任务生命周期与观测

```cpp
enum class TaskStatus { PENDING, QUEUED, RUNNING, COMPLETED, FAILED, CANCELLED, REJECTED };

struct TaskHandle {
    std::string taskId;          // 时间戳 + 随机数（复用 AutoTaskManager 生成方式）
    TaskType type;
    TaskStatus status;
    int priority;                // 数值越大优先级越高：0=低（周期探活），1=普通，2=高（手动用户操作）
    std::string createdAt;
    std::string startedAt;
    std::string completedAt;
    std::string error;
    // 容器/子任务追踪（方案 A）
    std::string parentTaskId;     // 容器任务 ID（子任务填写）
    int childTotal{0};            // 子任务总数（容器任务填写）
    int childCompleted{0};        // 已完成子任务数
    int childFailed{0};           // 失败子任务数
    // 观测
    int progressCurrent{0};
    int progressTotal{0};
    std::string progressMessage;
};
```

#### 2.2.3 队列与调度

新增 `TaskQueue`（`include/task/TaskQueue.h` / `src/task/TaskQueue.cpp`）：

- **单 worker 线程**（继承现有 `workerThread_` 语义）+ **优先级队列**（`std::priority_queue<TaskEntry>`，按 priority 降序、createdAt 升序）。
- **提交**：`submit(TaskType, params, priority, callbacks) → TaskHandle`；若当前有运行中任务且互斥矩阵判定冲突 → 状态 `REJECTED` 并回调 `onRejected`（替代现有 `AsyncOperationGuard` 的 `REJECT:` 消息）。
- **取消**：`cancel(taskId)` / `cancelCurrent()`；取消标志统一为 `TaskQueue::cancelRequested_`，各业务通过 `isCancelled()` 回调查询。
- **观测**：`getQueueDepth()`、`getCurrentTask()`（返回副本）、`getHistory(size)`；UI 通过 `TaskProgressEvent` / `TaskCompletedEvent` 接收进度。`TaskHandle` 通过 `wxQueueEvent` **按值复制**投递到 UI 线程；UI 线程不持有 `TaskHandle` 引用，避免 data race。
- **事件节流（I3）**：进度事件按时间（≥100ms）或数量（≥50 个子任务）合并发送，避免细粒度子任务导致的事件风暴。
- **持久化**：复用 `AutoTaskManager` 的 state file 机制，扩展为通用 `TaskQueueState`（JSON），支持崩溃恢复。

**批量任务（测试、批量更新）全部走队列**：
- `testAllProxiesAsync` → `submit(TaskType::TEST_ALL, ...)`
- `updateAllSubscriptionsAsync` → `submit(TaskType::UPDATE_ALL, ...)`
- `resolveRegionsBatchAsync` → `submit(TaskType::RESOLVE_REGION, ...)`
- `AutoTask` 流水线 → `submit(TaskType::AUTO_TASK, ...)`，内部将 steps 拆解为子任务依次入队（而非单线程顺序执行）。

#### 2.2.4 批量测试的细粒度子任务（方案 A，按用户裁决）

批量测试不作为不可分割的单任务执行，而是拆解为“容器任务 + 每代理子任务”，以便高优先级测试在代理边界插入并优先执行，**不终止**批量任务：

- **`TEST_BATCH(subId/indexIds)`（容器任务）**  
  - 负责生成待测代理列表、为每个代理入队一个 `TEST_SINGLE_PROXY(indexId, priority=普通/1)` 子任务、聚合结果并更新汇总/状态栏。  
  - 容器任务状态为 `RUNNING`，直到所有子任务完成；取消容器任务 = 取消容器 + 取消尚未执行的子任务（已执行结果保留）。

- **`TEST_SINGLE_PROXY(indexId)`（子任务）**  
  - 单代理测试的唯一实现入口（批量测试与手动单代理测试复用）。  
  - 用户手动测单个代理 → 入队 `TEST_SINGLE_PROXY(priority=高/2)`，在批量测试的代理边界被优先执行，无需取消批量任务。

- **插入语义**  
  - 高优先级单代理任务在 `TaskQueue` 优先级队列中排在普通批量子任务之前；批量容器任务继续运行，其子任务按优先级被调度器交错执行。  
  - 不抢占正在执行的单个代理测试（避免 Xray 实例/DB 写入中途竞态）；抢占只发生在子任务边界。

- **防饥饿**  
  - 对等待超过阈值的普通子任务做 aging（提升 priority）或设置最大等待时间，避免高优先级插队饿死批量测试。  
  - 具体算法（I5）：等待超过 30s 的普通子任务 priority +1（上限 2）；同优先级内按 `createdAt` FIFO；或使用公平队列（FIFO within same priority）。

- **容器聚合与取消（B1）**  
  - `TaskHandle` 新增字段：`parentTaskId`、`childTotal`、`childCompleted`、`childFailed`。  
  - 容器任务 (`TEST_BATCH`) 通过子任务的 `TaskCompletedEvent`/`TaskFailedEvent` 更新计数；`childCompleted == childTotal` 时容器状态转 `COMPLETED`。  
  - 取消语义：`cancel(containerTaskId)` → 设置容器 `CANCELLED` + 调用 `TaskQueue::cancelChildren(containerTaskId)` 取消尚未开始的子任务；正在执行的子任务运行至完成或其内部取消点（§2.3.4）。  
  - 子任务失败不阻断容器：记录 `childFailed`，容器最终状态仍 `COMPLETED`（部分成功），失败详情写入日志。

- **批量子任务粒度与事件节流（B2）**  
  - 默认每 `batchSize`（配置项，默认 10，可调 1–100）个代理为一个子任务，避免 50k+ 代理逐个入队的开销。  
  - UI 进度事件按时间（≥100ms）或数量（≥50 个子任务）节流发送，避免事件风暴。  
  - `batchSize` 为 1 时退化为每代理一个子任务（调试/细粒度模式）。

- **类型语义（B3）**  
  - `TEST_ALL`：用户入口类型（UI/CLI `testAllProxiesAsync`），映射为 `TEST_BATCH` 容器任务。  
  - `TEST_BATCH`：内部容器任务类型，负责生成列表、入队子任务、聚合结果。  
  - `ProxyTestService` 是映射层：`submit(TEST_ALL)` → 内部 `submit(TEST_BATCH)`；外部不应直接提交 `TEST_BATCH`。
#### 2.2.5 与现有 `AutoTaskManager` 的关系:

`AutoTaskManager` 保留为"流水线编排器"，但其 `run()`/`resume()` 改为向 `TaskQueue` 提交子任务序列，并监听子任务完成事件推进下一步。

**有序依赖（I2）**：AutoTask 子任务链使用 `dependsOn` 字段（或顺序队列）保证步骤顺序；高优先级任务可插队但不破坏 AutoTask 的步骤顺序；AutoTask 容器任务状态仍按步骤顺序推进。

**向后兼容策略（I1/D3）**：老 `autotask_state.json` 字段（`task_id`、`created_at`、`current_step_index`、`steps`、`completed`、`cancelled`、`sql_query_id`、`sql_by_subid_id`）全部保留；新增字段（`queue_depth`、`task_type`、`priority`、`sub_task_index`）设默认值。加载时按字段存在性分支，保证升级后可 resume 旧任务。`resume()` 恢复"当前执行到第 N 个子任务"的状态，通过 `sub_task_index` 记录子任务进度；若子任务已入队但未完成，重新提交并跳过已完成的前置子任务。

### 2.3 互斥任务关系清理

新增 `MutexPolicy`（`include/task/MutexPolicy.h` / `src/task/MutexPolicy.cpp`）作为**唯一真源**，定义：

#### 2.3.1 资源锁

| 资源 | 锁 | 持有者 |
|------|----|--------|
| `workerThread_` / 运行中任务 | `TaskQueue` 内部互斥 | TaskQueue |
| `db_` 句柄（写） | `dbWriteMutex_` | DatabaseConnectionService |
| `config_` | `configMutex_` | ConfigService |
| `standaloneProxies_` | `standaloneMutex_` | StandaloneProxyManager |
| `proxyPool_` | `poolMutex_` | StandaloneProxyManager |
| `staleConfigWarned_` | `staleConfigMutex_` | StandaloneProxyManager |
| `discardedStandalonePids_` | `standaloneMutex_` | StandaloneProxyManager |

#### 2.3.2 任务互斥矩阵与副作用判定

`MutexPolicy` 不仅返回 bool，还封装副作用判定（join/cancel probe 等），避免 UI 层写特殊分支：

```cpp
enum class ConcurrencyDecision { ALLOW, REJECT, JOIN_PROBE_THEN_RUN, CANCEL_PROBE };

ConcurrencyDecision MutexPolicy::decide(TaskType a, TaskType b, const SystemState& state);
```

- `ALLOW`：可并发。
- `REJECT`：拒绝新任务（返回 REJECTED）。
- `JOIN_PROBE_THEN_RUN`：先 join 周期探活线程，再运行（手动测试优先探活）。
- `CANCEL_PROBE`：取消探活后运行。

副作用（join/cancel probe）由 `TaskQueue` 内部执行，UI 层不感知。

`MutexPolicy::canRunConcurrently(TaskType a, TaskType b) → bool` 保留为 `decide(...) != REJECT` 的便捷包装：

|  | TEST_* | UPDATE_* | DEDUP | SYNC_DB | EXPORT | RESOLVE_REGION | FIND_* | TEST_ONLINE |
|--|--------|----------|-------|---------|--------|----------------|--------|-------------|
| TEST_* | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌* |
| UPDATE_* | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌* |
| DEDUP | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌* |
| SYNC_DB | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌* |
| EXPORT | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| RESOLVE_REGION | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌* |
| FIND_* | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌* |
| TEST_ONLINE (周期探活) | ❌* | ❌* | ❌* | ❌* | ✅ | ❌* | ❌* | ✅** |

说明：
- `*`：周期探活（`TEST_ONLINE` silent）不占 `workerThread_`，但与用户操作**并存**时按 `docs/specs/2026-09-16-Spec-ProbeNonBlocking-v1.0.md` 规则：保存配置拒绝、切库拒绝（追加 `isOnlineProbeRunning()`）、手动测试优先 join 探活。
- `**`：周期探活之间可并存（单实例，`onlineProbeRunning_` 守卫）。
- `EXPORT` 为只读查询，可与任何任务并存（但需 DB 读锁）。

#### 2.3.3 读写分离（选定方案 A）

SQLite 单连接同一时刻只能有一个活跃 statement（除非 `SQLITE_OPEN_FULLMUTEX`）。为避免引入连接管理复杂度，本方案**选定方案 A**：

- **方案 A（推荐，本方案采用）**：保持单连接 + 单 worker 串行化 DB 访问；`std::shared_mutex`（`dbReadMutex_`/`dbWriteMutex_`）**仅保护内存缓存**（`loadSubscriptions` 缓存、计数缓存等），不允许多线程并发调用 `sqlite3`。
- 方案 B（备选，暂不采用）：为读操作创建只读连接（`sqlite3_open_v2` + `SQLITE_OPEN_READONLY`），读写真正并发，但需管理连接生命周期与 schema 迁移同步；留作后续性能优化。

因此：
- DB 读操作使用 `dbReadMutex_`（共享锁）保护缓存；实际 `sqlite3` 调用仍在单 worker 线程内串行执行。
- DB 写操作使用 `dbWriteMutex_`（独占锁）保护缓存与写入；与读锁互斥。
- 现有 `sqlite3` 连接已启用 WAL（`DatabaseConnectionService::applyPragmas`），读写不阻塞；互斥锁仅保护内存缓存与句柄切换。

#### 2.3.4 取消语义统一

- `TaskQueue::cancelRequested_` 为唯一取消标志；业务代码通过 `TaskContext::isCancelled()` 查询。
- 网络恢复 30s 超时 → 置 `cancelRequested_=true` + 置应用级取消（继承 `docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md` 的 D1 裁决）。
- 周期探活的取消不复用用户取消标志（继承现有设计：`testOnlineProxiesAsync(silent=true)` 不消费 `cancelRequested_`）。
- **取消检查点（I4）**：取消标志在每个子任务开始前检查；正在执行的子任务运行至完成或其内部取消点（如 `ProxyTester::test` 的 curl 超时/externalCancel 检查），不强制中断。容器任务取消传播至未开始的子任务。

#### 2.3.5 UI 状态与任务类型对齐

- `OperationType`（UI 按钮态）保留为粗粒度 UI 态，由 `TaskQueue::getCurrentTask().type` 映射（见下）；`TaskCompletedEvent` 额外携带原始 `TaskType` 供 UI 显示详细文案（如"测试单订阅" vs "测试全部"）。如需更细粒度按钮态，可扩展 `OperationType`，但需评估 UI 影响。
  - `TaskType::TEST_* → OperationType::TEST`
  - `TaskType::UPDATE_* → OperationType::UPDATE`
  - `TaskType::FIND_* → OperationType::FIND`
  - `TaskType::SYNC_DB → OperationType::SYNC`
  - `TaskType::AUTO_TASK → OperationType::AUTOTASK`
  - `TaskType::RESOLVE_REGION → OperationType::RESOLVE_REGION`
  - 无运行任务 → `OperationType::NONE`
- `setOperationState()` 仅反映当前任务类型，不再独立管理。
- `isBusy_`（计划中的）**不新增**，统一用 `TaskQueue::isRunning()`；UI 通过 `TaskProgressEvent`/`TaskCompletedEvent` 刷新。

#### 2.3.6 周期探活生命周期（probeThread_）

周期探活是独立线程，不走 `TaskQueue`。`TaskQueue` 需查询探活状态（如 DB 切换守卫），但不直接依赖 `AppController`。实现方式：`TaskQueue` 持有 `std::function<bool()> probeRunningQuery` 回调，由 `AppController` 注入 `&AppController::isOnlineProbeRunning`；或由 `MutexPolicy` 持有 probe 状态查询。`MutexPolicy::decide` 在 `JOIN_PROBE_THEN_RUN` 分支内部调用该查询并执行 join。

### 2.4 互斥任务关系清理（额外）

- **纳管 vs 启动**：`adoptInFlight_` 保留，但 `StandaloneProxyManager` 内部化；UI 不直接访问。
- **代理池 vs 独立代理**：`poolMutex_`/`standaloneMutex_` 保留在 `StandaloneProxyManager`，外部通过服务方法访问。
- **配置 vs 探活**：`configMutex_` 保留在 `ConfigService`，探活通过 `ConfigService::snapshot()` 读取。
- **DB 切换**：`DatabaseSwitchPolicy` 保留，但判定依据改为 `TaskQueue::isRunning()` ∨ `TaskQueue::isProbeRunning()`。

## 3. 实施计划

### Phase 0：前置（1-2 天）
- 创建 `TaskType` / `TaskHandle` / `TaskStatus` 头文件（纯类型，可单测）。
- 创建 `MutexPolicy` + 单元测试（互斥矩阵真值表）。
- Characterization tests：记录现有 `AutoTaskManager`、`ProxyBatchTester`、`RegionBatchResolver` 的行为快照。

### Phase 1：TaskQueue 核心（2-3 天）
- 实现 `TaskQueue`（单 worker + 优先级队列 + 取消 + 观测）。
- 单元测试：入队/出队/取消/优先级/拒绝/观测。
- 不接入业务（并行开发）。

### Phase 1.5：完成已有 Responsibility Decomposition 的 Phase 0-4（B1）

`docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` 的 5 阶段拆分尚未落地（`src/service/` 仅 8 个轻量 service，`AppController` 仍 ~4700 行）。Phase 2 前需先完成其 Phase 0-4（ConfigReader/ShareLink/ConfigGenerator 拆分 + characterization tests）。

**回退策略（I6）**：若 Phase 0-4 未完成，`TaskQueue` 先以适配器模式接入现有 `AppController`（不等服务层拆分），服务层拆分作为后续迭代。

### Phase 2：服务层拆分（3-5 天，继承已有 spec）
- 按 `docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` Phase 1-3 落地 `ConfigReader`/`ShareLink`/`ConfigGenerator` 拆分（已有 spec，本方案不重复）。
- 新增 `SubscriptionService`、`ProxyTestService`、`RegionResolutionService`、`DatabaseMaintenanceService`、`ShareLinkExportService`。

### Phase 3.0：迁移策略（适配器模式，B2）

先让 `TaskQueue` 包装现有 `workerThread_`（适配器），保持 `AsyncOperationGuard` 行为不变；再逐步把 async 入口改为 `submit`；最后删除 `AsyncOperationGuard`。避免一次性替换导致 UI 事件/进度/取消时序回归。

### Phase 3：接入 TaskQueue（3-4 天）
- `AppController` async 方法改为 `taskQueue_.submit(...)` + 回调。
- `ProxyTestService::runBatch(subId)`：生成待测列表，入队 `TEST_BATCH` 容器任务；容器任务内部为每个代理入队 `TEST_SINGLE_PROXY(priority=普通/1)`，聚合结果后更新 UI。
- 手动单代理测试：`submit(TEST_SINGLE_PROXY, priority=高/2)`，在批量测试代理边界被优先执行。
- UI 事件处理改为监听 `TaskProgressEvent`/`TaskCompletedEvent`（携带 `TaskType` 与汇总）。
- `AutoTaskManager` 改为提交子任务序列。

### Phase 4：互斥关系清理与观测（2 天）
- `MutexPolicy` 接入 `TaskQueue` 与各 Service。
- DB 读写锁分离。
- UI 状态由任务类型映射。

### Phase 5：测试与验收（2 天）
- 全量 ctest + `build-and-test.bat`。
- 手工验收清单（见 §5）。

**总计**：~20-30 天（含 Phase 1.5 完成已有拆分）。

## 4. 风险与控制

| 风险 | 控制 |
|------|------|
| 改变任务执行顺序/取消语义 | TaskQueue 保留单 worker 语义，与现有 `workerThread_` 一致；先加 characterization tests |
| Xray 生命周期 | ProxyBatchTester 最后接入 TaskQueue；先提取 query/worker/counters |
| UI 调用链断裂 | AppController public API 不变，内部委托；`MainFrame` 调用方式不变 |
| C++17 兼容 / 禁止 auto | 重构完成后运行 `grep -R "\bauto\s+[A-Za-z_]"` 检查 |
| 文档流程 | 本 spec 登记 `docs/INDEX.md`；实施后更新 `docs/plans/project-plans-tracker.md` |

## 5. 验收标准

- [ ] `TaskType`/`TaskStatus`/`TaskHandle` 为唯一任务类型真源；`AutoTaskStepType`/`OperationType` 仅作为 UI/兼容映射。
- [ ] 所有批量任务（测试/更新/去重/同步/导出/地区解析）经 `TaskQueue::submit` 入队。
- [ ] 批量测试按方案 A 拆解为 `TEST_BATCH` 容器任务 + `TEST_SINGLE_PROXY` 子任务；高优先级单代理测试可在代理边界插入并优先执行，不终止批量任务。
- [ ] `MutexPolicy::canRunConcurrently` 单元测试覆盖全部矩阵单元。
- [ ] DB 读写锁分离；读操作可并发。
- [ ] 周期探活与用户操作的互斥/并存规则由 `MutexPolicy` 单一判定。
- [ ] 全量 ctest 通过。既有基线失败清单（不计入回归）：`XrayManagerStartupTest`、`RegionBatchResolverTest`（链接缺源）、`UI_PORTCLOSE`、`UI_FLOATINGWIDGET`（并行 flaky）、`NetworkMonitorTest`（无外网）。
- [ ] `build-and-test.bat` 全绿。
- [ ] 性能回归阈值：批量测试 1000 代理耗时 ≤ 当前基线 +10%；UI 操作响应延迟 ≤ 当前基线 +20%。

## 6. 与现有文档的关系

- 继承：`docs/specs/2026-06-18-Spec-Responsibility-Decomposition-v1.0.md`（模块拆分）。
- 继承：`docs/specs/2026-07-01-Spec-AppController-Responsibility-Decomposition-v1.0.md`（AppController 职责边界，5 个服务拆分；本 spec 是其"调度/队列/互斥"层扩展，非替代）。
- 继承：`docs/specs/2026-05-27-disable-conflict-ui-during-ops.md`（UI 禁用）。
- 继承：`docs/specs/2026-09-16-Spec-ProbeNonBlocking-v1.0.md`（探活非阻塞）。
- 继承：`docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md`（worker 停摆裁决）。
- 本 spec 是上述文档的"调度/队列/互斥"层补充，不替代它们。
---

*本方案为设计文档，实施前需经独立评审（见 `docs/reviews/2026-10-27-Review-BatchTaskQueue-Optimization-v1.1.md`）。*

---

## 7. 实施修订记录（as-built，2026-10-28）

本节记录实施阶段相对 §2.2.4「方案 A（子任务拆分）」的**最终落地形态**。方案 A 在实施后被否决，改为**方案 B（池内插入）**，本节为权威描述；§2.2.4 中「按代理拆分子任务」的描述不再代表当前实现。

### 7.1 为何放弃方案 A

方案 A 将批量测试拆成 N 个 `TEST_SINGLE_PROXY` 子任务进入 `TaskQueue` 优先级队列。实践中暴露出：

1. **Xray 实例池是基础设施，不是任务**：实例池一旦启动，其生命周期应覆盖整轮批量测试；把"每个代理一次测试"建模成独立任务，会导致实例池被反复启停，测试吞吐反而下降。
2. **优先级退化为手工排队**：每个测试都是同构任务时，所谓"高优先级插入"只是把新任务排到队尾之外，无法在**正在运行的池**内立刻插入一个代理。
3. **容器聚合成本高**：50k 代理规模的子任务入队/聚合/事件节流开销显著，收益不匹配复杂度。

### 7.2 方案 B（当前实现）

**核心语义**：`ProxyBatchTester` 即"测试池"。批量测试运行期间，池保持存活；高优先级单代理测试**直接插入池内的插入队列**，由空闲 worker 立即取走执行，无需新任务、无需等待整批结束。

| 机制 | 实现位置 | 说明 |
|------|----------|------|
| 池存活标志 | `ProxyBatchTester::running_`（atomic bool） | 池运行期间为 true；`insertProxy()` 仅在 true 时接受插入 |
| 插入队列 | `insertedProxies_` / `insertedConfigs_` / `insertedQueue_`（+ `insertedMutex_` / `insertedCv_`） | worker 每次取任务**先取插入队列**，再取普通队列 |
| 插入接口 | `bool ProxyBatchTester::insertProxy(const std::string& indexId)` | 从 DB 读取该代理、用 `ConfigGenerator` 生成其专属 Xray 配置、入队、`totalProxies_` +1、`insertedCv_.notify_all()`，日志 `Inserted high-priority proxy: <id>` |
| worker 取任务 | `ProxyBatchTester::workerThreadFunc` | 插入队列优先；普通队列空且 `running_` 为 true 时 50ms 轮询等待，不退出 |
| 池退出 | watcher 线程 | 普通队列清空后进入 20×100ms 宽限期；**期间若有新插入则重新计时**；宽限期结束才 `running_ = false` 并唤醒所有 worker |
| 配置正确性 | worker 内 `selectedConfig` | 插入代理使用 `insertedConfigs_[i]`；批次代理使用 `preGenConfigs_[i]`；两者边界检查分离（曾存在"插入代理误用批次配置"的严重缺陷，已修） |
| 失败隔离 | `insertedConfigFailed_` / `pregenFailedFlags_` | 配置生成失败 → 该代理结果记为 `PREGEN_FAILED`，不影响其他代理 |
| UI 入口 | `AppController::testSingleProxyAsync` | 快路径：`isRunning_ && proxyBatchTester_ != nullptr && insertProxy(id)` → 立即返回并提示"Inserted high-priority proxy"；慢路径：无池在跑时提交 `TEST_SINGLE_PROXY`(priority 2) |
| 池注册 | `proxyBatchTester_`（atomic 指针） | `testAllProxiesAsync`、`doTestSubscription`、AutoTask 测试步骤在 run 前后注册/清空 |

### 7.3 实时结果刷新（新增）

原实现仅在整批结束时刷新 UI。现增加增量刷新链路：

1. `ProxyBatchTester` 新增 `RowsUpdatedCallback`（`setRowsUpdatedCallback`），在结果批量落库成功后回调本次写入的 `indexId` 列表。
2. 新增 `ProxyRowsUpdatedEvent`（`src/ui/Events.h/.cpp`，`wxEVT_PROXY_ROWS_UPDATED`，载荷 `std::vector<std::string>`）。
3. `ProxyListPanel::onProxyRowsUpdated` → `refreshResultsFor(ids)`，仅重载受影响行（复用既有增量刷新实现）。
4. 批量测试（`testAllProxiesAsync`）、订阅测试（`doTestSubscription`）、AutoTask 测试步骤均已接入。

### 7.4 覆盖范围

池内插入在以下三条路径均可用：

- 工具栏"批量测试"（`testAllProxiesAsync` → `doTestAllProxies`）
- 订阅测试（`testSubscriptionAsync` → `doTestSubscription`，经 `runWithSubId`）
- 自动任务中的测试步骤（`AutoTaskManager::stepTestAll`，通过 `setPoolObserver` 回调把内部 tester 注册到 `AppController::proxyBatchTester_`）

### 7.5 与 §5 验收标准的差异

- §5 中「批量测试按方案 A 拆解为 `TEST_BATCH` 容器任务 + `TEST_SINGLE_PROXY` 子任务」**不再适用**，替换为：批量测试以单个 `TEST_ALL` 任务承载池生命周期，高优先级单代理测试通过 `ProxyBatchTester::insertProxy` 插入运行中的池。
- 其余验收标准（任务类型真源、MutexPolicy 单测、全量 ctest、`build-and-test.bat` 全绿）保持不变。

### 7.6 实施验证

- `cmake --build build --parallel 8`：全部 target 链接通过。
- `ctest -R "MutexPolicyTest|TaskQueueTest|LogStatistics|UnifiedMonitor"`：5/5 通过。
- `scripts/build-and-test.bat`：12/12 UI 用例通过（含 `UI_FLOATINGWIDGET`）。
