# Spec — 任务与队列监控面板 + 日志多标签页 (TaskQueueMonitorAndLogTabs)

- **日期**: 2026-10-30
- **类型**: Spec (SDD)
- **模块**: TaskQueue / ProxyBatchTester / MainFrame / LogPanel / AppController
- **版本**: v1.1（修订自 v1.0；修订依据独立评审 B1–B4 / I1–I6 / D1–D3）
- **作者**: AI Assistant
- **状态**: 🟡 reviewed (v1.1 独立评审 CONDITIONAL PASS, 0 blockers)
- **评审对象**: docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.0.md

## 0. 修订记录（v1.0 → v1.1）

| 评审项 | 修订位置 | 修订内容 |
|---|---|---|
| B1 | §3.2, §6 | `snapshot()` 在 TaskQueue 成员函数内部访问 `queue_.c`（protected），按 `EntryCompare` 语义排序；`pending` 输出顺序 = priority desc → createdAt asc |
| B2 | §2.3, §3.4, §6 | `proxyBatchTester_` 改为 `std::shared_ptr<ProxyBatchTester>` + `std::mutex proxyBatchTesterMutex_`（C++17 兼容），UI 读取 copy shared_ptr 后使用，析构先 store nullptr 再 join；消除 use-after-free |
| B3/D1 | §3.5 | 统一使用成员 `bottomNotebook_`，代码片段与成员声明一致 |
| B4 | §3.6, §5 | 明确取消排队任务的 `TaskQueue::cancel` 语义、onRejected 回调、UI 反馈与线程 |
| I1 | §3.6, §3.7 | timer 仅在对应标签页激活时运行（`wxEVT_NOTEBOOK_PAGE_CHANGED`），切换离开 Stop() |
| I2 | §3.1 | 标签格式 `"批量测试 (TEST_ALL)"`（中文 + 英文 TaskType） |
| I3 | §3.5, §6 | notebook 设 MinSize，LogPanel 作为 page 不再 SetMinSize；验证 page sizer 扩展 |
| I4 | §3.7 | 空闲显示条件：`running==false && processed==0`（避免批次结束闪烁） |
| I5 | §3.6, §3.7, §4, §5 | 新增控件 ID 枚举与 UI 测试用例列表 |
| I6 | §3.2, §5 | pending 排序语义明确 |
| D2 | §4 | 文件清单补全 CMake 注册、结构体位置 |
| D3 | §8 | 明确与 ProxyTestUnified Phase D 分工（Phase D = 日志/控制台输出；本 spec = UI 可视化） |

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

### 2.3 AppController 现有接口（复用/扩展）与生命周期策略（B2）

现有：
- `taskQueue_`（私有）— 需暴露只读快照方法 `getTaskQueueSnapshot()` 或让 UI 通过 controller 聚合。
- `proxyBatchTester_` — **v1.1 改为 `std::shared_ptr<ProxyBatchTester>` + `std::mutex proxyBatchTesterMutex_`**（C++17 兼容；不使用 `std::atomic<std::shared_ptr>`，该特性为 C++20）。
- `isRunning()` / `isOnlineProbeRunning()`。

**生命周期策略（B2，解决 use-after-free）**：

1. 成员声明：
   ```cpp
   std::mutex proxyBatchTesterMutex_;
   std::shared_ptr<ProxyBatchTester> proxyBatchTester_;
   ```
2. 所有读取路径（`getBatchQueueStats()`、`insertProxy` 调用、fast-path 注入、watcher 回调注册）：
   ```cpp
   std::shared_ptr<ProxyBatchTester> tester;
   { std::lock_guard<std::mutex> lk(proxyBatchTesterMutex_); tester = proxyBatchTester_; }
   if (!tester) return emptyStats;
   // use tester (refcount 保证对象存活)
   ```
3. 所有写入路径（`doTestAllProxies` 创建/销毁、`cancelTest` 清理）：
   ```cpp
   { std::lock_guard<std::mutex> lk(proxyBatchTesterMutex_); proxyBatchTester_ = tester; }  // assign
   // destroy:
   { std::lock_guard<std::mutex> lk(proxyBatchTesterMutex_); proxyBatchTester_.reset(); }  // store nullptr first
   // then join worker / destroy tester (shared_ptr refcount 自动延迟释放)
   ```
4. 析构顺序保证：先 `proxyBatchTester_.reset()`（store nullptr）→ 再 `join` worker 线程 → 再释放 shared_ptr。UI 读取到的 shared_ptr 副本 refcount 保证对象在 UI 使用期间存活；析构线程 reset 后新读取返回 null，旧副本在 UI 释放时才真正析构。

**备选（若 Phase 1 迁移证明过于侵入）**：方案 (b) 保留 `std::atomic<ProxyBatchTester*>`，析构前先 `store(nullptr)` 再 join 再 delete；UI 读取 `load()` 后 null 检查。存在 TOCTOU（概率极低，500ms 轮询 + 析构前置 null），可接受。Phase 1 实施者可根据编译/测试结果选择 (a) 或 (b)，但必须在 Phase 1 完成后更新本 spec 的 §2.3 选定方案。

**迁移影响**：现有 `proxyBatchTester_.load()`/`.store()` 调用点（AppController.cpp 共 11 处：doTestAllProxies ×4、testSelectedProxiesAsync ×3、doTestSubscription ×3、runAutoTask/resumeAutoTask ×1、cancelTest ×1、getBatchQueueStats ×1）改为 lock+copy/reset 模式。`std::make_unique<ProxyBatchTester>` → `std::make_shared<ProxyBatchTester>`（4 处：doTestAllProxies、testSelectedProxiesAsync、doTestSubscription、auto-task step）。`AutoTaskManager::PoolObserver` 签名从 `std::function<void(ProxyBatchTester*)>` 改为 `std::function<void(const std::shared_ptr<ProxyBatchTester>&)>`（或保留 raw + AppController 在 observer 内 lock+store）。`std::atomic` 头可保留（TaskQueue 仍用 atomic），但 ProxyBatchTester 指针不再 atomic。try/catch 中的 `proxyBatchTester_.store(nullptr)` 改为 `reset()`。

## 3. 设计

### 3.1 后端：TaskType 名称映射（I2）

在 `include/task/TaskType.h` 增加：

```cpp
namespace task {
// Returns a human-readable label for a TaskType.
// Format: "<中文描述> (<TaskType 枚举名>)", e.g. "批量测试 (TEST_ALL)".
// Used by monitoring UI; not localized beyond Chinese labels.
const char* taskTypeToString(TaskType t);
} // namespace task
```

实现于 `src/task/TaskType.cpp`（新文件）。标签必须包含英文 TaskType 名，便于与日志/代码对照（I2）。

### 3.2 后端：TaskQueue 只读快照（B1, I6）

新增 `TaskQueue::snapshot()`：

```cpp
struct TaskQueueSnapshot {
    TaskHandle current;                       // 当前任务（taskId empty if none）
    std::vector<TaskHandle> pending;           // 待执行（按 priority desc → createdAt asc）
    std::size_t queueDepth = 0;
    bool running = false;
    bool busy = false;
};
TaskQueueSnapshot TaskQueue::snapshot() const;
```

**实现要点（B1）**：
- `TaskQueue` 是 `snapshot()` 的成员函数，可访问 `queue_`（`std::priority_queue<Entry, std::vector<Entry>, EntryCompare>`）的 protected 成员 `c`。
- 实现：
  ```cpp
  TaskQueueSnapshot TaskQueue::snapshot() const {
      TaskQueueSnapshot snap;
      std::lock_guard<std::mutex> lk(mutex_);
      snap.current = current_;                   // TaskHandle (current_ 本身就是 TaskHandle)
      snap.queueDepth = queue_.size();
      // priority_queue 底层容器 c 是 protected，成员函数可访问。
      // Entry.createdAt 是 steady_clock::time_point，避免 TaskHandle::createdAt (std::string)
      // 字典序陷阱：先对 Entry 排序再转换。
      std::vector<Entry> entries(queue_.c.begin(), queue_.c.end());
      std::sort(entries.begin(), entries.end(),
                [](const Entry& a, const Entry& b) {
                    if (a.priority != b.priority) return a.priority > b.priority; // priority desc
                    return a.createdAt < b.createdAt;                               // createdAt asc
                });
      snap.pending.reserve(entries.size());
      for (const auto& e : entries) {
          snap.pending.push_back(e.handle);
      }
      snap.running = running_.load(std::memory_order_relaxed);
      snap.busy = busy_.load(std::memory_order_relaxed);
      return snap;
  }
  ```
- `Entry` → `TaskHandle` 转换只拷贝 `handle` 字段，不拷贝 lambda/callback。
- `pending` 排序语义（I6）：与 `std::priority_queue` 的 pop 顺序一致（priority 降序，同 priority 按 createdAt 升序 = 最早提交先执行）。

`cancel(taskId)` 已存在；UI 终止按钮调用 `controller_->cancelTask(taskId)`（新增薄包装，内部 `taskQueue_.cancel(taskId)`）。

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

`getBatchQueueStats()`：按 §2.3 生命周期策略读 `proxyBatchTester_`（lock + copy shared_ptr）；若 null 返回默认空 stats；否则调用 tester->getQueueStats()。shared_ptr refcount 保证读取期间对象存活。

### 3.5 UI：底部标签页容器（B3/D1）

`MainFrame::initPanels()` 修改：

```cpp
// 替换：
// centerSizer->Add(logPanel_, 0, wxEXPAND | wxTOP, 2);
// 为：
bottomNotebook_ = new wxNotebook(centerPanel, wxID_ANY);
logPanel_ = new LogPanel(bottomNotebook_);
// ... setInitialLogLevel ...
bottomNotebook_->AddPage(logPanel_, "日志");
taskMonitorPanel_ = new TaskMonitorPanel(bottomNotebook_, controller_);
bottomNotebook_->AddPage(taskMonitorPanel_, "任务");
batchQueuePanel_ = new BatchQueuePanel(bottomNotebook_, controller_);
bottomNotebook_->AddPage(batchQueuePanel_, "测试队列");
centerSizer->Add(bottomNotebook_, 0, wxEXPAND | wxTOP, 2);
bottomNotebook_->SetMinSize(wxSize(620, 260));
```

`MainFrame.h` 新增成员：
```cpp
wxNotebook* bottomNotebook_{nullptr};
class TaskMonitorPanel* taskMonitorPanel_{nullptr};
class BatchQueuePanel* batchQueuePanel_{nullptr};
```

统一使用成员 `bottomNotebook_`（B3/D1），代码片段与成员声明一致。

**布局（I3）**：notebook 设 MinSize，LogPanel 作为 page 不再 SetMinSize（page 由 notebook 管理）。需验证 LogPanel 内部 sizer 在 notebook page 中正确扩展（`AddPage` 不自动 Expand，page 自身 sizer 负责）。LogPanel 已有内部 sizer，OK，但需在 Phase 2 手动验证。

### 3.6 TaskMonitorPanel（新文件）（B4, I1, I5）

`src/ui/TaskMonitorPanel.h/.cpp`：

布局：
- 顶部：当前任务组（wxStaticBox）：类型名、状态、进度 "current/total"、消息、开始时间、"终止当前"按钮。
- 中部：待执行任务 `wxListCtrl`（列：#、任务ID、类型、优先级、状态、提交时间），每行可选；底部 "取消选中"/"取消全部排队"按钮。
- 刷新：`wxTimer` 500ms → 调 `controller_->getTaskQueueSnapshot()` → 更新控件。**仅当面板对应标签页处于活动状态时启动 timer**（I1）：监听 `wxEVT_NOTEBOOK_PAGE_CHANGED`，激活时 `Start(500)`，离开时 `Stop()`。避免空闲时持续唤醒 UI 线程。
- 终止：按钮调 `controller_->cancelTask(id)` 或 `cancelCurrent()`；按钮置灰逻辑：无当前任务时禁用"终止当前"；无待执行时禁用"取消全部"。

**取消排队任务语义（B4，按代码实际行为修正）**：
- `TaskQueue::cancel(taskId)` 对排队任务：从 `queue_` 移除该 Entry，status 设为 `CANCELLED`，追加到 `history_`，**同步调用 `onComplete` 回调**（不是 `onRejected`；`onRejected` 在当前代码中从未被调用，MutexPolicy 拒绝路径也未接入 submit）。回调在调用线程（UI 线程，因 cancel 由 UI 触发）同步执行；UI 应在回调内 `wxQueueEvent` 更新状态栏。
- 取消当前任务 → `cancelCurrent()` → 设置取消标志 → worker ScopeGuard 退出 → 触发 `onComplete` 回调（status CANCELLED）→ UI 更新。
- UI 反馈：状态栏显示"已取消排队任务: <type>"（queued cancel）或"已取消当前任务: <type>"（current cancel）。UI 不应依赖 `onRejected`（当前未实现）；如需 onRejected 语义，需在后续 Phase 中接入 MutexPolicy 到 submit 并在拒绝时调用 onRejected（超出本 spec 范围）。
- `TaskQueue::cancel(taskId)` 已实现（从 queue_ 移除 + history_ + onComplete 回调）；UI 需确认回调触发状态栏更新。

线程安全：所有 controller 调用在 UI 线程（timer 事件在 UI 线程），OK。

**控件 ID（I5）**：
```cpp
enum TaskMonitorIds {
    ID_TASK_MONITOR_PANEL = wxID_HIGHEST + 100,
    ID_TASK_CURRENT_TYPE,
    ID_TASK_CURRENT_STATUS,
    ID_TASK_CURRENT_PROGRESS,
    ID_TASK_CURRENT_MESSAGE,
    ID_TASK_CURRENT_STARTED,
    ID_TASK_CANCEL_CURRENT,
    ID_TASK_PENDING_LIST,
    ID_TASK_CANCEL_SELECTED,
    ID_TASK_CANCEL_ALL_PENDING,
};
```

### 3.7 BatchQueuePanel（新文件）（I1, I4, I5）

`src/ui/BatchQueuePanel.h/.cpp`：

布局：
- 状态行：运行中/空闲。
- 总数/已处理/成功/失败/剩余。
- 进度条 `wxGauge`（percent）+ 文本 "X/Y (Z%)"。
- 双队列：主队列剩余 N、高优先级队列剩余 M。
- 刷新：`wxTimer` 500ms → `controller_->getBatchQueueStats()`。**仅当对应标签页激活时启动 timer**（I1）。

**空闲显示（I4）**：仅在 `running==false && processed==0` 时显示"无运行中的批量测试"；若 `running==false && processed>0`（批次刚结束），保留最后统计 1s 防抖后再显示空闲，避免闪烁。

**控件 ID（I5）**：
```cpp
enum BatchQueueIds {
    ID_BATCH_QUEUE_PANEL = wxID_HIGHEST + 200,
    ID_BATCH_STATUS,
    ID_BATCH_TOTAL,
    ID_BATCH_PROCESSED,
    ID_BATCH_SUCCESS,
    ID_BATCH_FAILED,
    ID_BATCH_REMAINING,
    ID_BATCH_GAUGE,
    ID_BATCH_PERCENT_TEXT,
    ID_BATCH_MAIN_QUEUE,
    ID_BATCH_INSERTED_QUEUE,
};
```

### 3.8 事件驱动刷新（可选优化）

为降低轮询，`AppController` 可在任务状态变化时发出 `TaskQueueChangedEvent`（自定义 wxEvent）。但为简化与降低风险，v1.1 采用 500ms 轮询 + 标签页激活门控（I1）（与现有 `proxyMonTimer_` 1s 轮询模式一致）。Phase 2 可升级为事件驱动。

### 3.9 CMake

新增 `src/ui/TaskMonitorPanel.cpp`、`src/ui/BatchQueuePanel.cpp`、`src/task/TaskType.cpp` 加入构建（UI 源文件列表 + task 源文件列表）。检查 `CMakeLists.txt` 的 UI 源列表与 task 源列表。

## 4. 文件改动清单（D2 补全）

| 文件 | 动作 | 说明 |
|---|---|---|
| `include/task/TaskType.h` | 修改 | 增加 `taskTypeToString` 声明 |
| `src/task/TaskType.cpp` | 新增 | 实现映射 |
| `CMakeLists.txt` | 修改 | 注册 `src/task/TaskType.cpp` |
| `include/task/TaskQueue.h` | 修改 | 增加 `TaskQueueSnapshot`（同文件、task 命名空间）+ `snapshot()` |
| `src/task/TaskQueue.cpp` | 修改 | 实现 `snapshot()`（成员函数内部访问 `queue_.c`） |
| `include/ProxyBatchTester.h` | 修改 | 增加 `BatchQueueStats`（同文件）+ `getQueueStats()` |
| `src/ProxyBatchTester.cpp` | 修改 | 实现 `getQueueStats()` |
| `src/ui/AppController.h` | 修改 | 增加 `getTaskQueueSnapshot`/`getBatchQueueStats`/`cancelTask`；`proxyBatchTester_` 类型改为 shared_ptr + mutex |
| `src/ui/AppController.cpp` | 修改 | 实现薄包装；迁移 `proxyBatchTester_` 读写为 lock+copy/reset；make_unique→make_shared |
| `include/AutoTaskManager.h` | 修改 | `PoolObserver` 签名改为 shared_ptr（或保留 raw + AppController 适配） |
| `src/ui/MainFrame.h` | 修改 | 新增 `bottomNotebook_`/`taskMonitorPanel_`/`batchQueuePanel_` |
| `src/ui/MainFrame.cpp` | 修改 | `initPanels` 改为 notebook（使用成员 `bottomNotebook_`） |
| `src/ui/TaskMonitorPanel.h/.cpp` | 新增 | 任务监控面板（含控件 ID 枚举） |
| `src/ui/BatchQueuePanel.h/.cpp` | 新增 | 队列监控面板（含控件 ID 枚举） |
| `CMakeLists.txt` | 修改 | 注册 `src/ui/TaskMonitorPanel.cpp`、`src/ui/BatchQueuePanel.cpp` |
| `docs/INDEX.md` | 修改 | 登记 spec + review |

## 5. 验收标准

1. 底部区域显示 3 个标签页："日志"、"任务"、"测试队列"，可切换，日志内容与原 LogPanel 一致（级别过滤、清空、打开文件）。
2. "任务"标签页：空闲时显示"无运行任务"；启动批量测试后显示当前任务类型"批量测试 (TEST_ALL)"、状态 RUNNING、进度 "n/total"、开始时间；排队中任务显示在列表，按 priority desc → createdAt asc 排序。
3. "任务"标签页：点击"终止当前"后，当前任务取消，UI 在 ≤6s 内恢复空闲；排队任务可通过"取消选中"移除（cancel），触发 onComplete 回调（status=CANCELLED），状态栏显示"已取消排队任务: <type>"。
4. "测试队列"标签页：批量测试进行中显示主队列剩余、高优先级队列剩余、已处理/总数、百分比进度条；高优先级代理注入后，高优先级队列剩余数增加并快速消化。
5. 无运行批次时，"测试队列"标签页显示"无运行中的批量测试"（仅 running==false && processed==0 时显示空闲）。
6. `ctest` 全量通过；UI 自动化 `scripts/build-and-test.bat` 全绿（新增 UI 测试覆盖标签页切换、控件 ID 可查）。
7. 无 `auto` 类型推导；PowerShell 构建命令可用。
8. timer 仅在对应标签页激活时运行（切换离开 Stop()）。

**UI 测试用例（I5）**：
- T1：`FindWindowById(ID_TASK_MONITOR_PANEL)` 非 null；`FindWindowById(ID_BATCH_QUEUE_PANEL)` 非 null。
- T2：notebook page count == 3；page text 为 "日志"/"任务"/"测试队列"。
- T3：切换到"任务"标签页后，timer 启动；切换离开后 timer 停止。
- T4：空闲时 TaskMonitorPanel 显示"无运行任务"。
- T5：批量测试进行中，BatchQueuePanel 的 gauge value > 0。

## 6. 风险与缓解

| 风险 | 缓解 |
|---|---|
| use-after-free（B2） | shared_ptr + mutex；UI 读取 copy shared_ptr 保证存活；析构先 reset 再 join |
| `TaskQueue::snapshot()` 访问 priority_queue 底层容器（B1） | 成员函数内部访问 `queue_.c`（protected），按 EntryCompare 语义排序 |
| 500ms 轮询开销（DB 读） | BatchQueuePanel 仅读原子+两个 mutex 的 size，无 DB；TaskMonitorPanel 仅读 TaskQueue 内存；开销可忽略 |
| wxNotebook 替换可能破坏 AUI 布局 | 仅替换 centerPanel 内部 sizer 的 logPanel_ 为 notebook，AUI pane 不变；LogPanel 作为 page 保留全部功能 |
| 取消排队任务的语义 | `TaskQueue::cancel(taskId)` 已实现（从 queue_ 移除并回调 onRejected）；UI 需确认回调触发状态栏更新 |
| proxyBatchTester_ 迁移影响面 | 约 8 个读写调用点，Phase 1 先迁移并编译 + 单测 |

## 7. 实施阶段

- Phase 1（后端）：TaskType 名称映射 + TaskQueue::snapshot() + ProxyBatchTester::getQueueStats() + AppController 薄包装 + proxyBatchTester_ shared_ptr 迁移。编译 + 单测。
- Phase 2（UI 容器）：MainFrame 底部 notebook + LogPanel 迁移。编译 + 手动验证日志功能不变。
- Phase 3（UI 面板）：TaskMonitorPanel + BatchQueuePanel + timer 刷新（激活门控）+ 终止按钮。编译。
- Phase 4（测试与文档）：UI 自动化测试（标签切换、终止）、更新 tracker、INDEX 登记。

## 8. 与现有方案的关系（D3）

- 复用 Scheme B（池内插入）与 TaskQueue/MutexPolicy，不改变互斥矩阵。
- 复用 ProxyTestUnified Phase B/C 的统一日志/汇总，监控面板仅"展示"，不重复打印。
- **与 ProxyTestUnified Phase D 分工**：Phase D（ProxyTestUnified spec §3.4）聚焦日志/控制台输出的队列汇总信息（TaskQueue stats 打印到日志）；本 spec 聚焦 UI 可视化与交互（面板展示 + 手工终止）。两者互补，不重复实现：Phase D 提供数据源的"文本化"，本 spec 提供"可视化"。
- 与 Phase D（队列汇总信息）互补：Phase D 聚焦日志/TaskQueue stats；本 spec 聚焦 UI 可视化与交互。

---

*本 spec v1.1 修订依据独立评审 docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.0.md 的 B1–B4 / I1–I6 / D1–D3。*