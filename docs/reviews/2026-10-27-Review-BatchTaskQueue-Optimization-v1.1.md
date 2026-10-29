---
title: "Batch Task Queue Optimization 方案 v1.0 独立评审（修订后，三视角）"
date: 2026-10-27
type: review
status: draft
version: v1.1
---

# 独立评审：Batch Task Queue Optimization 方案 v1.0（修订后）

**评审对象**：`docs/specs/2026-10-27-Spec-BatchTaskQueue-Optimization-v1.0.md`（含 B1–B5/I1–I6/D1–D3 修订与方案 A）

**三视角**：A 技术断言攻击 / B 可实施性 / C 文档一致性与验收效力

## 1. 总体结论

**有条件通过（CONDITIONAL PASS）**。

修订后的方案在任务调度、互斥矩阵、DB 方案、迁移策略等方面明显改进；方案 A（细粒度子任务 + 优先级队列）解决了“高优先级插入会终止批量任务”的核心矛盾。但细粒度方案引入了新的工程复杂度（子任务聚合、UI 事件风暴、大规模入队开销），需在实施前明确设计细节。

## 2. 通过项（P1–P10）

1. **P1 方案 A 解决抢占终止问题**：批量测试拆解为容器任务 + 子任务，高优先级任务在代理边界插入，不终止批量任务。
2. **P2 互斥矩阵 + ConcurrencyDecision**：副作用（join/cancel probe）封装在 `TaskQueue`，UI 层不感知。
3. **P3 DB 方案 A 选定**：单连接串行化，`shared_mutex` 仅保护内存缓存，避免 SQLite 并发 statement 风险。
4. **P4 迁移策略（适配器模式）**：渐进替换 `AsyncOperationGuard`，降低 UI 事件/取消时序回归风险。
5. **P5 优先级语义统一**：数值越大优先级越高，防混淆。
6. **P6 UI 线程安全**：`TaskHandle` 按值复制投递，避免 data race。
7. **P7 验收量化**：基线失败清单 + 性能阈值，可判定回归。
8. **P8 依赖方向明确**：`TaskQueue → NetworkMonitor` 单向。
9. **P9 状态文件兼容**：老字段保留，新增字段默认值，支持 resume。
10. **P10 与现有文档关系清晰**：继承而非替代。

## 3. 阻断项（B1–B3）

### B1：容器任务与子任务的聚合/取消语义未定义

§2.2.5 说容器任务“聚合结果并更新汇总/状态栏”，但未说明：
- 容器任务如何追踪子任务完成（计数/事件）；
- 子任务失败时容器是否继续、是否重试；
- 取消容器任务时，已入队但未执行的子任务如何取消（`TaskQueue::cancel(taskId)` 是否支持按 `parentTaskId` 批量取消）。

**修订**：在 §2.2.5 增加“容器聚合与取消”小节，定义 `TaskHandle` 新增字段 `parentTaskId`、`childTotal`、`childCompleted`、`childFailed`；`TaskQueue` 提供 `cancelChildren(parentTaskId)`。

### B2：大规模入队开销与 UI 事件风暴未评估

批量测试 50k+ 代理时，为每个代理入队一个子任务（`TEST_SINGLE_PROXY`）会产生 50k 次 `submit`/`TaskCompletedEvent`，可能造成：
- 队列内存与调度开销；
- UI 线程事件风暴（每代理一次 `wxQueueEvent`）。

**修订**：在 §2.2.5 增加“批量子任务粒度”策略：默认每 N 个代理一个子任务（如 N=10/50），或提供 `batchSize` 配置；UI 进度事件按时间/数量节流（如每 100ms 或每 50 个代理一次）。

### B3：`TEST_ALL` 与 `TEST_BATCH` 语义重叠

§2.2.1 同时定义 `TEST_ALL`（“全部代理测试（容器任务，拆解为 TEST_SINGLE_PROXY 子任务）”）与 `TEST_BATCH`（“批量测试容器任务（生成列表 + 聚合结果）”），职责不清，映射规则未说明二者区别。

**修订**：明确 `TEST_ALL` = “测试全部代理”（用户入口），`TEST_BATCH` = “批量测试容器”（内部编排）；或合并为一个类型，避免双轨。

## 4. 重要问题（I1–I6）

### I1：`TaskHandle` 缺少子任务追踪字段

容器聚合需要 `parentTaskId`、`childTotal`、`childCompleted`、`childFailed`；当前 §2.2.2 的 `TaskHandle` 无这些字段。

**建议**：在 §2.2.2 补充。

### I2：AutoTask 步骤依赖与优先级插入的冲突

§2.2.4 说 AutoTask 改为提交子任务序列；但若高优先级任务插队，AutoTask 的步骤顺序可能被破坏（例如 AutoTask 的 TEST_ALL 步骤被手动高优先级测试打断后，AutoTask 的后续步骤仍按原顺序执行，但时间线被插入）。

**建议**：在 §2.2.4 明确：AutoTask 子任务链使用“有序依赖”（`dependsOn` 字段或顺序队列），高优先级任务可插队但不破坏 AutoTask 的步骤顺序；AutoTask 容器任务状态仍按步骤顺序推进。

### I3：UI 进度事件节流

§2.2.3/§2.3.5 未定义进度事件频率；细粒度子任务下可能产生大量事件。

**建议**：在 §2.2.3 增加“事件节流”：进度事件按时间（如 100ms）或数量（如 50 个子任务）合并发送。

### I4：取消语义的“安全点”定义

§2.3.4 只说取消标志；细粒度子任务下，取消容器任务时正在执行的子任务何时停止？需要定义“子任务边界”为取消检查点。

**建议**：在 §2.3.4 增加：取消检查在每个子任务开始前执行；正在执行的子任务运行至完成或其内部取消点。

### I5： aging 防饥饿的具体算法未定义

§2.2.5 提到 aging，但未定义阈值与提升幅度。

**建议**：在 §2.2.5 增加：等待超过 T 秒的普通子任务 priority +1（上限 2），或使用公平队列（FIFO within same priority）。

### I6：工时与 Phase 1.5 依赖风险

Phase 1.5 依赖 `2026-06-18-Spec-Responsibility-Decomposition-v1.0.md` 的 5 阶段拆分，该 spec **仍未落地**（`src/service/` 仅 8 个轻量 service）。若该 spec 继续推迟，Phase 1.5 可能阻塞整个计划。

**建议**：在 §3 Phase 1.5 增加“若 Phase 0-4 未完成，则 TaskQueue 先以适配器模式接入现有 AppController（不等服务层拆分），服务层拆分作为后续迭代”。

## 5. 文档一致性问题（D1–D2）

### D1：§编号顺序错误

§2.2.5（批量测试细粒度）出现在 §2.2.4（AutoTaskManager 关系）之前，编号顺序错乱（2.2.5 → 2.2.4）。

**修订**：重新编号为 §2.2.4（细粒度子任务）与 §2.2.5（AutoTaskManager 关系），或调整章节顺序。

### D2：§6 仍引用 v1.0 评审，未更新为 v1.1

文档末尾仍指向 `docs/reviews/2026-10-27-Review-BatchTaskQueue-Optimization-v1.0.md`；修订后应指向 v1.1 评审（本文件）。

## 6. 评分

| 维度 | 评分 | 说明 |
|------|------|------|
| 技术判断 | 8.5/10 | 方案 A 解决核心矛盾 |
| 可实施性 | 6.5/10 | 细粒度开销与聚合语义待明确 |
| 文档一致性 | 7.0/10 | 编号顺序、引用待修正 |
| 验收效力 | 7.0/10 | 细粒度验收标准待补充 |

**综合：7.3/10**

## 7. 结论与建议

**结论：有条件通过（CONDITIONAL PASS）。**

建议修订项优先级：
1. **必须修订（阻断）**：B1（容器聚合/取消语义）、B2（批量子任务粒度与事件节流）、B3（TEST_ALL vs TEST_BATCH 语义）。
2. **建议修订（重要）**：I1–I6、D1–D2。
3. **可后续修订（轻微）**：aging 算法细节、UI 文案粒度。

修订完成后，建议再次评审或进入 Phase 0 实施。

---

---

## 8. 修订追踪（2026-10-27）

已按本评审修订 `docs/specs/2026-10-27-Spec-BatchTaskQueue-Optimization-v1.0.md`：

- **B1**：§2.2.4 增加“容器聚合与取消”小节（`parentTaskId`/`childTotal`/`childCompleted`/`childFailed` + `cancelChildren`）。
- **B2**：§2.2.4 增加“批量子任务粒度与事件节流”（`batchSize` 默认 10 + 事件节流）。
- **B3**：§2.2.4 增加“类型语义”（`TEST_ALL` 用户入口 → `TEST_BATCH` 容器）。
- **I1**：§2.2.2 `TaskHandle` 新增子任务追踪字段。
- **I2**：§2.2.5 增加 AutoTask 有序依赖说明。
- **I3**：§2.2.3 增加事件节流。
- **I4**：§2.3.4 增加取消检查点定义。
- **I5**：§2.2.4 增加 aging 算法细节。
- **I6**：§3 Phase 1.5 增加回退策略。
- **D1**：§编号修正（2.2.4/2.2.5 顺序）。
- **D2**：文末评审引用更新为 v1.1。

*本评审为独立评审，评审者与方案作者为同一 AI 代理，但按三视角（技术断言 / 可实施性 / 文档一致性）独立审视。*
