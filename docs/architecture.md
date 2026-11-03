# Architecture

> 本文档描述当前代码库（分支 `Module-splitting-and-optimization-of-batch-task-queues`，系统版本 1.5.1）的整体架构。
> 依据 `AGENTS.md` 路由表：所有功能修改/重构必须先更新设计文档，再登记 `docs/INDEX.md`。

---

## 1. 分层结构

```
┌─────────────────────────────────────────────────────────────┐
│  UI 层 (src/ui)  wxWidgets 3.2+ (wxMSW)                      │
│    MainFrame / SubscriptionPanel / ProxyListPanel / LogPanel │
│    AppController / Events / StandaloneFloatingWidget         │
├─────────────────────────────────────────────────────────────┤
│  service 层 (src/service)  纯业务编排，UI 无业务逻辑          │
│    ProxyTestService / SubscriptionService / AutoTaskService   │
│    ConfigService / ProxyListService / ShareLinkExportService  │
│    DatabaseConnectionService / DatabaseMaintenanceService     │
├─────────────────────────────────────────────────────────────┤
│  task 子系统 (include/task, src/task)  调度骨架               │
│    TaskType (统一任务枚举) / TaskQueue (单worker优先级队列)    │
│    MutexPolicy (互斥矩阵)                                    │
├─────────────────────────────────────────────────────────────┤
│  核心业务逻辑 (include/, src/)                               │
│    XrayManager / XrayInstance / XrayApi (gRPC)               │
│    ProxyBatchTester / ProxyTester / ProxyProbePool           │
│    ProxyFinder / ConfigGenerator / ConfigReader              │
│    SubitemUpdaterV2 / ShareLink / PortManager / UrlFetcher   │
│    AutoTaskManager / NetworkMonitor / RegionBatchResolver    │
│    StandaloneProxyPool / ProxyConnectivityVerifier           │
│    ProxyHealthEvaluator / ProxyScorer / BatchWorkerPolicy    │
├─────────────────────────────────────────────────────────────┤
│  数据层 (include/, src/)  SQLite DAO                          │
│    DatabaseHelper / ProfileitemDAO / SubitemDAO              │
│    ProfileExItemDAO / FullConfigTemplateDAO / SqlQuery       │
│    模型: Profileitem / Subitem / ProfileExItem               │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. 核心模块

### 2.1 Xray 核心控制

- **XrayManager**（`include/XrayManager.h`, `src/XrayManager.cpp`）：Xray 实例单例管理，`XrayManager::getInstance()` 返回全局单例。负责 `start(count, startPort, apiPort)` 启动共享实例池、`stopAll()` 清理。
- **XrayInstance**（`include/XrayInstance.h`, `src/XrayInstance.cpp`）：单个 Xray 进程生命周期维护（CreateProcessA + JobObject TerminateJobObject 清理）。
- **XrayApi**（`include/XrayApi.h`, `src/XrayApi.cpp`）：与 Xray 进行 gRPC API 通信（已修复 CreateProcessA 引起的 CMD 窗口闪烁问题）。

### 2.2 代理验证与批量测试

- **ProxyBatchTester**（`include/ProxyBatchTester.h`, `src/ProxyBatchTester.cpp`）：多线程并发测试调度引擎。
  - 共享 Xray 实例池：`xrayManager_ = XrayManager::getInstance(...)`（单例），池端口仅供批量测试使用。
  - **双注入队列（Scheme B）**：
    - `proxiesQueue_`（`queueMutex_`）— 批量代理索引（低优先级）。
    - `insertedQueue_`（`insertedMutex_` + `insertedCv_`）— 高优先级插入的代理索引。
    - worker 先取 `insertedQueue_`，再取 `proxiesQueue_`；`running_` 原子标志控制生命周期。
    - 并行配置容器：`preGenConfigs_`（批量）/ `insertedConfigs_` + `insertedConfigFailed_`（插入）。
    - `bool insertProxy(indexId)`：运行中插入高优先级代理，自行经 ConfigGenerator 生成配置。
    - watcher 线程：等待 `proxiesQueue_` 空后 20×100ms 宽限期（新插入会重置计时），再置 `running_=false`。
  - 实时结果刷新：`RowsUpdatedCallback` → `ProxyRowsUpdatedEvent` → `ProxyListPanel::onProxyRowsUpdated` → `refreshResultsFor(ids)`。
- **ProxyTester**（`include/ProxyTester.h`, `src/ProxyTester.cpp`）：单个代理测试核心（CURL + Xray 联动）。`test(socksPort, cancelFlag, ...)` 通过指定 SOCKS 端口发起 HTTP 探测。
- **ProxyProbePool**（`include/ProxyProbePool.h`）：共享池成员健康探测（拥有独立线程池，经 MutexPolicy 与 DB/config 互斥）。
- **ProxyConnectivityVerifier** / **ProxyHealthEvaluator** / **ProxyScorer** / **BatchWorkerPolicy**：连通性校验、健康度评估、评分与批量策略。
- **TestResultQueue**（`include/TestResultQueue.h`）：批量结果缓冲，BATCH_SIZE=50 / FLUSH_INTERVAL_MS=5000，flush 线程回调后批量写库。

### 2.3 代理发现与配置

- **ProxyFinder**（`include/ProxyFinder.h`, `src/ProxyFinder.cpp`）：代理查找策略（F / FMIN）。
- **ConfigGenerator**（`include/ConfigGenerator.h`, `src/ConfigGenerator.cpp`）：Xray JSON 配置文件动态生成。
- **ConfigReader**（`include/ConfigReader.h`, `src/ConfigReader.cpp`）：全局 `config.json` 解析。
- **SubitemUpdaterV2**（`include/SubitemUpdaterV2.h`, `src/SubitemUpdaterV2.cpp`）：订阅更新、解析与高效率去重。
- **ShareLink**（`include/ShareLink.h`, `src/ShareLink.cpp`）：分享链接导出与解析。
- **PortManager**（`include/PortManager.h`, `src/PortManager.cpp`）：并发测试中的本地端口动态分配与回收。
- **UrlFetcher**（`include/UrlFetcher.h`, `src/UrlFetcher.cpp`）：底层 cURL HTTP/HTTPS 请求器。**CurlEasyHandle**（header-only RAII 封装）。

### 2.4 独立代理与监测

- **StandaloneProxyPool**（`include/StandaloneProxyPool.h`）：独立代理进程池管理。
- **StandaloneAdoptionPolicy** / **StandaloneConfigPort** / **StandaloneProxyLogForwarder**：独立代理收养策略、端口解析、日志转发。
- **NetworkMonitor**（`include/NetworkMonitor.h`）：网络状态监测（在线探测 / 浮窗展示）。
- **RegionBatchResolver**（`include/RegionBatchResolver.h`）：批量区域解析。
- **AutoTaskManager**（`include/AutoTaskManager.h`, `src/AutoTaskManager.cpp`）：自动任务流水线，`stepTestAll()` 注册 poolObserver_ 以便高优先级插入。

### 2.5 基础设施

- **Logger**（`include/Logger.h`, `src/Logger.cpp`）：全局日志（TRACE < DEBUG < INFO < REPORT < WARN < ERR）。
- **LogStatistics**（`include/LogStatistics.h`）：日志统计。
- **CrashHandler**（`include/CrashHandler.h`）：崩溃捕获。
- **ProcessInspector** / **ProcessExitListener**：进程检查与退出监听。
- **DatabaseSwitchPolicy** / **DatabaseHelper**：数据库切换策略与数据访问。

### 2.6 辅助决策与历史记录

#### 2.6.1 ProxyRuntimeHistory（`include/ProxyRuntimeHistory.h`）

`ProxyRuntimeHistoryDAO` 维护 `proxy_runtime_history` 表 + `ProfileExItem` 的 3 个聚合列（`start_count` / `total_runtime_ms` / `crash_count`）。记录行字段：`indexId / startedAt / endedAt / exitCode / durationMs / source / pid / socksPort`；`endedAt` 为空 = 会话进行中。

- **事务契约**：明细行写入与聚合更新始终在**同一事务**内；嵌套事务安全用 `sqlite3_get_autocommit()`（仅在 autocommit 模式下才发 BEGIN/COMMIT/ROLLBACK），可安全参与调用方的外层事务。
- **启动**：`insertStart(indexId, startedAt, pid)` 插入明细行（ended_at=NULL）+ 同一事务内 `start_count++`；`insertStartWithPort(...)` 追加携带 SOCKS 端口的变体（socks_port<=0 存 NULL）。历史表是收养独立代理的**单一事实来源**：缺失实时行的会话追加新行而非 UPDATE 无关旧行。
- **退出**：`finalizeStop(historyId, endedAt, exitCode, durationMs)` 回填 ended_at/exit_code/duration_ms（仅当 ended_at IS NULL，二次调用为 no-op）+ 累加 `total_runtime_ms` / `crash_count`（exitCode==259 记为 crash）。
- **心跳**：`touchHeartbeat(historyId, durationMs)` 定期刷新进行中会话的 duration_ms（ended_at 保持 NULL），异常退出仍留下最新运行时长供评估。
- **收养匹配**：`findInProgressHistory(indexId, pid, startedAt)` 按 `(pid, startedAt)` 三元组匹配同一进程实例的进行中会话（**故意不匹配**不同 pid 或不同 startedAt → 开启全新会话）；`findInProgressHistoryLoose(indexId, pid)` 无 started_at 版本（用于丢弃无法解析的 dangling 进程）；`lookupSocksPort(indexId, pid, startedAt)` 恢复磁盘配置文件丢失时的 SOCKS 端口（startedAt 为空时按 (indexId, pid) 匹配）。
- **聚合读取**：`getRuntimeStats(indexId, ...)`；`getStartedAt(historyId)` 读取行原始 started_at（收养复用行时用于计算时长）。

**独立代理收养流程**：启动扫描发现 dangling 进程 → 校验配置与 DB 记录（`StandaloneAdoptionPolicy`，protocol 不匹配时 WARN）→ 按 (pid, startedAt) 匹配历史会话 → 复用或新开会话 → `lookupSocksPort` 恢复端口（history socks_port > 内存 socksPort > 磁盘配置文件）→ 运行期心跳刷新时长 → 退出时 `finalizeStop` 回填。

#### 2.6.2 BatchWorkerPolicy（`include/BatchWorkerPolicy.h`）

纯函数、无依赖的 worker 生命周期策略（自 `ProxyBatchTester` 抽出以支持单元测试；契约镜像 `tests/test_batch_worker_policy.cpp`；Bugfix 谱系 `docs/bugfix/2026-10-19-Bugfix-ProxyBatchTester-WorkerThreadStall-v1.0.md` + RCA）。

- **实例死亡裁决**（D3）：`InstanceHealthOutcome { Throttled, Relaunched, RelaunchFailed }` → `decideInstanceDead()` 映射为 `InstanceDeadDecision { ClearAndContinueRelaunched, ClearAndContinueThrottled, StopWorkerAndLog }`。重启成功→清标志继续服务；30s/apiPort 冷却命中→清标志继续（无需重启）；重启真正失败→停止该 worker 并记日志。
- **陈旧 tag 注入错误**（F5）：`isStaleTagInjectionError()` 仅匹配 gRPC "duplicate tag" 家族（`existing tag found` / `already exists` / `duplicate`）。这是语义错误，证明实例存活且可达，重启纯属折腾。**注意**：`xray api ado` 回退子进程 stdout 的 `output=adding: proxy` 文本不证明实例状态，**故意不**当作陈旧 tag 噪音。
- **端点不可达上报**：`shouldReportEndpointUnreachable(lastInjectionError, retryCount)` — retryCount<3 不报；>=3 且非陈旧 tag 错误才上报。
- **探测失败跨越阈值**（D5）：`shouldLogProbeFailureCrossing(priorConsecutiveFailures, probeFailStreak)` — 仅当 `probeFailStreak <= 0` 或 `priorConsecutiveFailures + 1 != probeFailStreak` 时不报；即**首次跨越**连续失败阈值时才 WARN，避免刷屏。

#### 2.6.3 ProxyHealthEvaluator（`include/ProxyHealthEvaluator.h`）

池成员健康探测。`MemberProbeTarget { tag, configtype, address, port, username(=p.security), password(=p.id), profile }`；`MemberHealth { tag, alive, tested, delayMs, lastError }`。

- **直接 cURL 探测**：SOCKS(4) / HTTP(10) 通过上游代理 URL 直连测量（`proxyUrlFor` → `probeSocksHttp`）。
- **非直接协议**：vmess/vless/trojan/ss/hysteria2/tuic/wireguard 需 xray 代答，经 `setProbePool()` 挂载的常驻 `ProxyProbePool` 路由；未挂载时返回 `tested=false`。
- 替换了旧的 xray ObservatoryService 路径（后者只报告 Start() 时已存在的 outbound，永远看不到运行时注入的成员，导致池成员冻结在注入态）。
- `stopFlag` 取消令牌：置位后提前终止循环返回部分结果（unprobed 目标 `tested=false`，`mergeHealth` 不改其状态），用于 `StandaloneProxyPool::stop()` 在单个探测间隔内解除 join()。

#### 2.6.4 ProxyProbePool（`include/ProxyProbePool.h`）

常驻 xray 探测实例池。`ProxyProbePool(xrayPath, workerCount, configDir)`，每个 worker 运行 xray（默认配置模板：socks-in mixed + api + direct/proxy outbounds + routing TCP->proxy），探测成员 = 经 gRPC 注入其 outbound，再经 worker 本地 socks 端口 cURL。

- **独立于** `XrayManager`（全局单例，批量测试共享）**和** `ProxyBatchTester`（生命周期/DB/verdict 语义不匹配池健康探测）。
- `start()`（幂等，至少一个 worker ready 返回 true）/ `stop()`（幂等）/ `probeMember(target, testUrl, connectTimeoutMs, totalTimeoutMs, out)`（无可用 worker 或未运行返回 false，调用方保持 tested=false）。`acquireWorker()` 标记空闲 worker 忙碌，`releaseWorker()` 释放。

#### 2.6.5 ProxyConnectivityVerifier（`include/ProxyConnectivityVerifier.h`）

本地启动代理是否真正把流量隧道到测试 URL（Spec R2，`docs/specs/2026-08-13-Spec-ProxyScoring-History-v1.0.md`）。

- `verify(socksPort, url, timeoutMs, maxAttempts=3)`：`fetchViaProxy` 最多 maxAttempts 次，任一次成功（非空响应体）返回 true。
- `verifyWithProbe(probe, maxAttempts=3, retryDelayMs=0)`：注入探测函数，失败后重试间隔 retryDelayMs，覆盖代理进程启动时间窗。
- `waitForPort(port, timeoutMs)`：轮询 SOCKS 端口直至 TCP 连接成功或超时（覆盖 CreateProcessA → 端口就绪的启动窗）。
- `waitForPort(port, timeoutMs, processHandle)`：崩溃感知变体，底层 xray/sing-box 进程已退出则提前返回（修复"启动闪崩却长时间等待"bug），避免整窗轮询永不打开的端口。

#### 2.6.6 ProxyScorer（`include/ProxyScorer.h`）

三因子评分引擎（纯函数，无 DB 副作用）。`ScoreResult { total_score[0,100], speed_score, stability_score, history_score, cold_start }`；`compute(item, speed_ms_min=0, speed_ms_max=5000)` / `computeBatch(items, ...)`。`speed_score` 基于延迟，`stability_score` 基于 consecutive_failures + 成功率，`history_score` 基于三因子历史健康度；`cold_start` 当 start_count==0。

---

## 3. task 调度子系统

### 3.1 TaskType（`include/task/TaskType.h`）

统一任务枚举：`NONE, UPDATE_SUBSCRIPTION, UPDATE_ALL, TEST_SUBSCRIPTION, TEST_SINGLE_PROXY, TEST_ALL, TEST_BATCH, TEST_ONLINE, FIND_FIRST, FIND_BEST, FIND_BY_INDEX, DEDUP, SYNC_DB, EXPORT_SHARE_LINKS, RESOLVE_REGION, RESOLVE_SINGLE_REGION, GENERATE_CONFIG, AUTO_TASK, PROBE_POOL_MEMBERS, PROBE_MEMBER`。

辅助类型：`TaskStatus`（PENDING/QUEUED/RUNNING/COMPLETED/FAILED/CANCELLED/REJECTED）、`TaskHandle`（taskId/type/status/priority/时间戳/进度/子任务统计）。

### 3.2 TaskQueue（`include/task/TaskQueue.h`, `src/task/TaskQueue.cpp`）

- **单 worker 线程** + **优先级队列**（max-heap by priority，同优先级 FIFO）。
- `submit(type, func, priority, callbacks)`：入队并返回 `TaskHandle`；`cancel(taskId)` / `cancelCurrent()`。
- `isRunning()` / `isBusy()`：`isBusy()` 仅在任务实际运行时为 true（修复了"worker 线程存活导致忙假阳性"问题）。
- `TaskContext`：携带 `cancelFlag` + `reportProgress`。
- `TaskCallbacks`：`onQueued / onProgress / onComplete / onRejected`（onQueued 用于"Task queued: <id>"提示）。
- `setProbeRunningQuery()`：注入探测运行态查询，供 MutexPolicy 判定。

### 3.3 MutexPolicy（`include/task/MutexPolicy.h`, `src/task/MutexPolicy.cpp`）

- `ConcurrencyDecision { ALLOW, REJECT, JOIN_PROBE_THEN_RUN, CANCEL_PROBE }`。
- `decide(TaskType a, TaskType b, bool probeRunning, bool userOpRunning)`：互斥矩阵裁决。
- `canRunConcurrently(a, b)`：判定两任务是否可并发。

**调度规则要点**：批量测试/更新/区域解析/AutoTask 在 worker 通道上互斥；共享池健康探测拥有独立线程池但经 MutexPolicy 与 DB/config 互斥。

---

## 4. service 层（`src/service/`）

| Service | 职责 |
| --- | --- |
| ProxyTestService | 单代理/全量/订阅测试编排（runSingle/runAll/runSubscription） |
| SubscriptionService | 订阅源管理 |
| AutoTaskService | 自动任务流水线 |
| ConfigService | 配置读写 |
| ProxyListService | 代理列表查询 |
| ShareLinkExportService | 分享链接导出 |
| DatabaseConnectionService | 数据库连接管理 |
| DatabaseMaintenanceService | 数据库维护 |

UI 层（AppController）通过 service 层委托业务逻辑，解耦 UI 与核心。

---

## 5. 数据层

- 数据库：SQLite（生产 `bin/worker/guindb.db`，测试 `test/guindb.db`）。
- 模型：`Profileitem`（基础配置项）、`Subitem`（订阅源节点）、`ProfileExItem`（携带测试状态/延迟/动态数据的扩展模型）。
- DAO：`DatabaseHelper` + `ProfileitemDAO` / `SubitemDAO` / `ProfileExItemDAO` / `FullConfigTemplateDAO`。
- 批量写库：`ProfileExItemDAO::updateTestResultBatch`（`INSERT OR REPLACE INTO ProfileExItem (indexid, delay, speed, sort, message, consecutive_failures, start_count, total_runtime_ms, crash_count)`，SUB_BATCH_SIZE=50）。
- 运行时历史表：`proxy_runtime_history`（进程收养/校验依据）。

---

## 6. 数据流

1. **订阅更新**：ProxyFinder → fetch → SubitemUpdaterV2 → normalize/dedup → DatabaseHelper → persist。
2. **配置生成**：ConfigGenerator → generate Xray JSON（outbound）。
3. **批量测试**：ProxyBatchTester → 共享 Xray 实例池 → ProxyTester（CURL+Xray）→ TestResultQueue → ProfileExItemDAO 批量写库 → ProxyRowsUpdatedEvent → UI 实时刷新。
4. **在线探测**：doTestOnlineProxies → curl 直连各独立代理进程监听 SOCKS 端口（不经过共享池）。
5. **高优先级插入（Scheme B）**：运行中 ProxyBatchTester → insertProxy() → insertedQueue_ → worker 优先取用 → 结果并入当前批次。
6. **任务调度**：UI 入口 → taskQueue_.submit(type, lambda, priority, callbacks) → 单 worker 顺序执行。

---

## 7. UI 层（wxWidgets）

- **MainFrame**（`src/ui/MainFrame.h/.cpp`）：主窗口布局、状态栏、工具栏。
- **AppController**（`src/ui/AppController.h/.cpp`）：控制器层，业务调度；持有 `taskQueue_`、`currentTaskType_`、`proxyBatchTester_`（原子指针，供高优先级插入）。
- **SubscriptionPanel / ProxyListPanel / LogPanel**：订阅源管理 / 代理列表 / 日志展示面板。
- **Events**（`src/ui/Events.h/.cpp`）：自定义事件系统（ProxyTestProgressEvent / ProxyRowsUpdatedEvent / StatusUpdateEvent / OnlineProbeFinishedEvent / StandaloneProxyEvent）。
- **StandaloneFloatingWidget**：悬浮窗（在线代理计数/状态徽标）。

---

## 8. 构建系统

- CMake + Ninja，C++17（**全栈禁止 `auto` 类型推导**）。
- 目标平台：Windows（MinGW/GCC）；默认终端 PowerShell。
- 依赖：Boost、SQLite、Curl、Threads、wxWidgets 3.2+（仅 GUI）、Xray-core。
- 调试配置：`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug`；并行 `cmake --build build --parallel 8`。

---

## 9. 测试

- 框架：Google Test（`tests/` 目录，测试数据集在 `test/`）。
- 核心：`tests/test_mutex_policy.cpp`（4/4）、`tests/test_task_queue.cpp`（2/2）、`tests/test_autotask.exe`、`tests/test_unified_monitor_rows.exe`、`tests/test_log_statistics_event.exe`。
- 集成：`ctest -V` 全量；`scripts/build-and-test.bat` UI 自动化验收（12 项，`UI_FLOATINGWIDGET` 已知偶发）。
- 基线已知失败（非本工作引入）：`XrayManagerStartupTest`、`RegionBatchResolverTest`（缺失源链接）、`NetworkMonitorTest`（无网络）。

---

## 10. 运行入口

- **GUI**（默认）：`.\build\validproxy.exe`（`main_gui.cpp`，无参或 `-ui`/`--ui`）。
- **CLI**：`.\build\validproxy-cli.exe`（`main_cli.cpp`）。

### CLI 命令速查

| 命令 | 作用 |
| --- | --- |
| （无参） | 默认批量测试（对数据库中全部代理执行连通性测试） |
| `-c/--config <path>` | 指定配置文件（默认 `bin/config.json`） |
| `-show-sub` | 显示所有订阅源 |
| `-G/-generator <id>` | 按 indexId 导出标准 outbound Xray JSON |
| `-F/-find-proxy` | 快速查找第一个可用代理 |
| `-FMIN/-findminproxy` | 查找当前延迟最小的优质代理 |
| `-U/-update <id>` | 强制更新指定订阅源 |
| `-UA/-update-all` | 批量更新所有启用订阅源 |
| `-T/-test-sub <id>` | 对指定订阅源内代理进行连通性测试 |
| `-TU/-tourl` | 导出所有有效代理为分享链接 |
| `-D/-dedup` | 去重算法 |
| `-S/-sync [src[:dst]]` | 数据库同步 |
| `-IS/-import-sub-config <f>` | 批量导入订阅节点 |
| `-h/--help` | 帮助 |

---

## 11. 关键决策记录（ADR）

1. **Scheme B 取代 Scheme A**：Xray 实例池是基础设施而非任务；逐代理子任务（Scheme A）会把优先级退化为手工排队；改为"运行中池内高优先级插入"（`insertProxy()` + `insertedQueue_`）。见 `docs/specs/2026-10-27-Spec-BatchTaskQueue-Optimization-v1.0.md` §7。
2. **单 worker 非抢占优先级队列**：优先级只影响排队顺序，不中断运行中任务；批量任务与单代理测试在 worker 通道互斥（MutexPolicy）。
3. **实时结果刷新**：`RowsUpdatedCallback` → `ProxyRowsUpdatedEvent` → `ProxyListPanel::onProxyRowsUpdated` → `refreshResultsFor(ids)`，批量测试逐批（50 条）刷新而非仅在完成时。
