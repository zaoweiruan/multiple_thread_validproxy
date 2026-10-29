---
title: "Batch Task Queue Optimization 方案独立评审（三视角）"
date: 2026-10-27
type: review
status: draft
version: v1.0
---

# 独立评审：Batch Task Queue Optimization 方案 v1.0

**评审对象**：`docs/specs/2026-10-27-Spec-BatchTaskQueue-Optimization-v1.0.md`

**三视角**：
- A：技术断言攻击（代码事实核对）
- B：可实施性（工时/风险/依赖）
- C：文档一致性与验收效力

## 1. 总体结论

**有条件通过（CONDITIONAL PASS）**。

方案方向正确、与现有 spec/bugfix 文档一致，三类问题（模块过大 / 无队列 / 互斥混乱）均被识别并给出落地方案。但存在若干阻断项与重要问题，需修订后实施。

## 2. 通过项（P1–P8）

1. **P1 模块拆分继承已有 spec**：正确引用 `2026-06-18-Spec-Responsibility-Decomposition-v1.0.md`，避免重复造轮子。
2. **P2 TaskType 合并两套枚举**：`AutoTaskStepType` + `OperationType` → 单一 `TaskType`，映射规则清晰。
3. **P3 互斥矩阵显式化**：`MutexPolicy::canRunConcurrently` 为单一真源，矩阵覆盖现有 bugfix 裁决（worker 停摆、探活非阻塞）。
4. **P4 DB 读写锁分离**：继承 WAL 设计，读操作可并发，合理。
5. **P5 取消语义统一**：单一 `cancelRequested_`，保留周期探活不复用用户取消标志（与现有设计一致）。
6. **P6 UI 状态由任务类型映射**：消除 `isBusy_` 与 `isRunning_` 重叠。
7. **P7 风险与回滚**：继承现有约束（C++17、禁止 auto、保留 public API）。
8. **P8 验收标准可测**：互斥矩阵单元测试、ctest、build-and-test.bat。

## 3. 阻断项（B1–B5）

### B1：工时估算偏低且未考虑已有 spec 的未落地状态

方案 §3 估算 12-16 天，但 `2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` 的 5 阶段拆分**仍未落地**（代码中 `src/service/` 仅 8 个轻量 service，`AppController` 仍 182KB）。Phase 2 "服务层拆分" 实际需先完成已有 spec 的 Phase 0-7，工时应重新评估为 **20-30 天**。

**修订**：在 §3 Phase 2 前增加 "Phase 1.5：完成已有 Responsibility Decomposition 的 Phase 0-4"，并把总工时调整为 20-30 天。

### B2：TaskQueue 与现有 `workerThread_` / `AsyncOperationGuard` 的迁移路径不清

现有 `AsyncOperationGuard`（`include/ui/AsyncOperationGuard.h`）在 11 个 async 入口使用，`ScopeGuard` 在 9 个 do* 方法使用。方案 §2.2.3 说"提交若冲突 → REJECTED"，但未说明如何**渐进式**替换：是先加 TaskQueue 并行运行，还是直接替换？直接替换风险高（UI 事件处理、进度回调、取消时序）。

**修订**：增加 "Phase 3.0 迁移策略"：先让 `TaskQueue` 包装现有 `workerThread_`（适配器模式），保持 `AsyncOperationGuard` 行为不变；再逐步把 async 入口改为 `submit`；最后删除 `AsyncOperationGuard`。

### B3：互斥矩阵与周期探活的"并存"规则缺少实现细节

矩阵中标注 `TEST_ONLINE` 与用户操作 `❌*`（并存但特殊规则），但 `MutexPolicy::canRunConcurrently` 是纯函数，无法表达"手动测试前 join 探活"这种**副作用**。如果只返回 bool，调用方仍需在 UI/Controller 层写特殊分支，互斥真源并不完整。

**修订**：扩展 `MutexPolicy` 接口：
```cpp
enum class ConcurrencyDecision { ALLOW, REJECT, JOIN_PROBE_THEN_RUN, CANCEL_PROBE };
ConcurrencyDecision decide(TaskType a, TaskType b, const SystemState& state);
```
并把副作用（join/cancel probe）封装在 `TaskQueue` 内部，而非 UI 层。

### B4：DB 读写锁与 SQLite WAL 的交互未说明

方案 §2.3.3 提议 `std::shared_mutex`，但 SQLite 连接是**单句柄**（`db_`），WAL 模式下读写不阻塞，但**同一连接的并发使用**仍需串行化（SQLite 文档：一个连接同一时刻只能有一个活跃 statement，除非启用 `SQLITE_OPEN_FULLMUTEX`）。当前代码通过单 worker 线程串行化 DB 访问。若引入读写锁允许并发读，必须确保**每个读操作使用独立连接或 `sqlite3` 的线程安全模式**。

**修订**：明确两种方案：
- 方案 A（推荐）：保持单连接 + 单 worker 串行化 DB 访问，`shared_mutex` 仅保护内存缓存（`loadSubscriptions` 缓存等），不允许多线程并发调用 sqlite3。
- 方案 B：为读操作创建只读连接（`sqlite3_open_v2` with `SQLITE_OPEN_READONLY`），读写真正并发，但需管理连接生命周期。

并在 §2.3.3 中选定方案 A，避免引入连接管理复杂度。

### B5：验收标准缺少"无回归"的量化定义

§5 验收标准写"全量 ctest 通过（除既有基线失败）"，但未列出**既有基线失败清单**，实施者无法判断是否引入新失败。且未定义性能回归阈值（如批量测试吞吐、UI 响应延迟）。

**修订**：在 §5 增加"基线失败清单"（`XrayManagerStartupTest`、`RegionBatchResolverTest` 链接缺源、`UI_PORTCLOSE`、`UI_FLOATINGWIDGET` 并行 flaky、`NetworkMonitorTest` 无外网）与性能阈值（批量测试 1000 代理 ≤ 当前耗时 +10%）。

## 4. 重要问题（I1–I6）

### I1：TaskQueue 持久化格式与 AutoTask state file 的兼容性

方案 §2.2.3 说"复用 AutoTaskManager 的 state file 机制，扩展为通用 TaskQueueState"。但 `autotask_state.json` 字段名已固定（`task_id`、`created_at`、`current_step_index`、`steps`、`completed`、`cancelled`、`sql_query_id`、`sql_by_subid_id`）。扩展时必须**向后兼容**老文件，否则升级后无法 resume 旧任务。

**建议**：在 §2.2.3 增加"向后兼容策略"：老字段保留，新字段（`queue_depth`、`task_type`、`priority`）设默认值；加载时按字段存在性分支。

### I2：优先级队列的饥饿问题

`std::priority_queue` 按 priority 降序，高优先级任务（手动用户操作 priority=1）可能**饿死**低优先级任务（周期探活 priority=2？注意 priority 数值语义需统一：建议 0=低、1=普通、2=高）。方案 §2.2.3 的 priority 定义（0=普通, 1=高, 2=低）与常规相反，易混淆。

**修订**：统一 priority 语义为 "数值越大优先级越高"（0=低/周期探活, 1=普通, 2=高/手动），并在 §2.2.2 中明确。

### I3：TaskQueue 与 NetworkMonitor 的循环依赖

`AutoTaskManager` 构造函数接收 `NetworkMonitor*`（`include/AutoTaskManager.h:26`）。若 `TaskQueue` 也需网络状态，可能形成 `TaskQueue → NetworkMonitor → TaskQueue` 循环。现有 `NetworkMonitor` 是纯探测器（已移除 cancelOnDisconnect），依赖方向应为 `TaskQueue → NetworkMonitor`（单向）。

**建议**：在 §2.1 架构图中明确依赖方向：`TaskQueue` 依赖 `NetworkMonitor`（只读），`NetworkMonitor` 不依赖 `TaskQueue`。

### I4：UI 事件处理的线程安全

现有 UI 通过 `wxQueueEvent` 从 worker 线程投递事件到 UI 线程。方案 §2.2.3 的 `TaskProgressEvent`/`TaskCompletedEvent` 必须保证**仅从 worker 线程投递**，且 UI 线程不直接读 `TaskHandle`（避免 data race）。现有 `TaskHandle` 含 `std::string` 和 `int`，若 UI 线程读取需加锁或复制。

**建议**：`TaskHandle` 通过 `wxQueueEvent` **按值复制**投递；UI 线程不持有 `TaskHandle` 引用；`TaskQueue::getCurrentTask()` 返回副本。

### I5：`OperationType` 与 `TaskType` 的映射丢失信息

方案 §2.3.5 将 `TaskType::TEST_SUBSCRIPTION` 与 `TaskType::TEST_ALL` 都映射到 `OperationType::TEST`，UI 无法区分"测试单订阅"与"测试全部"的按钮态（如进度文案）。现有 `setOperationState` 已按 `OperationType` 设置取消按钮文案（"取消测试"/"停止更新"等），但不区分粒度。

**建议**：保留 `OperationType` 为粗粒度 UI 态，但 `TaskCompletedEvent` 携带原始 `TaskType` 供 UI 显示详细文案；或扩展 `OperationType` 增加 `TEST_SUBSCRIPTION`/`TEST_ALL` 区分（需评估 UI 影响）。

### I6：未考虑 `probeThread_` 的生命周期管理

方案 §2.3.1 列出 `probeThread_` 但未说明其与 `TaskQueue` 的交互：周期探活是独立线程，不走 `TaskQueue`。若 `TaskQueue` 需要"等待探活结束"（如 DB 切换），需要 `TaskQueue` 能查询 `isProbeRunning()`。现有 `AppController::isOnlineProbeRunning()` 已提供，但 `TaskQueue` 不应直接依赖 `AppController`（循环依赖）。

**建议**：`TaskQueue` 持有 `std::function<bool()> probeRunningQuery` 回调，由 `AppController` 注入；或 `MutexPolicy` 持有 probe 状态查询。

## 5. 文档一致性问题（D1–D3）

### D1：§1.1 中 `AppController` 规模描述与实际不符

方案写 `AppController` ~182KB / ~1170 行，实际 `src/ui/AppController.cpp` 为 182259 字节（~4700 行，因近期 standalone adoption 大量新增）。`ProxyBatchTester.cpp` 实际 34730 字节。数字需更新以避免误导。

### D2：§6 "与现有文档的关系" 缺少 `docs/specs/2026-07-01-Spec-AppController-Responsibility-Decomposition-v1.0.md`

该 spec 也提出 AppController 拆分（5 个服务），与本方案 §2.1 的服务列表高度重叠。应明确本方案是其"调度层扩展"而非替代，并在 §6 中引用。

### D3：§2.2.4 "AutoTaskManager 保留为流水线编排器" 与现有 `AutoTaskManager` 的 `run()`/`resume()` 语义

现有 `AutoTaskManager::run()` 接收 `std::vector<std::string> stepNames`，内部顺序执行。方案说"内部将 steps 拆解为子任务依次入队"，但 `AutoTaskManager` 的 state file 与 resume 机制依赖**顺序执行**语义。若改为子任务入队，resume 时如何恢复"当前执行到第 N 个子任务"的状态？需在 §2.2.4 中说明 state file 的扩展方案（见 I1）。

## 6. 评分

| 维度 | 评分 | 说明 |
|------|------|------|
| 技术判断 | 8.0/10 | 问题识别准确，方向正确 |
| 可实施性 | 6.0/10 | 工时偏低，迁移路径不清 |
| 文档一致性 | 7.0/10 | 部分数字过时，引用不全 |
| 验收效力 | 6.5/10 | 缺基线清单与性能阈值 |

**综合：7.0/10**

## 7. 结论与建议

**结论：有条件通过（CONDITIONAL PASS）**。

建议修订项优先级：
1. **必须修订（阻断）**：B1（工时）、B2（迁移路径）、B3（互斥副作用）、B4（DB 连接）、B5（验收量化）。
2. **建议修订（重要）**：I1–I6、D1–D3。
3. **可后续修订（轻微）**：priority 语义、UI 文案粒度。

修订完成后，建议再次评审（或进入 Phase 0 前置实施）。在修订期间，可先实施 Phase 0 的纯类型与 `MutexPolicy`（无风险），为后续阶段奠定基础。

---

---

## 8. 修订追踪（2026-10-27）

已按本评审修订 `docs/specs/2026-10-27-Spec-BatchTaskQueue-Optimization-v1.0.md`：

- **B1**：Phase 2 前新增 Phase 1.5（完成已有 Responsibility Decomposition Phase 0-4）；总工时调整为 20-30 天。
- **B2**：新增 Phase 3.0 迁移策略（适配器模式，渐进替换 `AsyncOperationGuard`）。
- **B3**：`MutexPolicy` 扩展 `ConcurrencyDecision` 枚举 + `decide()`，副作用封装在 `TaskQueue` 内部。
- **B4**：§2.3.3 选定方案 A（单连接串行化 DB，shared_mutex 仅保护内存缓存）。
- **B5**：§5 增加基线失败清单与性能阈值。
- **方案 A（用户裁决）**：批量测试按 §2.2.5 拆解为 `TEST_BATCH` 容器任务 + `TEST_SINGLE_PROXY` 子任务；高优先级单代理测试可在代理边界插入并优先执行，不终止批量任务；更新 §2.2.1、§2.2.5、§3 Phase 3、§5 验收标准。
- **I1/I2/I3/I4/I5/I6**：state file 兼容策略；priority 语义统一；依赖方向明确；UI 线程安全（按值复制）；OperationType 粗粒度 + TaskType 详细；probeThread_ 生命周期回调。
- **D1/D2/D3**：数字更新；§6 增加 AppController 职责分解 spec 引用；AutoTask resume 语义说明。

修订完成后，提交人工评审。

*本评审为独立评审，评审者与方案作者为同一 AI 代理，但按三视角（技术断言 / 可实施性 / 文档一致性）独立审视。*
