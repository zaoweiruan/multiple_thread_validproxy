# Spec — 任务与队列监控面板 + 日志多标签页 (TaskQueueMonitorAndLogTabs)

- **日期**: 2026-10-30
- **类型**: Spec (SDD)
- **模块**: TaskQueue / ProxyBatchTester / MainFrame / LogPanel / AppController
- **版本**: v1.0
- **作者**: AI Assistant
- **状态**: 🟡 draft (待独立评审)

## 1. 背景与问题

### 1.1 现状

1. **任务不可见**：`TaskQueue` 维护 `current_`（运行中）与 `queue_`（待执行）以及 `history_`，但 UI 仅通过状态栏文字与 `isRunning()` 布尔值感知"有/无任务"。用户无法看到当前正在执行的任务类型、进度、开始时间，也无法看到排队中的任务列表。取消只能 `cancelTest()`/`cancelCurrent()`，缺乏按任务粒度的终止。
2. **批量测试队列不透明**：`ProxyBatchTester` 维护两个队列 `proxiesQueue_`（批量主队列）与 `insertedQueue_`（高优先级插入队列），以及 `totalProxies_`/`processedCount_`/`successCount_`/`failedCount_`。但这些仅写入日志，UI 没有实时的"剩余 X/Y、百分比"展示；用户无法直观判断批次进度与高优先级插入的消化情况。
3. **日志区单一**：底部区域仅为 `LogPanel`（单 `wxTextCtrl`），所有日志混在一起；无法按"日志 / 任务 / 队列"分标签查看，信息密度高时难以定位。

### 1.2 目标

1. **暴露当前执行任务与待执行任务列表，并支持手工终止**：在 UI 提供实时视图，显示当前任务（类型、状态、进度、开始时间）与待执行队列（类型、优先级、提交时间），并提供"终止当前"/"取消排队"按钮。
2. **暴露批量测试双队列剩余情况与百分比**：实时显示主队列剩余、高优先级队列剩余、已处理/总数、成功率、百分比进度。
3. **日志区改为标签页切换**：将底部区域改为 `wxNotebook`，标签页至少包含"日志"、"任务"、"测试队列"，可切换展示。

### 1.3 非目标

- 不改变 `TaskQueue` 的单 worker 串行语义与 `MutexPolicy` 矩阵。
- 不新增持久化（任务/队列状态仅内存、仅当前会话）。
- 不改变日志文件写入行为（`LogPanel` 仍负责文件日志）。
- 不实现跨进程/远程任务管理。

## 2. 数据源与接口

### 2.1 TaskQueue 现有接口（复用）

- `TaskHandle currentTask() const` — 当前运行任务
- `std::vector<TaskHandle> pendingTasks() const` — 待执行任务
- `std::size_t queueDepth() const` — 待执行数量
- `bool isRunning() / isBusy()` — 运行/忙碌状态
- `void cancel(const std::string& taskId)` — 取消指定任务
- `void cancelCurrent()` — 取消当前任务
- `std::vector<TaskHandle> history(n)` — 历史

`TaskHandle` 已含 `taskId/type/status/priority/createdAt/startedAt/completedAt/progressCurrent/progressTotal/progressMessage/error`。

**缺口**：`TaskType` → 可读名称映射不存在；需要 `taskTypeToString(TaskType)` 辅助函数。

### 2.2 ProxyBatchTester 现有计数（需加线程安全 getter）

现有字段（原子）：
- `totalProxies_`、`processedCount_`、`successCount_`、`failedCount_`、`running_`
- `proxiesQueue_`（size, under `queueMutex_`）
- `insertedQueue_`（size, under `insertedMutex_`）

**缺口**：无聚合 getter；`proxiesQueue_`/`insertedQueue_` 的 size 读取必须持锁，UI 线程不能直接访问。新增 `getQueueStats()` 返回快照。

### 2.3 AppController 现有接口（复用/扩展）

- `taskQueue_`（私有）— 需暴露只读快照方法 `getTaskQueueSnapshot()` 或让 UI 通过 controller 聚合。
- `proxyBatchTester_`（原子）— 需暴露 `getBatchQueueStats()`。
- `isRunning()` / `isOnlineProbeRunning()`。

## 3. 设计

### 3.1 后端：TaskType 名称映射

在 `include/task/TaskType.h` 增加：

```cpp
namespace task {
// Returns a human-readable label for a TaskType (e.g. "批量测试", "自动任务").
// Used by monitoring UI; not localized beyond Chinese labels.
const char* taskTypeToString(TaskType t);
} // namespace task
```

实现于 `src/task/TaskType.cpp`（新文件）或追加到 TaskQueue.cpp。避免在头文件内联大量 switch（保持可维护）。

### 3.2 后端：TaskQueue 只读快照

新增 `TaskQueue::snapshot()`：

```cpp
struct TaskQueueSnapshot {
    TaskHandle current;                       // 当前任务（taskId empty if none）
    std::vector<TaskHandle> pending;           // 待执行（按优先级/提交顺序）
    std::size_t queueDepth = 0;
    bool running = false;
    bool busy = false;
};
TaskQueueSnapshot TaskQueue::snapshot() const;
```

实现：持 `mutex_` 拷贝 `current_` 与 `queue_`（转 vector）；原子读 `running_`/`busy_`。线程安全。

`cancel(taskId)` 已存在；UI 终止按钮调用 `controller_->cancelTask(taskId)`（新增薄包装，内部 `taskQueue_.cancel(taskId)` 并更新 UI）。

### 3.3 后端：ProxyBatchTester 队列统计

新增结构与方法：

```cpp
// include/ProxyBatchTester.h
struct BatchQueueStats {
    int total = 0;
    int processed = 0;
    int success = 0;
    int failed = 0;
    int remaining = 0;          // total - processed
    int batchQueueSize = 0;    // proxiesQueue_ size
    int insertedQueueSize = 0;  // insertedQueue_ size
    bool running = false;
    double percent = 0.0;      // processed/total * 100 (0 if total==0)
};
BatchQueueStats getQueueStats() const;
```

实现（`src/ProxyBatchTester.cpp`）：
- `total = totalProxies_.load()`；`processed = processedCount_.load()`；等；
- `batchQueueSize`：持 `queueMutex_` 读 `proxiesQueue_.size()`；
- `insertedQueueSize`：持 `insertedMutex_` 读 `insertedQueue_.size()`；
- `remaining = total - processed`（钳制 ≥0）；
- `percent = total>0 ? processed*100.0/total : 0.0`；
- `running = running_.load()`。

**注意死锁顺序**：先读原子，再分别取两个 mutex（不嵌套），避免锁顺序死锁。

### 3.4 后端：AppController 薄包装

```cpp
// AppController.h
task::TaskQueueSnapshot getTaskQueueSnapshot() const;
BatchQueueStats getBatchQueueStats() const;   // returns empty stats if no running tester
void cancelTask(const std::string& taskId);   // delegates to taskQueue_.cancel
```

`getBatchQueueStats()`：读 `proxyBatchTester_.load()`；若 null 返回默认空 stats；否则调用 tester->getQueueStats()。注意 tester 可能在调用期间析构——通过 `proxyBatchTester_` 原子指针管理生命周期（现有模式），UI 刷新频率 ≤1Hz，风险可接受；若为 null 返回空。

### 3.5 UI：底部标签页容器

`MainFrame::initPanels()` 修改：

```cpp
// 替换：
// centerSizer->Add(logPanel_, 0, wxEXPAND | wxTOP, 2);
// 为：
wxNotebook* bottomNotebook = new wxNotebook(centerPanel, wxID_ANY);
logPanel_ = new LogPanel(bottomNotebook);
// ... setInitialLogLevel ...
bottomNotebook->AddPage(logPanel_, "日志");
taskMonitorPanel_ = new TaskMonitorPanel(bottomNotebook, controller_);
bottomNotebook->AddPage(taskMonitorPanel_, "任务");
batchQueuePanel_ = new BatchQueuePanel(bottomNotebook, controller_);
bottomNotebook->AddPage(batchQueuePanel_, "测试队列");
centerSizer->Add(bottomNotebook, 0, wxEXPAND | wxTOP, 2);
bottomNotebook->SetMinSize(wxSize(620, 260));
```

`MainFrame.h` 新增成员：
```cpp
wxNotebook* bottomNotebook_{nullptr};
class TaskMonitorPanel* taskMonitorPanel_{nullptr};
class BatchQueuePanel* batchQueuePanel_{nullptr};
```

`LogPanel` 保持不变（作为 notebook page）。

### 3.6 TaskMonitorPanel（新文件）

`src/ui/TaskMonitorPanel.h/.cpp`：

布局：
- 顶部：当前任务组（wxStaticBox）：类型名、状态、进度 "current/total"、消息、开始时间、"终止当前"按钮。
- 中部：待执行任务 `wxListCtrl`（列：#、任务ID、类型、优先级、状态、提交时间），每行可选；底部 "取消选中"/"取消全部排队"按钮。
- 刷新：`wxTimer` 500ms → 调 `controller_->getTaskQueueSnapshot()` → 更新控件。仅当面板 visible 时刷新（避免后台开销）。
- 终止：按钮调 `controller_->cancelTask(id)` 或 `cancelCurrent()`；按钮置灰逻辑：无当前任务时禁用"终止当前"；无待执行时禁用"取消全部"。

线程安全：所有 controller 调用在 UI 线程（timer 事件在 UI 线程），OK。

### 3.7 BatchQueuePanel（新文件）

`src/ui/BatchQueuePanel.h/.cpp`：

布局：
- 状态行：运行中/空闲。
- 总数/已处理/成功/失败/剩余。
- 进度条 `wxGauge`（percent）+ 文本 "X/Y (Z%)"。
- 双队列：主队列剩余 N、高优先级队列剩余 M。
- 刷新：`wxTimer` 500ms → `controller_->getBatchQueueStats()`。

### 3.8 事件驱动刷新（可选优化）

为降低轮询，`AppController` 可在任务状态变化时发出 `TaskQueueChangedEvent`（自定义 wxEvent）。但为简化与降低风险，v1.0 采用 500ms 轮询（与现有 `proxyMonTimer_` 1s 轮询模式一致）。Phase 2 可升级为事件驱动。

### 3.9 CMake

新增 `src/ui/TaskMonitorPanel.cpp`、`src/ui/BatchQueuePanel.cpp` 加入构建（UI 源文件列表）。检查 `CMakeLists.txt` 的 UI 源列表。

## 4. 文件改动清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `include/task/TaskType.h` | 修改 | 增加 `taskTypeToString` 声明 |
| `src/task/TaskType.cpp` | 新增 | 实现映射 |
| `include/task/TaskQueue.h` | 修改 | 增加 `TaskQueueSnapshot` + `snapshot()` |
| `src/task/TaskQueue.cpp` | 修改 | 实现 `snapshot()` |
| `include/ProxyBatchTester.h` | 修改 | 增加 `BatchQueueStats` + `getQueueStats()` |
| `src/ProxyBatchTester.cpp` | 修改 | 实现 `getQueueStats()` |
| `src/ui/AppController.h` | 修改 | 增加 `getTaskQueueSnapshot`/`getBatchQueueStats`/`cancelTask` |
| `src/ui/AppController.cpp` | 修改 | 实现薄包装 |
| `src/ui/MainFrame.h` | 修改 | 新增 `bottomNotebook_`/`taskMonitorPanel_`/`batchQueuePanel_` |
| `src/ui/MainFrame.cpp` | 修改 | `initPanels` 改为 notebook |
| `src/ui/TaskMonitorPanel.h/.cpp` | 新增 | 任务监控面板 |
| `src/ui/BatchQueuePanel.h/.cpp` | 新增 | 队列监控面板 |
| `CMakeLists.txt` | 修改 | 注册新源文件 |
| `docs/INDEX.md` | 修改 | 登记 spec + review |

## 5. 验收标准

1. 底部区域显示 3 个标签页："日志"、"任务"、"测试队列"，可切换，日志内容与原 LogPanel 一致（级别过滤、清空、打开文件）。
2. "任务"标签页：空闲时显示"无运行任务"；启动批量测试后显示当前任务类型"批量测试(TEST_ALL)"、状态 RUNNING、进度 "n/total"、开始时间；排队中任务显示在列表。
3. "任务"标签页：点击"终止当前"后，当前任务取消，UI 在 ≤6s 内恢复空闲；排队任务可通过"取消选中"移除（cancel）。
4. "测试队列"标签页：批量测试进行中显示主队列剩余、高优先级队列剩余、已处理/总数、百分比进度条；高优先级代理注入后，高优先级队列剩余数增加并快速消化。
5. 无运行批次时，"测试队列"标签页显示"无运行中的批量测试"。
6. `ctest` 全量通过；UI 自动化 `scripts/build-and-test.bat` 全绿（新增 UI 测试覆盖标签页切换）。
7. 无 `auto` 类型推导；PowerShell 构建命令可用。

## 6. 风险与缓解

| 风险 | 缓解 |
|---|---|
| `proxyBatchTester_` 在 UI 读取时已析构 → 空指针 | `getBatchQueueStats()` 先 load 原子指针；null 返回默认空 stats；刷新 500ms，窗口极小 |
| `TaskQueue::snapshot()` 拷贝 `queue_`（priority_queue）需转换为 vector | 实现中锁定后遍历底层 container（`queue_.c` 可访问，同结构体 friend）或维护辅助 vector； alternatively store pending in `std::vector` mirror. 采用：snapshot 时将 priority_queue 拷贝到 vector 并 sort by priority/createdAt |
| 500ms 轮询开销（DB 读） | BatchQueuePanel 仅读原子+两个 mutex 的 size，无 DB；TaskMonitorPanel 仅读 TaskQueue 内存；开销可忽略 |
| wxNotebook 替换可能破坏 AUI 布局 | 仅替换 centerPanel 内部 sizer 的 logPanel_ 为 notebook，AUI pane 不变；LogPanel 作为 page 保留全部功能 |
| 取消排队任务的语义 | `TaskQueue::cancel(taskId)` 已实现（从 queue_ 移除并回调 onRejected）；UI 需确认回调触发状态栏更新 |

## 7. 实施阶段

- Phase 1（后端）：TaskType 名称映射 + TaskQueue::snapshot() + ProxyBatchTester::getQueueStats() + AppController 薄包装。编译 + 单测。
- Phase 2（UI 容器）：MainFrame 底部 notebook + LogPanel 迁移。编译 + 手动验证日志功能不变。
- Phase 3（UI 面板）：TaskMonitorPanel + BatchQueuePanel + timer 刷新 + 终止按钮。编译。
- Phase 4（测试与文档）：UI 自动化测试（标签切换、终止）、更新 tracker、INDEX 登记。

## 8. 与现有方案的关系

- 复用 Scheme B（池内插入）与 TaskQueue/MutexPolicy，不改变互斥矩阵。
- 复用 ProxyTestUnified Phase B/C 的统一日志/汇总，监控面板仅"展示"，不重复打印。
- 与 Phase D（队列汇总信息）互补：Phase D 聚焦日志/TaskQueue stats；本 spec 聚焦 UI 可视化与交互。
