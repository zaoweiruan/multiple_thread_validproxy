# Review — 统一代理测试反馈与队列化方案 (ProxyTestUnified)

- **日期**: 2026-10-29
- **被评审文档**: docs/specs/2026-10-29-Spec-ProxyTestUnified-v1.0.md
- **评审类型**: 独立评审（架构/实现/风险/测试）
- **评审结论**: **CONDITIONAL PASS**（3 blockers, 5 important, 2 consistency）

## 1. 评审范围

- 4 阶段方案（A 存活队列检查 / B 日志统一 / C 完成汇总 / D 队列汇总信息）
- 与现有 Scheme B（池内插入）的关系
- 与现有 TaskQueue/MutexPolicy 的交互
- 验收标准与风险

## 2. Blockers（必须修订）

### B1. Phase A "创建 count=N 池"与现有 Scheme B 不兼容

Spec §3.1 Phase A 提出"无存活池 → 创建 count=N 的池（而非 N 个 count=1 池）"。但现有 `ProxyBatchTester` 的 `proxies_`/`proxiesQueue_` 是构造时由 `run()`/`runWithSubId()`/`runWithIndexId()` 填充的，**构造函数不接受代理列表**。多选场景下 `onTestProxy` 直接调 `controller_->testSingleProxyAsync(indexId, this)`，绕过 `ProxyBatchTester` 构造。要创建 count=N 池，需要：
- 新增 `ProxyBatchTester::loadProxies(std::vector<Profileitem>)` 或构造函数接受代理列表；
- `AppController::testSelectedProxiesAsync(std::vector<std::string> indexIds)` 新入口，统一创建池；
- `onTestProxy` 改为收集选中 indexId 列表后调用新入口（而非逐个 testSingleProxyAsync）。

**建议**：在 spec §3.1 明确新增 `testSelectedProxiesAsync` 入口与 `loadProxies` 接口，避免隐含假设。

### B2. Phase A "检查 TaskQueue 是否已有排队的 TEST_SINGLE_PROXY 任务"需要 TaskQueue 扩展

Spec 提到"先检查 taskQueue_ 中是否已有排队的 TEST_SINGLE_PROXY / TEST_ALL 任务"。但 `TaskQueue`（include/task/TaskQueue.h）当前**无** `getPendingTasksByType(TaskType)` 或 `peekFront()` 接口，仅 `queueDepth()`/`currentTask()`/`history()`。需要新增：
- `std::vector<TaskHandle> TaskQueue::pendingTasks() const` 或 `bool hasPending(TaskType) const`。
- 或在 AppController 维护 `std::atomic<int> pendingSingleProxyCount_`。

**建议**：在 spec §3.1 补充 TaskQueue 接口扩展需求，否则无法实现"追加到 pending 任务"。

### B3. Phase C "程序关闭时打印汇总"与 XrayManager::release() 时序

Spec §3.3 提到"程序关闭：main_gui.cpp cleanup 路径打印 `[Summary] ... (exit: shutdown)`（若批次运行中）"。但 `main_gui.cpp:166` 调用 `XrayManager::release()` 时，`ProxyBatchTester` 可能已被析构（单例/局部变量），其 `running_`/`totalProxies_` 等状态已不可用。需要：
- 在 `AppController` 析构或 `XrayManager::release()` 前，先查询当前 running pool 的汇总状态；
- 或在 `ProxyBatchTester` 析构时打印汇总（exit reason=shutdown）。

**建议**：spec §3.3 明确汇总打印位置（ProxyBatchTester 析构时，exit reason 由析构上下文推断），避免在 main_gui.cpp 直接访问已析构对象。

## 3. Important（强烈建议修订）

### I1. Phase B 日志格式与现有 Logger 级别体系对齐

Spec 提出 `[Testing]`/`[Result]`/`[Summary]` 前缀，但现有 Logger 使用 `[INFO]`/`[REPORT]`/`[WARN]`/`[ERR]` 级别前缀（Logger.h）。建议格式为 `[INFO] [Testing] i/total: ...` 或 `[REPORT] [Summary] ...`，保留级别语义，便于按级别过滤。

### I2. Phase A 需考虑 `keepXrayAlive_` 与池复用的交互

现有 `keepXrayAlive_` 机制（single-proxy 测试保持实例存活）与 Phase A 的"创建 count=N 池"可能冲突：若单代理测试保持实例存活，后续多选应复用而非新建。Spec 需明确：多选池创建前先 `XrayManager::start()` 的 reuse 路径（:118-134）已处理复用，但 `keepXrayAlive_` 的实例（port 10000）可能被 batch 池的 index 0 复用——需确认 port 对齐。

### I3. Phase C 强制终止路径的 exit reason 需考虑 detach 的 worker

有界 teardown（30s detach）后，`printSummary()` 打印的 Total/Success/Failed 可能不完整（detach 的 worker 未更新计数）。Spec §3.3 应注明：detach 的 worker 计为 "remaining"（未完成），summary 反映实际处理数。

### I4. Phase D `QueueStats` 与 `TaskQueue` 现有接口

`TaskQueue` 已有 `queueDepth()`/`currentTask()`/`history()`。`QueueStats` 可复用 `history()`（TaskHandle 含 status/progress）。建议不新增接口，改为在 AppController 定时读取 `taskQueue_.history(n)` 聚合。

### I5. 验收标准 #1 "只出现 1 次 XrayManager::start"在多选+批量混合场景下不严格

若多选（count=N 池）与已有 batch 池（count=4）并发，XrayManager::start() 会被调用两次（不同 count/ports）。验收标准应限定"单次多选操作只创建 1 个池"。

## 4. Consistency（文档一致性）

### D1. §2 任务类型表与现有 MutexPolicy 矩阵一致

✅ 三类分类（worker/isExport/probe）与 src/task/MutexPolicy.cpp 一致。

### D2. §5 验收标准与 §3.1 Phase A 不完全对应

验收 #1 提"只出现 1 次 XrayManager::start: count=69"，但 Phase A §3.1 提"创建 count=N 的池"——N 应明确为"选中代理数"。建议统一为"count=选中代理数（上限 xray_workers）"。

## 5. 评审结论

**CONDITIONAL PASS**：Phase A（B1/B2）和 Phase C（B3）为 blocker，需修订后实施。Phase B/D 为 enhancement，可后续迭代。建议：Phase A 修订 spec 后优先实施（消除 xray 重启），Phase B/C/D 可逐步推进。

## 6. 修订追踪

- [x] B1: 明确 `testSelectedProxiesAsync` + `loadProxies` 接口 — **已修订**（spec §3.1.1/§3.1.2）
- [x] B2: 补充 TaskQueue 接口扩展（pendingTasks/hasPending）— **已修订**（spec §3.1.4）
- [x] B3: 明确汇总打印位置（ProxyBatchTester 析构时，exit reason 由上下文推断）— **已修订**（spec §3.3）
- [ ] I1: 日志格式与 Logger 级别对齐
- [ ] I2: keepXrayAlive_ 与池复用交互说明
- [ ] I3: detach worker 计入 remaining
- [ ] I4: QueueStats 复用 history()
- [ ] I5: 验收标准限定单次操作
- [ ] D2: N 明确为选中代理数（上限 xray_workers）