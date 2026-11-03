# Review — 任务与队列监控面板 + 日志多标签页 (TaskQueueMonitorAndLogTabs)

- **日期**: 2026-10-30
- **被评审文档**: docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.0.md
- **评审类型**: 独立评审（架构 / 实现 / 风险 / 测试）
- **评审结论**: **CONDITIONAL PASS**（4 blockers, 6 important, 3 consistency）

## 1. 评审范围

- 三大功能：任务/待执行列表 + 手工终止；批量测试双队列剩余 + 百分比；日志区标签页。
- 后端接口扩展：`taskTypeToString`、`TaskQueue::snapshot()`、`ProxyBatchTester::getQueueStats()`、`AppController` 薄包装。
- UI：`wxNotebook` 底部容器 + `TaskMonitorPanel` + `BatchQueuePanel`。
- 与现有 Scheme B / TaskQueue / MutexPolicy / ProxyTestUnified 的兼容性。

## 2. Blockers（必须修订）

### B1. `TaskQueue::snapshot()` 无法直接遍历 `std::priority_queue`

Spec §3.2 与 §6 提到"snapshot 时将 priority_queue 拷贝到 vector 并 sort"。但 `std::priority_queue` 仅暴露 `top()/pop()/push()`，底层容器 `c` 是 `protected` 成员；外部代码无法直接遍历。实现选项：

- **选项 A**：在 `TaskQueue` 内部实现 `snapshot()`（成员函数可访问 `c`），按 `EntryCompare` 语义排序后转 `TaskHandle`。可行，但需 friend 或成员实现。
- **选项 B**：维护一个并行的 `std::vector<TaskHandle> pendingHandles_` 镜像，每次 submit/cancel 同步更新。增加一致性维护成本。

**建议**：Spec §3.2 明确采用选项 A（成员函数内部访问 `queue_.c`），并注明 `Entry` → `TaskHandle` 转换只拷贝 `handle` 字段；排序复用 `EntryCompare`。避免在头文件暴露底层容器。

### B2. `proxyBatchTester_` 生命周期与 UI 读取的竞态

Spec §3.4 与 §6 承认"tester 可能在调用期间析构"。但 `proxyBatchTester_` 是 `std::atomic<ProxyBatchTester*>`，析构时若 UI 线程正调用 `getQueueStats()`（读取 `proxiesQueue_` 等成员），而析构线程已 `delete` 对象 → **use-after-free**。现有代码中 `proxyBatchTester_` 的读取多在 worker/UI 线程且 tester 生命周期由 `doTestAllProxies` 的局部 `unique_ptr` 管理；UI 轮询引入新的并发读取路径。

**建议**：Spec 必须明确 tester 生命周期策略，二选一：
- **(a) shared_ptr**：`proxyBatchTester_` 改为 `std::atomic<std::shared_ptr<ProxyBatchTester>>`（或 `std::shared_ptr` + mutex），UI `lock()` 后读；析构自动延迟。代价：破坏现有原子指针模式，需改动 `doTestAllProxies`/`insertProxy` 路径。
- **(b) 读锁 + 析构前置 null**：析构前 `proxyBatchTester_.store(nullptr)`，UI 在读取前 `load()`；若 null 返回空 stats；但 load 与 delete 之间仍有 TOCTOU。需在析构时先 store nullptr 再 `join` worker / destroy tester，且 UI 读取时对 tester 指针做 "try-lock"（无锁方案不安全）。

**推荐 (a)**，或至少在 spec 中承认 (b) 的 TOCTOU 并限定刷新频率 + 在析构前 store nullptr + 500ms 轮询下概率极低但非零。评审要求 spec 明确选择并给出线程安全论证。

### B3. `TaskMonitorPanel`/`BatchQueuePanel` 作为 `wxNotebook` page 的 parent 与生命周期

Spec §3.5 代码片段 `new LogPanel(bottomNotebook)`、`new TaskMonitorPanel(bottomNotebook, controller_)`。但 `LogPanel` 构造签名是 `explicit LogPanel(wxWindow* parent)`，OK。`TaskMonitorPanel`/`BatchQueuePanel` 需定义构造函数。此外 `bottomNotebook` 是局部变量（`wxNotebook* bottomNotebook = new wxNotebook(...)`），未存为成员——但 `MainFrame` 需在析构/调整时访问？Spec §3.5 同时声明 `bottomNotebook_{nullptr}` 成员。**不一致**：代码片段用局部变量，成员声明为成员。需统一：存为成员 `bottomNotebook_`，所有 AddPage 使用成员。

### B4. 手工终止"排队任务"的语义与回调

Spec §3.6 与 §5 验收 #3 提到"排队任务可通过'取消选中'移除（cancel）"。`TaskQueue::cancel(taskId)` 的行为需确认：是否从 `queue_` 移除、是否触发 `onRejected` 回调、是否更新 `history_`。Spec 未描述取消排队任务后的 UI 反馈（状态栏/消息）。且 `TaskQueue::cancel` 的签名接受 `taskId` 字符串，但 UI 列表中需保留 taskId；OK。

**建议**：Spec §3.6 补充：取消排队任务 → `taskQueue_.cancel(id)` → 触发 `onRejected` 回调 → UI 状态栏显示"已取消排队任务: <type>"；取消当前任务 → `cancelCurrent()` → workerScopeGuard 退出 → `onComplete`/`onRejected` 回调。明确回调线程（UI 线程，via wxQueueEvent）。

## 3. Important（强烈建议修订）

### I1. 刷新频率与面板可见性

Spec §3.6/§3.7 提 500ms 轮询。但 `MainFrame` 已有 `proxyMonTimer_`（1s）与 `netMonTimer_`。新增两个 500ms timer 在空闲时仍运行 → 持续唤醒 UI 线程。建议：
- 仅当对应标签页处于活动状态时启动 timer（`wxEVT_NOTEBOOK_PAGE_CHANGED`），切换离开时 `Stop()`。
- 或使用单一 1s 全局 timer 统一刷新所有面板（与现有 `proxyMonTimer_` 合并）。

### I2. `taskTypeToString` 的放置与 i18n

Spec 放在 `include/task/TaskType.h` + `src/task/TaskType.cpp`（新文件）。项目现有 TaskType 无 .cpp（纯头文件枚举）。新增 .cpp 需更新 CMakeLists。且中文标签硬编码在 C++ 源码，与项目已有 i18n 策略（无 i18n 框架，直接中文字符串）一致，OK。但建议标签包含英文 TaskType 名（如 "批量测试 (TEST_ALL)"）便于日志对照。

### I3. 日志标签页与 `LogPanel` 的 AUI/布局

现有 `logPanel_` 通过 `centerSizer->Add(logPanel_, 0, wxEXPAND | wxTOP, 2)` + `SetMinSize(wxSize(620, 260))`。改为 notebook 后，notebook 设 MinSize，LogPanel 作为 page 不再需要 SetMinSize（page 由 notebook 管理）。需确保 `LogPanel` 的内部 sizer 在 notebook page 中正确扩展（`AddPage` 不自动 Expand，需 page 自身 sizer）。LogPanel 已有内部 sizer，OK，但需验证。

### I4. `BatchQueuePanel` 读取 `proxyBatchTester_` 的时机

Spec §3.7 仅 500ms 轮询 `getBatchQueueStats()`。但 tester 指针可能在轮询间隙变为 null（批次结束），UI 会短暂显示"无运行中的批量测试"然后可能再次出现（新批次）。建议在 UI 层对"running=false 但 processed>0"做 1s 防抖，避免闪烁。或仅在 `running_` false 且 `processed==0` 时显示空闲。

### I5. 验收标准 #6 UI 自动化测试

Spec 要求"新增 UI 测试覆盖标签页切换"。但项目 UI 自动化测试（`tests/UITests`）基于 wxWidgets 控件 ID 与 `TestWindowSensing`/`TestLayoutSensing`，新增面板需暴露稳定控件 ID（如 `ID_TASK_MONITOR_PANEL`、`ID_BATCH_QUEUE_PANEL`）与 `wxWindow::FindWindowById` 可查。Spec §4 文件清单未包含控件 ID 定义。建议在 spec 补充 ID 枚举与 UI 测试用例列表。

### I6. `TaskQueueSnapshot` 的 `pending` 排序语义

`pendingTasks()` 返回 `std::vector<TaskHandle>`，但 spec 未说明排序（priority desc / createdAt asc）。UI 列表应与实际执行顺序一致。`TaskQueue` 内部 `std::priority_queue` 为 max-heap by priority then earliest createdAt。`snapshot()` 应按相同顺序输出 `pending`。Spec §3.2 应明确。

## 4. Consistency（文档一致性）

### D1. Spec §3.5 成员变量 vs 代码片段不一致

代码片段使用局部 `bottomNotebook`，成员列表使用 `bottomNotebook_`。修订为统一使用成员。

### D2. Spec §4 文件清单遗漏

- 遗漏 `src/task/TaskType.cpp` 的 CMakeLists 注册。
- 遗漏 `TaskQueueSnapshot` 定义位置（建议放在 `TaskQueue.h` 内，与 `TaskHandle` 同命名空间）。
- 遗漏 `BatchQueueStats` 定义位置（`ProxyBatchTester.h`）。

### D3. 与 ProxyTestUnified Phase D 的关系

ProxyTestUnified spec §3.4 Phase D 提到"队列汇总信息（TaskQueue stats）"，本 spec §8 说"互补"。但两者都涉及 TaskQueue 统计展示，需明确边界：ProxyTestUnified Phase D 为日志/控制台输出；本 spec 为 UI 可视化。建议在 spec §8 明确分工，避免重复实现。

## 5. 风险评估

| 风险 | 等级 | 缓解 |
|---|---|---|
| use-after-free（B2） | 高 | 采用 shared_ptr 或明确析构顺序 |
| priority_queue 遍历（B1） | 中 | 成员函数内部访问 |
| UI 线程阻塞（500ms 轮询） | 低 | 仅读内存，无 DB |
| notebook 布局回归 | 中 | Phase 2 独立验证日志功能 |
| 控件 ID 不稳定影响 UI 测试 | 中 | 补充 ID 枚举 |

## 6. 评分

| 维度 | 分数 | 说明 |
|---|---|---|
| 技术可行性 | 7.5/10 | 接口复用充分，但 B2 生命周期风险需解决 |
| 可实施性 | 6.5/10 | B1/B3/B4 需修订后方可实施 |
| 文档完整性 | 7.0/10 | 缺 ID 枚举、回调语义、排序语义 |
| 验收可测性 | 6.5/10 | UI 测试用例未列出 |
| **综合** | **7.0/10** | **CONDITIONAL PASS** |

## 7. 修订建议汇总

1. 解决 B2（tester 生命周期）——推荐 shared_ptr 方案或明确析构顺序 + store nullptr。
2. 解决 B1（priority_queue 遍历方式）——成员函数内部访问。
3. 统一 B3（notebook 成员变量）。
4. 补充 B4（取消排队任务的回调与 UI 反馈）。
5. 补充 I1（timer 仅在活动标签页运行）。
6. 补充 I2（标签格式含英文 TaskType）。
7. 补充 I5（控件 ID 枚举与 UI 测试用例）。
8. 补充 I6（pending 排序语义）。
9. 补充 D1/D2/D3（一致性修正）。

修订后建议升级为 PASS，进入 Phase 1 实施。
