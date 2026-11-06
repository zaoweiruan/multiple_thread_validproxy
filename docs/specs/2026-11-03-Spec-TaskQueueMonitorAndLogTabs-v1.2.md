# Spec — 任务与队列监控面板 + 底部视图单选切换 (TaskQueueMonitorAndLogTabs)

- **日期**: 2026-11-03
- **类型**: Spec (SDD)
- **模块**: TaskQueue / ProxyBatchTester / MainFrame / LogPanel / AppController / AutoTaskManager
- **版本**: v1.2（修订自 v1.1；修订依据第二轮独立评审 B1–B4 / I1–I6 / D1–D6，并按 owner 需求变更 D1 改底部视图形态）
- **作者**: AI Assistant
- **状态**: 🟡 drafted（待评审）
- **取代**: `docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.1.md`
- **评审依据**: `docs/reviews/2026-11-03-Review-TaskQueueMonitorAndLogTabs-v1.2.md`

---

## 0. 修订记录

### 0.1 需求变更（owner 指令，2026-11-03）

> **D1**：底部"日志 / 任务 / 测试队列"**不再使用 `wxNotebook` 标签页**，改为**单选控件切换、三者共用同一窗口区域**的形式。

采纳形态：`wxRadioBox` 单选（三个互斥选项）+ **同一容器内三页 `Hide()/Show()` 切换**。理由（均有 wxWidgets 源码依据）：

1. **零 UIA 布局回归**：`tests/ui/TestLayoutSensing.cpp:59-61` 断言"清空日志窗口/日志统计/打开日志"三按钮在 UIA 树中可见。`wxNotebook` 的非激活页不进入 UIA 树，一旦默认页不是"日志"，`UI_LAYOUTSENSING`/`UI_CLEAR` 立即变红；`Hide()/Show()` 方案中日志页始终处于"已显示"状态，既有断言与 `tests/ui/UIIds.h:4-7,17-22`（"main frame has NO notebook tabs"）的表述都得以继续成立。
2. **日志流不中断**：`LogPanel` 通过 `Logger::setLogCallback` + `wxQueueEvent` 持续 `AppendText`（`src/ui/LogPanel.cpp:73-78`）。`wxNotebook` 切走时页窗口被 `Show(false)`（`src/msw/notebook.cpp` `UpdateSelection`），在隐藏窗口上持续写文本控件既浪费又易出绘制问题；本方案日志页恒可见。
3. **隐藏项不参与布局**：`wxBoxSizer::CalcMin()`（`src/common/sizer.cpp:2819`）与 `wxBoxSizer::DoLayout()`（同文件 2543/2560）都跳过 `!item->IsShown()` 的子项，因此三页共用一个比例槽位时，隐藏页不会撑高区域，窗口缩放行为与现状一致。
4. 实现与心智负担更低：无 notebook 页生命周期、无 `wxEVT_NOTEBOOK_PAGE_CHANGED` 绑定对象问题。

### 0.2 评审项处置（对应第二轮评审 §9 修订清单）

| 评审项 | 严重度 | 修订位置 | 修订内容 |
|---|---|---|---|
| **B1** | 阻断 | §3.2、§6 | 删除"成员函数可访问 `queue_.c`"方案（`std::priority_queue::c` 是 protected，非派生类成员同样不可访问，g++ 14.2.0 实测编译失败）。改为**持锁内联复制 + 排空**（与现有 `pendingTasks()` 同构），排空顺序即 pop 顺序，**不需要 `std::sort`**。并显式警告：`snapshot()` 内**不得**调用 `pendingTasks()`（同一把非递归 `mutex_` → 自死锁） |
| **B2** | 阻断 | §3.3、§3.4、§4 | `getQueueStats()` / `getBatchQueueStats()` **去掉 `const`**（`queueMutex_`/`insertedMutex_`/`proxyBatchTesterMutex_` 均非 `mutable`，const 下加锁实测编译失败）。选定"去 const"方案而非"加 mutable"，改动面最小 |
| **B3-a** | 阻断 | §3.5、§3.8 | `AppController::cancelTask(id)` **分流**：`id == snapshot().current.taskId` → `cancelTest()`（唯一真正停止批量测试的链路：置 `AppController::cancelRequested_` + `taskQueue_.cancelCurrent()` + `taskQueue_.forceRelease()`）；否则 `taskQueue_.cancel(id)`。依据：任务 lambda 一律 `(void)ctx;`，`ProxyBatchTester` 只看 `externalCancel_`（= `AppController::cancelRequested_`） |
| **B3-b** | 阻断 | §3.4 | `TaskQueue::cancel` 的回调**移出临界区**（现为 `TaskQueue.cpp:77` 持锁 / `:94` 锁内调 `onComplete` → UI 回调内再访问 TaskQueue 即死锁）。改为锁内收集、锁外回调 |
| **B3-c** | 阻断 | §3.4、§3.8、§4 | 新增 `TaskQueue::cancelAllPending()`（一次持锁清空 + 锁外批量回调），支撑"取消全部排队"按钮；禁止用循环 `cancel(id)` 代替（O(n² log n) + 重复持锁回调） |
| **B4** | 阻断 | §2.3、§3.6、§4 | AutoTaskManager **必须持有 `std::shared_ptr<ProxyBatchTester>`**：`stepTestAll()` 的 tester 由栈对象改为 `make_shared`；`PoolObserver` 签名改为 `std::function<void(const std::shared_ptr<ProxyBatchTester>&)>`；**删除 v1.1 的"或保留 raw + AppController 在 observer 内 lock+store"备选**（裸指针塞 shared_ptr 需空 deleter，不延长生命周期，B2 想消灭的 UAF 原样复活）。§2.3 生命周期备选方案 (b) 一并删除，不留"实施时再选"的余地 |
| **I1** | 重要 | §3.7、§3.8 | `TaskHandle` 时间戳是 `steady_clock` 计数值字符串，**无法还原墙上时间**。UI 统一显示**相对时间**（`elapsedSecondsSince(steadyString, now)` → `"12s"`，解析失败/空串 → `"- -"`） |
| **I2** | 重要 | §3.2 | 明确 `snapshot.running` = worker 线程存活（构造后即 true，仅调试用），`snapshot.busy` = 有任务执行中（**UI 唯一判据**）；空闲判定统一为 `busy == false && current.taskId.empty()` |
| **I3** | 重要 | §3.8 | 单选控件不存在 notebook 事件问题：`wxEVT_RADIOBOX` 由 `MainFrame` 绑定（`bottomViewRadio_->Bind(...)`），切换后直接调用目标面板的 `setActive(bool)` 与 `refreshNow()`。timer 由**唯一的 `MainFrame::bottomViewTimer_`（1000ms）**驱动，仅向当前可见页派发刷新 |
| **I4** | 重要 | §3.9 | 控件 ID 改到未占用段位 `wxID_HIGHEST + 1200`（底部视图）/ `+ 1300`（任务面板）/ `+ 1400`（队列面板），集中定义在 `src/ui/UiControlIds.h` 并由 `tests/ui/UIIds.h` include，避免与 `ID_MENU_IMPORT_SUB`(=`+100`)、`ID_LOG_CLEAR`/`ID_TOOL_UPDATE_ALL`(=`+200`) 撞号 |
| **I5** | 重要 | §5 | UI 测试用例全部改写为**外部 UIA 可执行**形式（`UITests.exe` 是独立进程，不能调用进程内 `FindWindowById`）；删除 notebook 相关断言；新增"三个单选项存在且互斥"、"默认选中日志"、"切到任务页后日志按钮不可见但切回可见"、"窗口缩放后日志按钮仍可见" |
| **I6** | 重要 | §3.1、§3.10 | 给出 `TaskType` 全部 **20 枚举 + `NONE`** 的中文映射表与 `default` 兜底；新增代码禁用 `auto`（AGENTS.md §1 红线），并写明 C++17 约束（不使用 `std::atomic<std::shared_ptr>`） |
| **D1** | 一致性 | §6 | 风险表"回调 onRejected"→"回调 onComplete（status=CANCELLED）" |
| **D2** | 一致性 | §2.3 | 调用点更正为 **13 处**（`doTestAllProxies` 4 / `testSelectedProxiesAsync` 4 / `doTestSubscription` 4 / `runAutoTask`+`resumeAutoTask` 1），删除把尚不存在的 `getBatchQueueStats` 计为现有调用点的表述 |
| **D3** | 一致性 | §8 | 删除与 ProxyTestUnified Phase D 分工的重复段落 |
| **D4** | 一致性 | §4、§7 | 补后端单测：`tests/test_task_queue.cpp` 扩展（快照排序 / 取消后快照 / 回调不持锁）+ 新增 `tests/test_task_type.cpp`（映射表全覆盖）；§4 文件清单与 CMake 注册项补全 |
| **D5** | 一致性 | §4 | 文件清单补 `src/AutoTaskManager.cpp`、`tests/ui/UIIds.h`，删除 `wx/notebook.h` 项 |
| **D6** | 一致性 | §5 | 验收"≤6s 恢复空闲"改为可测判据（点终止后 ≤2s 出现状态栏文案，且 `isRunning()` 在 worker 正常退出后转 false），并单列 worker 卡死为已知限制 |

---

## 1. 背景与问题

### 1.1 现状

1. **任务不可见**：`TaskQueue` 维护 `current_`（运行中）与 `queue_`（待执行）以及 `history_`，但 UI 仅通过状态栏文字与 `isRunning()` 布尔值感知"有/无任务"。用户无法看到当前正在执行的任务类型、进度、开始时间，也看不到排队中的任务列表；取消只能整体 `cancelTest()`，缺乏按任务粒度的终止。
2. **批量测试队列不透明**：`ProxyBatchTester` 维护 `proxiesQueue_`（批量主队列）与 `insertedQueue_`（高优先级插入队列）以及 `totalProxies_`/`processedCount_`/`successCount_`/`failedCount_`，但只写日志，UI 无"剩余 X/Y、百分比"的实时视图。
3. **日志区单一**：底部区域只有 `LogPanel`（单 `wxTextCtrl`，`src/ui/MainFrame.cpp:900-908`），信息密度高时难以同时观察任务与队列状态。

### 1.2 目标

1. **暴露当前执行任务与待执行任务列表，并支持手工终止**（类型、状态、进度、已运行时长、排队顺序；"终止当前"/"取消选中"/"取消全部排队"）。
2. **暴露批量测试双队列剩余与百分比**（主队列剩余、高优先级队列剩余、已处理/总数、成功/失败、进度条）。
3. **底部区域改为"单选切换 + 共用窗口"**：一个 `wxRadioBox`（日志 / 任务 / 测试队列）三选一，三个面板共用同一块区域，任一时刻只显示其一；**默认显示"日志"**。

### 1.3 非目标

- 不改变 `TaskQueue` 的单 worker 串行语义与 `MutexPolicy` 矩阵。
- 不接入 `onRejected`/`MutexPolicy→submit` 拒绝路径（`onRejected` 至今全仓从未被调用，属独立议题）。
- 不新增持久化（任务/队列状态仅内存、仅当前会话）。
- 不改变日志文件写入行为与 `LogPanel` 的级别过滤/清空/统计/打开日志功能。
- 不实现跨进程/远程任务管理；不实现任务的暂停/恢复/调序。

---

## 2. 数据源与接口（现状核对）

### 2.1 TaskQueue（`include/task/TaskQueue.h`）

可直接复用：

| 接口 | 位置 | 说明 |
|---|---|---|
| `TaskHandle currentTask() const` | `TaskQueue.h:60` | 当前任务，`taskId` 空表示无 |
| `std::vector<TaskHandle> pendingTasks() const` | `TaskQueue.h:68` / `.cpp:154-163` | **已存在**，复制队列后 `top()/pop()` 排空 ⇒ 顺序即出队顺序 |
| `bool hasPending(TaskType) const` | `TaskQueue.h:67` | 按类型探测排队 |
| `std::size_t queueDepth() const` | `TaskQueue.h:59` | 排队数量 |
| `void cancel(const std::string&)` | `.cpp:76-101` | 排队项：重建队列 + `history_` + **锁内** `onComplete`；当前项：置取消标志 |
| `void cancelCurrent()` | `.cpp:103-106` | 仅置 TaskQueue 内部取消标志（**不停业务**） |
| `void forceRelease()` | `.cpp:108-123` | 强制释放 `busy_` 并归档 `current_` |
| `bool isBusy() const` | `TaskQueue.h:71` | 有任务执行中（UI 判据） |

`TaskHandle`（`include/task/TaskType.h:41-62`）字段：`taskId/type/status/priority/createdAt/startedAt/completedAt/error/parentTaskId/child*/progressCurrent/progressTotal/progressMessage`。

**已知事实（决定设计）**：

- `createdAt/startedAt/completedAt` 是 **`steady_clock` 计数值的字符串**（写入点 `.cpp:41/187/227`），非墙上时间 ⇒ UI 只能显示相对时长（§3.7）。
- `running_` 由 `workerLoop()` 首行置 true（`.cpp:166`），仅析构时 false ⇒ "worker 存活"而非"任务执行中"（仓库 2026-10-28 已因同一坑新增 `isBusy()`）。
- 任务 lambda **一律 `(void)ctx;`**（`AppController.cpp:660/691/710/778`），全仓无 `ctx.isCancelled()`/`ctx.progress()` 调用 ⇒ **TaskQueue 层的取消/进度对业务无效**，取消必须走 `AppController::cancelRequested_`，进度必须走 `ProxyBatchTester` 计数。

**缺口**：无聚合快照、无 `cancelAllPending()`、无 `TaskType` → 可读名映射。

### 2.2 ProxyBatchTester（`include/ProxyBatchTester.h`）

| 字段 | 位置 | 读取方式 |
|---|---|---|
| `totalProxies_` / `processedCount_` / `successCount_` / `failedCount_` / `running_` | `:118-121`、`:140` | 原子，直接 `load()` |
| `proxiesQueue_` + `queueMutex_` | `:123-124` | 读 size **必须持锁**（非 `mutable` ⇒ getter 不能 `const`） |
| `insertedQueue_` + `insertedMutex_` | `:131-132` | 同上 |
| `externalCancel_` | `:155` | = `&AppController::cancelRequested_`（构造注入，`AppController.cpp:713/779/3249`、`AutoTaskManager.cpp:244`）；业务在 `ProxyBatchTester.cpp:504/640/661/689/893` 轮询它 |

### 2.3 AppController 生命周期策略（B2/B4）

现状：`std::atomic<ProxyBatchTester*> proxyBatchTester_`（`src/ui/AppController.h:312`），**13 处**读写（`AppController.cpp` 行号）：

| 入口 | 行号 | 次数 |
|---|---|---|
| `testAllProxiesAsync` | 672(`load`) 之外为 721/729/732 + 672 属 `testSingleProxyAsync` | 见下 |
| `testSingleProxyAsync`（快路径注入） | 672 | 1 |
| `testAllProxiesAsync` | 721, 729, 732 | 3 |
| `testSelectedProxiesAsync` | 748, 789, 797, 800 | 4 |
| `doTestSubscription` | 3258, 3260, 3293, 3299 | 4 |
| `runAutoTask`/`resumeAutoTask`（observer） | 4122 | 1 |
| **合计** | | **13** |

**v1.2 决策（单一方案，不留备选）**：

```cpp
// AppController.h
std::mutex proxyBatchTesterMutex_;                        // 只保护指针本身
std::shared_ptr<ProxyBatchTester> proxyBatchTester_;      // 替代 std::atomic<T*>
```

- 读（UI 轮询、快路径注入）：
  ```cpp
  std::shared_ptr<ProxyBatchTester> tester;
  { std::lock_guard<std::mutex> lk(proxyBatchTesterMutex_); tester = proxyBatchTester_; }
  if (!tester) { /* 空 stats / 走排队路径 */ }
  ```
- 写（创建/销毁）：`{ lock; proxyBatchTester_ = tester; }` / `{ lock; proxyBatchTester_.reset(); }`，reset 后**不等待** UI 副本，由引用计数延迟真正析构。
- `make_unique<ProxyBatchTester>` → `make_shared<ProxyBatchTester>`：`AppController.cpp:713/779/3249` **3 处** + `AutoTaskManager.cpp:244` **1 处** = 共 4 处。
- `AutoTaskManager`（B4，必须改）：
  ```cpp
  // include/AutoTaskManager.h:96
  using PoolObserver = std::function<void(const std::shared_ptr<ProxyBatchTester>&)>;
  // src/AutoTaskManager.cpp:244  栈对象 → make_shared（run() 期间由局部 shared_ptr 保活）
  std::shared_ptr<ProxyBatchTester> tester = std::make_shared<ProxyBatchTester>(db_, cfg, baseDir_, cancelPtr, netMon_);
  if (poolObserver_) poolObserver_(tester);
  ```
  撤销回调（`.cpp:253/258`）传 `nullptr`（隐式构造空 shared_ptr）。
- 锁顺序：`proxyBatchTesterMutex_` 仅用于 copy/assign，**持锁期间不得调用 tester 任何方法** ⇒ 不与 `queueMutex_`/`insertedMutex_` 构成嵌套。
- C++17 约束：不使用 `std::atomic<std::shared_ptr>`（C++20 特性）。

---

## 3. 设计

### 3.1 后端：`taskTypeToString`（I2/I6）

新增 `src/task/TaskType.cpp`（`TaskType.h` 现为 header-only，加声明即可被 UI 与 core 共用）：

```cpp
namespace task {
// "<中文描述> (<TaskType 枚举名>)"，例如 "批量测试 (TEST_ALL)"。
// 供监控面板与日志共用；未列出的枚举返回 "未知任务 (UNKNOWN)"。
const char* taskTypeToString(TaskType t);
}
```

**完整映射表（21 项，必须全覆盖）**：

| TaskType | 标签 |
|---|---|
| `NONE` | 无任务 (NONE) |
| `UPDATE_SUBSCRIPTION` | 更新订阅 (UPDATE_SUBSCRIPTION) |
| `UPDATE_ALL` | 更新全部订阅 (UPDATE_ALL) |
| `TEST_SUBSCRIPTION` | 测试订阅 (TEST_SUBSCRIPTION) |
| `TEST_SINGLE_PROXY` | 测试单个代理 (TEST_SINGLE_PROXY) |
| `TEST_ALL` | 批量测试 (TEST_ALL) |
| `TEST_BATCH` | 批量测试容器 (TEST_BATCH) |
| `TEST_ONLINE` | 测试在线代理 (TEST_ONLINE) |
| `FIND_FIRST` | 查找首个可用代理 (FIND_FIRST) |
| `FIND_BEST` | 查找最佳代理 (FIND_BEST) |
| `FIND_BY_INDEX` | 按编号查找代理 (FIND_BY_INDEX) |
| `DEDUP` | 移除重复代理 (DEDUP) |
| `SYNC_DB` | 同步数据库 (SYNC_DB) |
| `EXPORT_SHARE_LINKS` | 导出分享链接 (EXPORT_SHARE_LINKS) |
| `RESOLVE_REGION` | 批量解析地区 (RESOLVE_REGION) |
| `RESOLVE_SINGLE_REGION` | 解析单个地区 (RESOLVE_SINGLE_REGION) |
| `GENERATE_CONFIG` | 生成配置 (GENERATE_CONFIG) |
| `AUTO_TASK` | 自动任务 (AUTO_TASK) |
| `PROBE_POOL_MEMBERS` | 探活池成员 (PROBE_POOL_MEMBERS) |
| `PROBE_MEMBER` | 探活单个池成员 (PROBE_MEMBER) |

实现：单个 `switch`，`default` 分支返回 `"未知任务 (UNKNOWN)"`；**不使用 `auto`**，不使用 `std::map`（编译期常量表或 `switch` 均可）。

### 3.2 后端：`TaskQueue::snapshot()`（B1/I2/I6）

```cpp
// include/task/TaskQueue.h  （namespace task 内，紧接 TaskCallbacks 之后）
struct TaskQueueSnapshot {
    TaskHandle current;                 // current_.taskId 为空表示无运行任务
    std::vector<TaskHandle> pending;    // 待执行，顺序 == 实际出队顺序（priority desc → createdAt asc）
    std::size_t queueDepth = 0;
    bool busy = false;                  // 有任务执行中（UI 判据）
    bool workerAlive = false;           // worker 线程存活（仅调试；原 running_，恒 true）
};

TaskQueueSnapshot snapshot() const;     // 与 queueDepth()/currentTask() 并列
```

实现（`src/task/TaskQueue.cpp`，与 `pendingTasks()` 同构）：

```cpp
task::TaskQueueSnapshot TaskQueue::snapshot() const {
    task::TaskQueueSnapshot snap;
    std::lock_guard<std::mutex> lock(mutex_);
    snap.current = current_;                                   // current_ 本身就是 TaskHandle
    snap.queueDepth = queue_.size();
    // priority_queue 的底层容器 c 是 protected，外部与"非派生类的成员函数"都不可访问；
    // 复制一份再 top()/pop() 排空，得到的顺序天然等于出队顺序（无需 std::sort，
    // 也避开 TaskHandle::createdAt 字符串的字典序陷阱）。
    std::priority_queue<Entry, std::vector<Entry>, EntryCompare> tmp = queue_;
    snap.pending.reserve(tmp.size());
    while (!tmp.empty()) {
        snap.pending.push_back(tmp.top().handle);
        tmp.pop();
    }
    snap.busy = busy_.load(std::memory_order_relaxed);
    snap.workerAlive = running_.load(std::memory_order_relaxed);
    return snap;
}
```

约束：

- **禁止**在 `snapshot()` 内调用 `pendingTasks()` / `queueDepth()` / `currentTask()`：同一线程重复锁非递归 `mutex_` ⇒ **自死锁**。
- `pending` 排序语义（I6）：与 `EntryCompare`（`TaskQueue.h:84-89`，max-heap by priority，同优先级 createdAt 升序）的 pop 顺序一致；§5 用"先塞阻塞任务再按不同优先级排队 3 个"的确定性用例锁死该顺序。
- 复制 `Entry` 会复制 `std::function`；队列规模上限为个位数（同一时刻排队的手动操作），500ms 轮询下开销可忽略。若日后成为热点，再引入 `pendingMirror_`（`std::vector<TaskHandle>` 镜像）——本期不做（YAGNI）。

### 3.3 后端：`ProxyBatchTester::getQueueStats()`（B2）

```cpp
// include/ProxyBatchTester.h
struct BatchQueueStats {
    int total = 0;
    int processed = 0;
    int success = 0;
    int failed = 0;
    int remaining = 0;              // max(0, total - processed)
    int batchQueueSize = 0;         // proxiesQueue_.size()
    int insertedQueueSize = 0;      // insertedQueue_.size()
    bool running = false;
    double percent = 0.0;           // total > 0 ? processed*100.0/total : 0.0
};

// 非 const：内部需持 queueMutex_/insertedMutex_（二者非 mutable）。调用方均为 UI 线程。
BatchQueueStats getQueueStats();
```

实现要点：先 `load()` 全部原子，再**分别**取 `queueMutex_` 与 `insertedMutex_`（**不嵌套**，避免与 worker 的锁顺序冲突），最后做钳制与百分比计算。

### 3.4 后端：`TaskQueue` 取消语义修正（B3-b/B3-c）

```cpp
void cancel(const std::string& taskId);   // 既有签名不变，行为修正
void cancelAllPending();                  // 新增：取消全部排队任务（不影响当前任务）
```

`cancel(taskId)`（当前任务分支不变，排队分支修正为锁外回调）：

```cpp
void TaskQueue::cancel(const std::string& taskId) {
    std::vector<std::pair<TaskHandle, TaskCallbacks>> cancelled;   // 出锁后回调
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (current_.taskId == taskId) {                 // 当前任务：仅置标志
            cancelRequested_.store(true);
            if (currentCancelFlag_) currentCancelFlag_->store(true);
            return;                                      // 回调由 worker 收尾时触发
        }
        std::priority_queue<Entry, std::vector<Entry>, EntryCompare> rebuilt;
        while (!queue_.empty()) {
            Entry e = queue_.top();
            queue_.pop();
            if (e.handle.taskId == taskId) {
                e.handle.status = TaskStatus::CANCELLED;
                history_.push_back(e.handle);
                if (history_.size() > historyLimit_) history_.erase(history_.begin());
                cancelled.emplace_back(e.handle, e.callbacks);
            } else {
                rebuilt.push(std::move(e));
            }
        }
        queue_ = std::move(rebuilt);
    }                                                    // ← 锁在此释放
    for (const std::pair<TaskHandle, TaskCallbacks>& item : cancelled) {
        if (item.second.onComplete) item.second.onComplete(item.first);   // 锁外
    }
}
```

`cancelAllPending()`：同一模式，把全部 `Entry` 排空进 `cancelled`，`queue_` 置空，锁外逐个回调 `onComplete`。**回调异常处理**：回调内异常不得逃逸回 `TaskQueue`，实现处用 `try { ... } catch (...) {}` 包裹（回调是 UI 侧代码，不能让 UI 回调异常杀死调用线程）。

被取消任务的回调语义（沿用 v1.1 §3.6 的代码事实结论）：

- 走 `onComplete`（`status = CANCELLED`），**不是** `onRejected`（全仓从未调用，接入属独立议题）。
- 回调在**调用线程**（UI 线程，因为取消由 UI 触发）同步执行 ⇒ UI 侧只做 `wxQueueEvent` 投递或直接更新控件，**不得**在回调里阻塞。
- 取消当前任务：`cancelCurrent()` 置标志 → worker 的 `ScopeGuard` 退出 → worker 线程调 `onComplete`（`.cpp:238`）。

### 3.5 后端：AppController 薄包装（B3-a）

```cpp
// src/ui/AppController.h
task::TaskQueueSnapshot getTaskQueueSnapshot() const;   // 仅转发 taskQueue_.snapshot()
BatchQueueStats         getBatchQueueStats();           // 非 const（B2）
void cancelTask(const std::string& taskId);             // 分流，见下
```

`getBatchQueueStats()`：按 §2.3 lock+copy 取 `tester`；为空返回默认 `BatchQueueStats{}`；否则返回 `tester->getQueueStats()`。

`cancelTask(taskId)` **必须分流**（B3-a，核心）：

```cpp
void AppController::cancelTask(const std::string& taskId) {
    const task::TaskQueueSnapshot snap = taskQueue_.snapshot();
    if (!snap.current.taskId.empty() && snap.current.taskId == taskId) {
        cancelTest();          // 唯一有效路径：置 cancelRequested_ + cancelCurrent() + forceRelease()
        return;
    }
    taskQueue_.cancel(taskId); // 排队任务：从 queue_ 移除 + history_ + 锁外 onComplete
}

void AppController::cancelAllQueuedTasks() { taskQueue_.cancelAllPending(); }
```

依据：现有任务 lambda 全部 `(void)ctx;`，`ProxyBatchTester` 只认 `externalCancel_`（= `AppController::cancelRequested_`，`ProxyBatchTester.cpp:640/661/689/893`）。只调 `TaskQueue::cancel/cancelCurrent` 时批量测试**不会停**，且 `isRunning()` 仍为 true。

### 3.6 AutoTaskManager 迁移（B4）

- `include/AutoTaskManager.h:96`：`using PoolObserver = std::function<void(const std::shared_ptr<ProxyBatchTester>&)>;`
- `src/AutoTaskManager.cpp:244-259`：`ProxyBatchTester tester(...)` → `std::shared_ptr<ProxyBatchTester> tester = std::make_shared<ProxyBatchTester>(...)`；`tester.run()` → `tester->run()`；撤销点 `poolObserver_(nullptr)` 保持。
- `src/ui/AppController.cpp:4116-4123` observer lambda：形参改 `const std::shared_ptr<ProxyBatchTester>&`，内部 `{ lock; proxyBatchTester_ = tester; }`，`setRowsUpdatedCallback` 逻辑不变。
- **禁止**任何形式的"裸指针 + 空 deleter aliasing shared_ptr"。

### 3.7 时间显示工具（I1）

`TaskHandle` 的三个时间字段是 `steady_clock` 计数值字符串，**不可**渲染为日期时间。新增纯函数（便于单测，置于 `include/task/TaskFormat.h` / `src/task/TaskFormat.cpp`）：

```cpp
namespace task {
// 把 steady_clock 计数值字符串换算为"已过去秒数"；解析失败或空串返回 -1。
long long elapsedSecondsSince(const std::string& steadyCount,
                              std::chrono::steady_clock::time_point now);
} // namespace task
```

面板显示：当前任务"已运行 `Ns`"；排队任务"提交于 `Ns` 前"；`-1` 显示 `"- -"`。

### 3.8 UI：底部区域 = 单选切换 + 共用窗口（D1/I3）

#### 3.8.1 容器结构（替换 `MainFrame.cpp:900-908`）

```
centerPanel
└ centerSizer (VERTICAL)
  ├ splitter_                        proportion 1, wxEXPAND         （不变）
  └ bottomPanel_   (wxPanel, wxBORDER_SUNKEN, MinSize 620×260)      proportion 0, wxTOP 2
    └ bottomSizer_ (VERTICAL)
      ├ bottomViewRadio_  (wxRadioBox "视图"：日志 | 任务 | 测试队列, 1 行 3 列)
      └ [槽位 proportion 1, wxEXPAND]  ← logPanel_ / taskMonitorPanel_ / batchQueuePanel_ 三选一
```

```cpp
// src/ui/MainFrame.cpp::initPanels()（替换原 logPanel_ 直挂方案）
bottomPanel_ = new wxPanel(centerPanel, ID_BOTTOM_VIEW_PANEL);
wxBoxSizer* bottomSizer = new wxBoxSizer(wxVERTICAL);

wxArrayString bottomViewChoices = {wxT("日志"), wxT("任务"), wxT("测试队列")};
                                                 // wxArrayString 有 initializer_list 构造（arrstr.h）
bottomViewRadio_ = new wxRadioBox(bottomPanel_, ID_BOTTOM_VIEW_RADIO, wxT("视图"),
                                  wxDefaultPosition, wxDefaultSize,
                                  bottomViewChoices,
                                  3,                                  // majorDim=3 + wxRA_SPECIFY_COLS ⇒ 1 行 3 列
                                  wxRA_SPECIFY_COLS | wxNO_BORDER);
bottomViewRadio_->SetSelection(0);              // 默认"日志"
bottomSizer->Add(bottomViewRadio_, 0, wxEXPAND | wxALL, 2);

logPanel_         = new LogPanel(bottomPanel_);
taskMonitorPanel_ = new TaskMonitorPanel(bottomPanel_, controller_);
batchQueuePanel_  = new BatchQueuePanel(bottomPanel_, controller_);
std::vector<wxWindow*> pages;
pages.push_back(logPanel_);
pages.push_back(taskMonitorPanel_);
pages.push_back(batchQueuePanel_);
for (std::size_t i = 0; i < pages.size(); ++i) {
    bottomSizer->Add(pages[i], 1, wxEXPAND);
    pages[i]->Hide();                           // 三页先隐藏，由 switchBottomView 只显示一个
}
bottomPanel_->SetSizer(bottomSizer);
bottomPanel_->SetMinSize(wxSize(620, 260));     // 与原 logPanel_ 的 MinSize 一致
centerSizer->Add(bottomPanel_, 0, wxEXPAND | wxTOP, 2);

bottomViewRadio_->Bind(wxEVT_RADIOBOX, &MainFrame::onBottomViewSelected, this, ID_BOTTOM_VIEW_RADIO);
switchBottomView(0);                            // 默认"日志"

// timer 复用仓库既有范式（MainFrame.cpp:492-494 / 1031-1033）：
//   wxTimer* + Bind(wxEVT_TIMER, handler, this, ID) —— 必须带 ID，
//   否则与 netMonTimer_/proxyMonTimer_ 抢同一 handler（MainFrame.cpp:75 已记录该坑）
bottomViewTimer_ = new wxTimer(this, ID_BOTTOM_VIEW_TIMER);
Bind(wxEVT_TIMER, &MainFrame::onBottomViewTimer, this, ID_BOTTOM_VIEW_TIMER);
bottomViewTimer_->Start(1000);
```

- `MainFrame` 新增成员：`wxPanel* bottomPanel_{nullptr};`、`wxRadioBox* bottomViewRadio_{nullptr};`、`TaskMonitorPanel* taskMonitorPanel_{nullptr};`、`BatchQueuePanel* batchQueuePanel_{nullptr};`、`wxTimer* bottomViewTimer_{nullptr};`（**不再需要** `bottomNotebook_`）。析构沿用 `netMonTimer_` 的写法：`Stop()` + `delete` + 置 `nullptr`（`MainFrame.cpp:625-634`）。
- 头文件包含：`wx/radiobox.h`、`wx/timer.h` 已由 `MainFrame.h:4` 的 `<wx/wx.h>` 传递包含（已核对 `D:/vcpkg/installed/x64-mingw-dynamic/include/wx/wx.h` 含 `#include "wx/radiobox.h"`），无需新增 include。
- **LogPanel 不再 `SetMinSize(620,260)`**（原 `MainFrame.cpp:908`），最小尺寸约束上移到 `bottomPanel_`；`LogPanel` 内部 sizer（`LogPanel.cpp:64-66`）在页内正常扩展。
- 默认页必须是"日志"（`tests/ui/TestLayoutSensing.cpp:59-61`、`TestClear.cpp` 依赖三个日志按钮在 UIA 树中可见）。

#### 3.8.2 切换逻辑与刷新（I3）

```cpp
void MainFrame::onBottomViewSelected(wxCommandEvent& event) { switchBottomView(event.GetSelection()); }

void MainFrame::switchBottomView(int index) {
    wxWindow* pages[3] = {logPanel_, taskMonitorPanel_, batchQueuePanel_};
    for (int i = 0; i < 3; ++i) {
        if (!pages[i]) continue;
        pages[i]->Show(i == index);              // wxBoxSizer 的 CalcMin/DoLayout 跳过隐藏项
    }
    if (taskMonitorPanel_) taskMonitorPanel_->setActive(index == 1);
    if (batchQueuePanel_)  batchQueuePanel_->setActive(index == 2);
    bottomPanel_->Layout();
    if (index == 1 && taskMonitorPanel_) taskMonitorPanel_->refreshNow();
    if (index == 2 && batchQueuePanel_)  batchQueuePanel_->refreshNow();
    if (index != 0) bottomPanel_->FitInside();   // 保证页切换后子控件尺寸立即生效
}
```

- 绑定：`bottomViewRadio_->Bind(wxEVT_RADIOBOX, &MainFrame::onBottomViewSelected, this);`（单选控件天然互斥，无需 notebook 事件；`wxEVT_RADIOBOX` 由控件自身 handler 接收，绑定对象正确）。
- **单一 timer**：`MainFrame` 用 `bottomViewTimer_`（`Start(1000)`，ID `ID_BOTTOM_VIEW_TIMER`）在 `onBottomViewTimer`（`wxTimer*` + 带 ID 的 `Bind(wxEVT_TIMER, …, ID)`，见 §3.8.1 代码）内按当前选中页调用 `taskMonitorPanel_->refreshNow()` 或 `batchQueuePanel_->refreshNow()`；不可见面板不刷新（等价于 v1.1 的"仅活动页轮询"，但避免两个面板各自持 timer）。日志页不需要 timer（事件驱动）。
- 空闲/隐藏时 timer 不额外唤醒 UI：面板不可见 ⇒ 不调用任何 controller 接口。
- 三页常驻同一容器，切换零重建开销；`LogPanel` 的 `Logger::setLogCallback` 注册在构造期，切换不影响日志累积。

### 3.9 控件 ID（I4）

新增 `src/ui/UiControlIds.h`（生产代码唯一来源，杜绝撞号）。`MainFrame.cpp` 等生产代码用 `#include "UiControlIds.h"`（`src/ui` 在生产目标的 include 路径内）。**`tests/ui/UIIds.h` 不 include 本文件**（实施修正）：`UiControlIds.h` 依赖 `wxID_HIGHEST` 宏，而 UITests 铁律是不链接、不包含任何 wxWidgets 头（`CMakeLists.txt` UITests 目标仅 tests/ui include 路径 + Catch2/UIA 库）；UIA 断言一律按 Name/类名（§5.3），单选相关定位常量（`BottomViewRadioName` 等）直接以 `wchar_t*` 常量形式写死在 `tests/ui/UIIds.h`，并注释其生产端对应关系。

```cpp
enum UiControlIds {
    // 底部视图容器（避开 +100 = ID_MENU_IMPORT_SUB、+200 = ID_LOG_CLEAR/ID_TOOL_UPDATE_ALL、
    // +400 = ID_NETMON_TIMER、+500、+901、+1002 等已占用段位）
    ID_BOTTOM_VIEW_PANEL     = wxID_HIGHEST + 1200,
    ID_BOTTOM_VIEW_RADIO     = wxID_HIGHEST + 1201,
    ID_BOTTOM_VIEW_TIMER     = wxID_HIGHEST + 1202,
    // 任务监控面板
    ID_TASK_MONITOR_PANEL    = wxID_HIGHEST + 1300,
    ID_TASK_CURRENT_INFO     = wxID_HIGHEST + 1301,
    ID_TASK_CANCEL_CURRENT   = wxID_HIGHEST + 1302,
    ID_TASK_PENDING_LIST     = wxID_HIGHEST + 1303,
    ID_TASK_CANCEL_SELECTED  = wxID_HIGHEST + 1304,
    ID_TASK_CANCEL_ALL       = wxID_HIGHEST + 1305,
    // 测试队列面板
    ID_BATCH_QUEUE_PANEL     = wxID_HIGHEST + 1400,
    ID_BATCH_STATUS_TEXT     = wxID_HIGHEST + 1401,
    ID_BATCH_GAUGE           = wxID_HIGHEST + 1402,
    ID_BATCH_PERCENT_TEXT    = wxID_HIGHEST + 1403,
    ID_BATCH_QUEUE_DETAIL    = wxID_HIGHEST + 1404,
};
```

`TaskMonitorPanel` / `BatchQueuePanel` 构造时以 `ID_TASK_MONITOR_PANEL` / `ID_BATCH_QUEUE_PANEL` 作为自身窗口 ID，便于 UIA 侧按 Name 定位（见 §5 对定位器的说明）。

### 3.10 面板设计

#### TaskMonitorPanel（`src/ui/TaskMonitorPanel.h/.cpp`）

- 顶部（`ID_TASK_CURRENT_INFO`，多行 `wxStaticText`）：`当前任务：批量测试 (TEST_ALL)　状态：RUNNING　进度：120/1500 (8.0%)　已运行 34s　信息：<progressMessage>`；无任务时显示 `当前无运行任务`。
- 中部：`wxListCtrl`（`ID_TASK_PENDING_LIST`，report 视图，列：任务ID / 类型 / 优先级 / 状态 / 提交于）。数据来自 `controller_->getTaskQueueSnapshot().pending`，顺序即出队顺序。
- 按钮：`终止当前`（`ID_TASK_CANCEL_CURRENT`，无当前任务时禁用）→ `controller_->cancelTask(snap.current.taskId)`；`取消选中`（`ID_TASK_CANCEL_SELECTED`，无选中时禁用）→ `controller_->cancelTask(selectedId)`；`取消全部排队`（`ID_TASK_CANCEL_ALL`，`queueDepth == 0` 时禁用）→ `controller_->cancelAllQueuedTasks()`。
- 刷新：`refreshNow()` 读快照并更新控件；`setActive(bool)` 只控制是否需要刷新（timer 归 MainFrame）。回调内（`onComplete` 触发的状态栏更新）不得回调 TaskQueue。
- 状态栏反馈：取消排队 → `已取消排队任务: <type>`；终止当前 → `已取消当前任务: <type>`（走 `StatusUpdateEvent`，与现有 `AppController` 事件一致）。
- 无 `auto`；所有 controller 调用在 UI 线程。

#### BatchQueuePanel（`src/ui/BatchQueuePanel.h/.cpp`）

- `ID_BATCH_STATUS_TEXT`：`运行中` / `空闲` / `无运行中的批量测试`。
- `ID_BATCH_GAUGE`（`wxGauge`，range 1000 表达一位小数）+ `ID_BATCH_PERCENT_TEXT`：`"120/1500 (8.0%)"`。
- `ID_BATCH_QUEUE_DETAIL`：`主队列剩余 N　高优先级剩余 M　成功 S　失败 F`。
- 空闲显示（沿用 v1.1 I4）：仅当 `running == false && processed == 0` 显示"无运行中的批量测试"；`running == false && processed > 0`（批次刚结束）保留最后统计并在 2 次刷新后转空闲，避免闪烁。
- 数据来源：`controller_->getBatchQueueStats()`（非 const）。

### 3.11 编码与构建约束（I6）

- 新增代码**禁用 `auto` 类型推导**（AGENTS.md §1）；范围 `for` 写 `for (const task::TaskHandle& h : snap.pending)`。
- C++17：不使用 `std::atomic<std::shared_ptr>`、结构化绑定仅在明确类型处使用。
- CMake：`src/task/TaskType.cpp`、`src/task/TaskFormat.cpp` 追加到 `CORE_SOURCES`（`CMakeLists.txt:213-214` 同段）；`src/ui/TaskMonitorPanel.*`、`src/ui/BatchQueuePanel.*`、`src/ui/UiControlIds.h` 追加到 `UI_SOURCES`（`CMakeLists.txt:280-319`，紧邻 `src/ui/LogPanel.cpp:300`）。测试：新增 `add_executable(test_task_type tests/test_task_type.cpp src/task/TaskType.cpp)` + `add_test(NAME TaskTypeLabelTest ...)`（对齐 `CMakeLists.txt:498-501` 的 `test_task_queue` 写法）；`tests/test_task_queue.cpp` 已含 `src/task/TaskQueue.cpp` 无需改源列表。

---

## 4. 文件改动清单

| 文件 | 动作 | 说明 |
|---|---|---|
| `include/task/TaskType.h` | 修改 | `taskTypeToString` 声明 |
| `src/task/TaskType.cpp` | 新增 | 21 项映射 + `default` 兜底 |
| `include/task/TaskFormat.h` / `src/task/TaskFormat.cpp` | 新增 | `elapsedSecondsSince` 纯函数 |
| `include/task/TaskQueue.h` | 修改 | `TaskQueueSnapshot` + `snapshot()` + `cancelAllPending()` 声明 |
| `src/task/TaskQueue.cpp` | 修改 | `snapshot()`（锁内复制排空）；`cancel()` 回调出锁；`cancelAllPending()` |
| `include/ProxyBatchTester.h` | 修改 | `BatchQueueStats` + `BatchQueueStats getQueueStats();`（**非 const**） |
| `src/ProxyBatchTester.cpp` | 修改 | 实现 `getQueueStats()`（不嵌套加锁） |
| `include/AutoTaskManager.h` | 修改 | `PoolObserver` 改 `const std::shared_ptr<ProxyBatchTester>&` |
| `src/AutoTaskManager.cpp` | 修改 | `244-259`：栈 tester → `make_shared`；`run()` → `->run()` |
| `src/ui/AppController.h` | 修改 | `getTaskQueueSnapshot()`/`getBatchQueueStats()`/`cancelTask()`/`cancelAllQueuedTasks()`；`proxyBatchTester_` → `shared_ptr` + `std::mutex` |
| `src/ui/AppController.cpp` | 修改 | 13 处 `.load()/.store()` → lock+copy / assign / reset；3 处 `make_unique` → `make_shared`；`cancelTask` 分流；observer lambda 适配 |
| `src/ui/UiControlIds.h` | 新增 | 控件 ID 唯一来源（§3.9） |
| `src/ui/MainFrame.h` | 修改 | `bottomPanel_`/`bottomViewRadio_`/`taskMonitorPanel_`/`batchQueuePanel_`/`bottomViewTimer_`；`switchBottomView`/`onBottomViewSelected`/`OnTimer` |
| `src/ui/MainFrame.cpp` | 修改 | `initPanels()` 改共用窗口单选（§3.8.1）；单选绑定 + timer；`LogPanel` 去 `SetMinSize` |
| `src/ui/TaskMonitorPanel.h/.cpp` | 新增 | 任务监控面板 |
| `src/ui/BatchQueuePanel.h/.cpp` | 新增 | 测试队列监控面板 |
| `CMakeLists.txt` | 修改 | 注册 4 个新源文件 + `TaskTypeLabelTest` |
| `tests/test_task_queue.cpp` | 修改 | 新增快照/取消用例（§7 T1–T5） |
| `tests/test_task_type.cpp` | 新增 | 映射表全覆盖 + 兜底（§7 T6–T7） |
| `tests/ui/UIIds.h` | 修改 | 新增 `BottomViewRadioName = L"视图"`、`BottomViewLogOptionName = L"日志"`、`BottomViewTaskOptionName`、`BottomViewQueueOptionName` 及任务页按钮锚点常量（**不 include** `UiControlIds.h`，见 §3.9 实施修正：UITests 不含 wxWidgets 头，UIA 按 Name 定位）；更正"main frame has NO notebook tabs"注释（改为"底部为单选切换的共用视图区"） |
| `tests/ui/TestBottomViewSwitch.cpp` | 新增 | §5 的 UIA 用例（tag `[bottomview]`），并在 `CMakeLists.txt` 的 `UI_TEST_SOURCES` 与 `add_test` 注册 |
| `docs/INDEX.md` / `docs/plans/project-plans-tracker.md` | 修改 | 登记 spec v1.2 与评审 |

---

## 5. 验收标准

### 5.1 功能

1. 底部为**单选切换 + 共用区域**：`wxRadioBox`「视图」含且仅含"日志 / 任务 / 测试队列"三项，1 行 3 列，**默认选中"日志"**；任一时刻区域内只显示一个面板。
2. 切到"日志"时，级别过滤 / 清空日志窗口 / 日志统计 / 打开日志 四项功能与改造前完全一致；且**切到其它页再切回，历史日志不丢失**（页对象常驻，仅隐藏）。
3. "任务"页：空闲显示"当前无运行任务"；启动批量测试后显示"批量测试 (TEST_ALL)"、`RUNNING`、`进度 n/total`、`已运行 Ns`；排队任务按 priority desc → 提交时间 asc 列出，并显示"提交于 Ns 前"。
4. "任务"页取消：
   - `取消选中` 后该行消失，`getTaskQueueSnapshot().pending` 不再包含它，状态栏出现 `已取消排队任务: <type>`，且**UI 不挂起**（回调已出锁，验证方式：取消动作触发面板立即 `refreshNow()` 重入 `snapshot()`）。
   - `终止当前` 后 ≤2s 状态栏出现 `已取消当前任务: 批量测试 (TEST_ALL)`，`ProxyBatchTester` 在 ≤1 个代理测试周期内停止（判据：日志出现批次取消记录且 `processedCount` 不再增长），worker 正常返回后 `AppController::isRunning()` 转 false。
   - `取消全部排队` 一次清空所有排队项，每个各产生一次 `onComplete(CANCELLED)`。
5. "测试队列"页：批量测试进行中显示主队列剩余、高优先级剩余、已处理/总数、成功/失败与进度条；高优先级注入后其剩余数增加并被优先消化；`running == false && processed == 0` 显示"无运行中的批量测试"。
6. 底部区域随窗口缩放正常扩展；**隐藏的两个面板不占用高度**（`wxBoxSizer` 跳过隐藏项，已核对 wx 源码）。
7. 无 `auto` 类型推导；`cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel 8` 零错误零新增警告。

### 5.2 后端单测（`ctest`）

| # | 用例 | 断言 |
|---|---|---|
| T1 | `TaskQueueSnapshotTest.Ordering` | 先提交一个阻塞任务占住 worker，再以 priority 0/2/1 且不同提交时刻排入 3 个任务 ⇒ `snapshot().pending` 顺序为 `[p2, p1, p0]`，`queueDepth == 3` |
| T2 | `TaskQueueSnapshotTest.EmptySnapshot` | 全新队列：`current.taskId.empty()`、`pending.empty()`、`queueDepth == 0`、`busy == false` |
| T3 | `TaskQueueSnapshotTest.CancelRemovesFromPending` | `cancel(id)` 后 `pending` 不含该 id，`history(10)` 含 `status == CANCELLED` |
| T4 | `TaskQueueSnapshotTest.CancelCallbackIsReentrantSafe` | `onComplete` 回调内部调用 `snapshot()`；断言回调真的执行到（置标志位）且测试在超时前结束 ⇒ 证明回调不再持锁（修复前必然自死锁） |
| T5 | `TaskQueueSnapshotTest.CancelAllPending` | 排队 3 个后 `cancelAllPending()` ⇒ `pending.empty()`、3 次 `onComplete(CANCELLED)`、当前任务不受影响 |
| T6 | `TaskTypeLabelTest.CoversEveryEnumerator` | 遍历 21 个 `TaskType`，断言标签非空且包含该枚举名字符串 |
| T7 | `TaskTypeLabelTest.UnknownFallsBack` | `static_cast<TaskType>(9999)` ⇒ `"未知任务 (UNKNOWN)"` |
| T8 | `TaskFormatTest.ElapsedAndInvalidInput` | 合法计数 → 秒数正确（±1）；空串/非数字 → `-1` |

### 5.3 UI 自动化（外部 UIA 进程，`scripts/build-and-test.bat` 全绿）

新增 tag `[bottomview]`（`tests/ui/TestBottomViewSwitch.cpp`）：

- V1：主窗口存在名为「视图」的 radio/group 控件，且含 `日志`、`任务`、`测试队列` 三个选项（UIA `RadioButton`/`Tab` 类元素按 Name 匹配，常量写入 `tests/ui/UIIds.h`）。
- V2：启动后默认选中项为 `日志`——断言三个日志按钮（`清空日志窗口`/`日志统计`/`打开日志`）在 UIA 树中可见。
- V3：选中 `任务` 后，`清空日志窗口` **不可见**，且 `任务` 页容器可见（用 `findChildByText`/UIA 树两种手段之一，遵循 ui-automation-test "禁止猜 Name" 铁律：先 `[dumptree]` 实测再写常量）。
- V4：切回 `日志` 后三个日志按钮重新可见。
- V5：`resizeWindow(+240,+180)` 与 `ensureMinSize` 后，`清空日志窗口` 仍可见且位于窗口边界内（保护共用区域不被隐藏页撑坏）。
- V6：既有 `[layout]`/`[clear]` 套件不因本次改动变红（默认页=日志即成立）。

> **禁止**在 UI 测试中使用进程内 `FindWindowById`（`UITests.exe` 是独立进程，见 `CMakeLists.txt:1615-1622`）。控件 ID（§3.9）的作用是**应用内**稳定定位与 `SetName` 来源，UIA 断言一律按 Name/类名，且必须先跑一次 `[dumptree]` 实测再落常量。

---

## 6. 风险与缓解

| 风险 | 缓解 |
|---|---|
| `snapshot()` 访问 priority_queue 内部容器不可行（B1） | 复制 + 排空（与既有 `pendingTasks()` 同构，已编译验证该写法可行）；禁止锁内调用同类 getter |
| const getter 加锁编译失败（B2） | `getQueueStats()` / `getBatchQueueStats()` 非 const；Phase 1 首先编译验证 |
| 取消当前任务不生效（B3-a） | `cancelTask` 分流到 `cancelTest()`；§5.1-4 用日志 + `processedCount` 双重判据验收 |
| 取消回调重入死锁（B3-b） | 回调移出临界区；T4 用例直接证伪旧行为 |
| 自动任务路径 UAF 复发（B4） | AutoTaskManager 持有 shared_ptr；禁止 aliasing 空 deleter |
| 隐藏页撑高共用区域 | `wxBoxSizer::CalcMin/DoLayout` 跳过隐藏项（wx 源码 `sizer.cpp:2543/2560/2819` 已核对）；V5 缩放用例守护 |
| 切页后子控件尺寸未更新 | `switchBottomView` 内 `bottomPanel_->Layout()` + `FitInside()`；V3/V4 守护 |
| 日志按钮 UIA 可见性回归 | 默认页固定"日志"；V2 守护；`tests/ui/UIIds.h` 注释同步 |
| `LogPanel` 去 `SetMinSize` 后高度塌陷 | 最小尺寸上移到 `bottomPanel_`（620×260，与原值一致） |
| 500ms/1000ms 轮询开销 | 共用窗口形态下只有可见页刷新；仅读原子与两个 mutex，无 DB |
| worker 卡死导致 `isRunning()` 长期为 true | 既有已知行为（`AppController::cancelTest()` 注释 P3）：`forceRelease()` 只恢复 `busy_`，不回收卡死线程。列为已知限制，不在本期解决 |

---

## 7. 实施阶段

- **Phase 0（spec 收口）**：本 v1.2 评审通过。
- **Phase 1（后端，含编译门禁）**：`taskTypeToString` + `elapsedSecondsSince` + `TaskQueue::snapshot/cancelAllPending/cancel` 出锁 + `ProxyBatchTester::getQueueStats` + AutoTaskManager shared_ptr 迁移 + AppController 薄包装。
  验收：`cmake --build build --parallel 8` 零错误 + `ctest -R "TaskQueueTest|TaskTypeLabelTest|MutexPolicyTest"` 全绿（T1–T8）。
- **Phase 2（底部共用窗口）**：`bottomPanel_` + `wxRadioBox` + 三页 `Hide()/Show()` + 单一 timer；日志功能回归。
  验收：`ctest -R "UI_"` 全绿（重点 `UI_LAYOUTSENSING`、`UI_CLEAR`），手工确认缩放行为。
- **Phase 3（两个面板）**：`TaskMonitorPanel` + `BatchQueuePanel` + 取消三按钮 + 状态栏文案。
  验收：手工 + `[bottomview]` V1–V5。
- **Phase 4（测试与文档）**：补 `tests/ui/TestBottomViewSwitch.cpp` 与 CMake 注册、更新 tracker/INDEX、跑 `scripts/build-and-test.bat` 全绿。

## 8. 与现有方案的关系

- 复用 Scheme B（池内高优先级插入）与 `TaskQueue`/`MutexPolicy`，不改变互斥矩阵；`testSingleProxyAsync` / `testSelectedProxiesAsync` 的快路径注入逻辑不变（仅 `proxyBatchTester_` 读写方式改为 shared_ptr lock+copy）。
- 复用 `pendingTasks()` 已建立的"复制+排空"遍历范式（ProxyTestUnified Phase A 引入），本期 `snapshot()` 与其同构，不引入新容器。
- 与 **ProxyTestUnified Phase D** 分工：Phase D 负责把队列汇总**打印到日志/控制台**；本 spec 负责**可视化展示与交互**（共用视图区 + 手工终止）。两者共享同一数据源，不重复实现。

---


---

## 9. 增补（v1.2.1，2026-11-03 订阅测试纳入任务管理）

owner 反馈「新增订阅测试未纳入任务管理」→ 排查后确认不是"没入队"，而是两个独立缺口（详见
`docs/bugfix/2026-11-03-Bugfix-CancelFlagPoisoning-BetweenQueuedTasks-v1.0.md`）：

### 9.1 RC1 — 取消标志跨任务污染（行为修复）

`AppController::cancelRequested_` 为进程级单标志，原先只在**入队时刻（UI 线程）**复位，worker 侧任务体不重置。
结果：任务 A 被终止后，紧随其后的排队任务 B 读到残留 `true`，**零代理秒退**（2026-11-03 16:08 日志实证：
B 全程无 `cancelTest` 却 `exit: cancelled`）。

修法（沿用 `AUTO_TASK` 既有先例）：**14 个 `taskQueue_.submit` 任务体统一在 worker 侧任务启动后 `cancelRequested_ = false;`**
（AUTO_TASK 2 处原已有，本次补 12 处）。排队期间的取消意图由 `TaskQueue::cancel(taskId)`（移除队列项 + 出锁回调）承担。

守护：`tests/test_task_cancel_reset.cpp`（`TaskCancelResetGuardTest`）扫描 `AppController.cpp` 的每个
`taskQueue_.submit(` 任务体，要求其中出现 `cancelRequested_ = false;` —— 已用变异测试证实：删掉任一处即 FAIL。

### 9.2 RC2 — 队列只有计数，没有待测代理清单（可见性修复）

- `TaskQueue::submit` 增加统一入队日志：`TaskQueue: queued <类型标签> id=<taskId>`（INFO），
  覆盖全部 14 个调用点（此前 `testSubscriptionAsync` 等未传 `onQueued`，日志无痕）。
- `ProxyBatchTester` 新增：

```cpp
struct PendingProxy { std::string indexId, address, port, configType; bool inserted = false; };
std::vector<PendingProxy> getPendingProxies(std::size_t limit = 500);   // 非 const，UI 线程
```

  取用顺序与 worker 完全一致（`insertedQueue_` 优先 FIFO，再 `proxiesQueue_` FIFO，越界索引丢弃），
  投影逻辑抽为纯函数 `include/test/BatchPendingView.h :: test::drainPendingView()`，由 `tests/test_batch_pending.cpp`
  （5 例）单测。
  并发安全：`proxies_` 仅在 run 加载阶段被写，新增 `proxiesSealed_` 原子标志——run 的 4 个入口先置 false，
  `testProxiesMultiThreaded()` 填满主队列后置 true；`getPendingProxies()` 在 sealed=false 时返回空，
  绝不读取正在被替换的 `proxies_`。`insertedProxies_/insertedQueue_` 在 `insertedMutex_` 下整体拷贝，
  `proxiesQueue_` 在 `queueMutex_` 下拷贝（两锁不嵌套）。
- `AppController::getPendingBatchProxies(limit)`：shared_ptr 快照 + 转发（tester 为空时返回空）。
- `BatchQueuePanel` 新增 `ID_BATCH_PENDING_COUNT`（`待测代理（N）`）与 `ID_BATCH_PENDING_LIST`
  （**虚拟** `wxListCtrl`：代理 ID / 地址 / 类型 / 队列，`高优先级` 标记注入项），每秒刷新不重建行对象、无闪烁。

### 9.3 验收增补

| 项 | 方式 |
| :--- | :--- |
| 队列投影顺序/上限/越界 | `BatchPendingViewTest` 5 例 |
| 每个任务体都重置取消标志 | `TaskCancelResetGuardTest`（+ 变异测试证伪） |
| 测试队列页可见状态行 + 待测清单 | `[bottomview]` V7（UIA，原生 `SysListView32` 可见性断言） |
| 回归 | `build-and-test.bat` 13/13 全绿；后端 `TaskQueue/TaskType/TaskFormat/BatchPending/CancelReset/MutexPolicy/AutoTask/ProxyBatch*` 9/9 全绿 |


## 10. 增补（v1.2.2，2026-11-04 运行中任务并入列表）

owner 需求：「将正在执行任务纳入列表，状态为：running」。

`TaskMonitorPanel::refreshNow()` 的任务列表由"仅排队任务"改为**统一任务列表**：

- **首行 = 当前运行任务**（`snap.current`，非空时）：状态列显示 `RUNNING`，其余列（任务ID / 类型 / 目标 / 优先级 / 提交于）与排队行同构；顶部当前任务详情行保留（进度、已运行时长、进度消息仍在那行）。
- **其后 = 排队任务**（`snap.pending`，出队顺序，不重排），状态列仍为 `QUEUED`。
- `pendingIds_` 与列表行平行映射（首行为 current 的 taskId），故「取消选中」选中运行中行时经
  `AppController::cancelTask` 的 current 分支路由到 `cancelTest()`（与「终止当前」同语义），
  排队行仍走 `TaskQueue::cancel`（移除 + 出锁回调）；状态文案标签同步识别 current id。
- 按钮启用逻辑不变：`取消选中` 依赖行选中，`取消全部排队` 依赖 `queueDepth > 0`（不含运行中行）。

验收：`build-and-test.bat` 13/13 全绿（`[bottomview]` V3 等用例覆盖任务页可见性）；`auto` 红线 0 处。

## 11. 增补（v1.2.3，2026-11-04 列名/时间格式 + 队列页精简）

owner 需求：①任务列表「目标」列改为「名称」（显示订阅 remarks）；②「提交于」由相对秒数改为 `hh:mm:ss`；
③「测试队列」页去除"待测代理"清单。

1. **名称列**：`TaskMonitorPanel::refreshNow()` 经 `AppController::getSubscriptionRemarks(subId)`
   （`SubitemDAO::getAll` 查 remark）解析 targetId → 订阅显示名；每次刷新内以 map 缓存，同一 subId 只查一次
   SQLite（列表每秒刷新，不得逐行查库）。remarks 为空或订阅已删除时回退显示 subId 原值，无目标仍为 `-`。
2. **提交于 = 本地墙钟时刻**：新纯函数 `task::wallClockText(steadyStamp, steadyNow, wallNow)` ——
   `wall = wallNow - (steadyNow - stamp)` 后 `localtime + strftime("%H:%M:%S")`（沿用仓库既有 localtime 惯例，
   MinGW 下避免 `localtime_s` 可移植性问题；仅 UI 线程调用）。无效/空戳返回 `- -`。
   单测：`TaskFormatTest` 新增 3 例（映射正确性 / 零时长=当前墙钟 / 垃圾输入）。
3. **队列页去清单**：`BatchQueuePanel` 删除 `PendingProxyList` 虚拟列表与 `pendingCount_`，只保留状态行 +
   进度条 + 双队列计数行；`ID_BATCH_PENDING_COUNT/LIST` 在 `UiControlIds.h` 保留占位（不复用）。后端
   `getPendingProxies`/`getPendingBatchProxies` 及其 5 例单测保留（诊断/CLI 用途）。`[bottomview]` V7 相应收窄为
   仅断言状态行。

验收：`build-and-test.bat` 13/13 全绿；后端 `TaskFormat/TaskQueue/PendingVisibility/BatchPending` 4/4 全绿；新增代码 `auto` 0 处。

## 12. 增补（v1.2.4，2026-11-05 配置文案 + 自动任务子任务拆解）

owner 需求：①配置编辑窗口「去重」→「清洗」；②执行自动任务时，将子任务拆解后纳入任务监控。

### 12.1 配置文案（仅显示层）

`ConfigDialog.cpp` 四处「去重」文案改「清洗」：属性类别名、`dedup_after_update` 属性名（更新后清洗）、
自动任务步骤开关、任务链显示名 `stepDisplayName("dedup")`。属性键（`dedup_after_update` /
`autotask_step_dedup` / 步骤名 `dedup`）与 config.json schema 不变；工具栏早已用「清洗」（UIIds 断言不变）。

### 12.2 自动任务子任务拆解（Scheme A 首次落地）

数据流：`AutoTaskManager::writeState()`（每个持久化状态转换点）→ 新 `StepObserver` 回调（worker 线程，
非阻塞）→ `AppController` 把 `state_.steps` 映射为 `task::TaskHandle` 列表 → `TaskQueue::setChildTasks()`
整表替换（mutex 安全）→ `snapshot().children` → `TaskMonitorPanel` 渲染在 RUNNING 父行之下、排队行之上，
任务 ID 前缀 `└`，名称列直接用步骤中文名（全部更新/全部测试/清洗/同步/导出/解析地区，`targetId` 透传，
面板按 `parentTaskId` 非空跳过订阅 remark 查询）。

- 子行是**监控行非队列行**：不进 `queue_`，worker 永不执行，`hasPending()/pendingTasks()`（ProxyTestUnified
  注入门禁）与 `queueDepth` 均不受影响。
- 生命周期绑定父任务：`setChildTasks` 在 `current_` 为空时忽略；`workerLoop` 换任务/任务结束、
  `forceRelease` 都清空 children；父行 `childTotal/childCompleted/childFailed` 由列表自动汇总。
- 子行不可「取消选中」（取消单步=取消整个 AUTO_TASK，已由终止当前承担）：`rowIsChild_` 并行数组门控按钮。
- 提交于列：子行 `createdAt` 缺省继承父 `startedAt`（拆解与父同生命周期，列显示稳定）。
- 步骤→TaskType 标签映射（类型列）：UPDATE_ALL→UPDATE_ALL、TEST_ALL→TEST_ALL、DEDUP→DEDUP、
  SYNC→SYNC_DB、EXPORT→EXPORT_SHARE_LINKS、RESOLVE_REGION→RESOLVE_REGION。

单测：`PendingVisibilityTest` +2 例（children 绑定/计数/不入队/随父清理；无父任务时忽略）。
验收：`build-and-test.bat` 13/13 全绿；后端 `PendingVisibility/TaskQueue/AutoTask` 3/3 全绿；`auto` 红线 0 处。

---

## 13. 增补（v1.2.5，2026-11-05 批量更新拆解 + 监控组名/默认页）

owner 需求（2026-11-05）：①「更新全部订阅」也按订阅拆解进任务队列展示；②单选组名「视图」→「监控」；
③选项顺序改为 任务、队列(测试队列)、日志，默认页=任务。

### 13.1 批量更新订阅拆解（Scheme A 第二次落地）

数据流复用 §12.2 模式，观测点从 AutoTaskManager 移入 `SubitemUpdaterV2` 本体：

```
SubitemUpdaterV2::run()（worker 线程）
  ├─ 开始时：enabledSubs 每行建 PENDING 行；dedup_after_update&&dedup_enabled 时
  │          追加尾部「清洗」行（kDedupStepId="dedup" / kDedupStepName="清洗"）
  ├─ processSub 抓取前 → RUNNING；成功 → SUCCESS（粘滞，重试阶段不可降级）；
  │          404/解析失败/连接失败 → FAILED；间隔内跳过 → SKIPPED（新状态）
  ├─ 循环后清扫：仍 PENDING/RUNNING 的行（取消/断网未走到）→ CANCELLED，
  │          监控行永不冻结在 RUNNING；「清洗」行豁免清扫（它在下面运行）
  └─ 清洗：RUNNING → deduplicate() 返回值决定 SUCCESS/FAILED
       ↓ StepObserver（同步、非阻塞、mutex 外无共享状态）
AppController::doUpdateAllSubscriptions 注册回调
       ↓ UpdateStepInfo → task::TaskHandle（taskId="upd-"+subId；
         type=UPDATE_SUBSCRIPTION，清洗行=DEDUP；targetId=remarks 空则 subId）
TaskQueue::setChildTasks() → snapshot().children → 任务页 └ 子行渲染（复用 §12.2）
```

- `TaskStatus` 新增 `SKIPPED`（仅监控子行使用；面板文案 `SKIPPED`；不计入 childCompleted/childFailed）。
- 回调在 UPDATE_ALL 任务体运行期间发布（`current_` 非空），子行绑定父任务、随父清理，语义与 §12.2 完全一致。
- `runSingle`/`runSingleWithProxy`（单订阅路径）不发布拆解（父行 targetId 已是该订阅 remark）。

单测：`UpdateBreakdownTest`（`tests/test_update_breakdown.cpp`）2 例——
①首发全 PENDING + 尝试过的订阅 RUNNING→FAILED（黑洞端口）+ 间隔内订阅 SKIPPED + 清洗行 SUCCESS + remarks 随行；
②启动前取消 ⇒ 全部子行 CANCELLED 且无清洗行。
e2e（一次性探针，跑后已删）：UIA 树实测父行 `更新全部订阅 (UPDATE_ALL) RUNNING` + 子行
`└ upd-e2e-blackhole-1`(E2E黑洞一/RUNNING) / `└ upd-e2e-blackhole-2` / `└ upd-dedup`(清洗/PENDING)。

### 13.2 「视图」→「监控」

`MainFrame.cpp` radio caption 与 `tests/ui/UIIds.h::BottomViewRadioName` 同步改（`findInBottom`/
`findBottomOptions` 按组名作用域定位选项，两处不一致会使全部 `[bottomview]` 用例找不到选项）。

### 13.3 选项顺序与默认页

选项顺序=页索引（owner 心智模型）：**0=任务 1=测试队列 2=日志**，默认选中 0（任务）。
`switchBottomView()` 的 `pages[]`、`onBottomViewTimer` 索引、`setActive`/`refreshNow` 门控全部重映射；
`MainFrame.h::bottomViewIndex_` 注释同步。

**受默认页影响的 UI 测试**（原 spec D1 的「日志恒可见」前提被推翻，改为先切页再断言，不放宽断言）：
- 新增 `tests/ui/framework/BottomView.h` 共享助手：`findBottomOptions`/`clickBottomOption`/
  `waitBottomOptionSelected`/`waitLogButtons`/`showLogPage`（组名作用域 + 几何回退）。
- `UiElement::isSelected()` 新增（MinGW 无类型化 getter，走 `GetCurrentPropertyValue(UIA_SelectionItemIsSelectedPropertyId)`）。
- `TestBottomViewSwitch`：V1 改「监控」三选项；V2 默认页断言改任务（任务按钮可见 + 日志按钮不可见）；V3/V4 方向反转；V5 resize 前先 `showLogPage`；V7 不变。
- `TestClear` / `TestPopupGuard`(button) / `TestLayoutSensing`(3 例) / `TestBottomViewSwitch`(resize)：断言日志按钮前统一先 `showLogPage`，不可达时 INFO+skip（既有惯例）。
- `TestWindowSensing` 的「清空日志窗口」是纯字符串往返单测，不受影响。

验收：`build-and-test.bat` 13/13 全绿（UI_BOTTOMVIEW 按新顺序/默认页断言）；`UpdateBreakdownTest` 2/2；
`auto` 红线 0 处。

## 14. 增补（v1.2.6，2026-11-05 自动任务 update_all 订阅拆解 + 终态保留）

owner 反馈（2026-11-05）：「批量更新队列没有实现？」。取证（`bin/log/ui_20261105_*.log`）结论：
①直接入口「更新全部订阅」拆解（§13.1）已生效，但生产库仅 2 个启用订阅，任务约 6 秒完成——
当时的设计里任务一结束父行+子行立即从任务页消失，用户切换过去已无可看；
②15:02/15:03 两次触发实际走的是**自动任务**，其 `stepUpdateAll()` 自建 `SubitemUpdaterV2`
未接拆解观测，只显示步骤行「全部更新」——真实缺口。修复两点：

### 14.1 自动任务 update_all 步骤内订阅级拆解（三层行）

```
SubitemUpdaterV2(stepUpdateAll 内部)::StepObserver
  → AutoTaskManager::setUpdateStepObserver 转发（同 worker 线程，不存储快照）
  → AppController::doAutoTaskImpl：AutoBreakdownState{steps, updateSubs} 共享缓存
     stepObserver       填 steps（indentLevel 1）
     updateStepObserver 填 updateSubs（updateSubRowFromInfo，复用 §13.1 映射）
  → task::mergeAutoTaskChildren(steps, subs)   // include/task/TaskMonitorMerge.h 纯函数
  → TaskQueue::setChildTasks() → snapshot().children
```

- 三层渲染：AUTO_TASK 父行（无缩进）→ `└` 步骤行（indentLevel 1，两层前缀 `"  └ "`）→
  `    └` 订阅行（indentLevel 2，四层前缀，parentTaskId=步骤行 taskId）。
- 门控（merge 内）：订阅行**仅当 UPDATE_ALL 类型步骤 RUNNING** 时并入；步骤完成/取消后的
  迟到快照直接丢弃（陈旧尝试的子行不得挂到后续状态）。
- `TaskHandle` 新增 `indentLevel`（1=步骤/一级子行，2=步骤内订阅行）；渲染深度由
  merge 统一拥有，调用方设置值被覆盖。
- `AutoTaskManager.h` 引入 `SubitemUpdaterV2.h` 以类型化转发（UpdateStepObserver）。

单测：`MonitorMergeTest`（`tests/test_monitor_merge.cpp`）4 例——无 update 步骤仅步骤行 /
订阅行挂 RUNNING update 步骤（顺序、indentLevel、parentTaskId）/ 步骤 COMPLETED 后订阅行
丢弃 / 双空输入。

### 14.2 终态保留（recentParent/recentChildren）

`TaskQueueSnapshot` 新增 `recentParent`/`recentChildren`：**任务结束（COMPLETED/FAILED/
CANCELLED，worker 正常路径与 forceRelease 均覆盖）后父行+子行继续展示，直到下一个任务
START** 才清除（新任务出队时置空）。`children_` 随 current 清空的旧契约不变（
`PendingVisibility.ChildTasksAreBoundToCurrentAndClearedOnTaskEnd` 继续守护）。

- 面板（`TaskMonitorPanel::refreshNow`）：idle 且 recentParent 非空时，在「当前无运行任务」
  提示下渲染 recent 父行 + 子行；父行标 monitorOnly（`rowIsChild_` 泛化为「仅监控行」），
  终态行不可「取消选中」；队列非空时 recent 行照常展示（与 pending 行并存）。
- 取消路由不受影响：`updateButtonStates` 的「终止当前」门控仍只看 `snap.current`；
  `cancelTask` 对 recent 行 id 无路由（按钮已禁用，双保险）。

单测：`PendingVisibility.FinishedTaskStaysVisibleAsRecentUntilNextStart`——任务结束后
recentParent/recentChildren 可见且 children 已空；下一任务 START 后 recent 清空。

验收：全量 ctest 68 例仅 `XrayManagerStartupTest`（真实 xray 冷启动 7.7s>3s）/
`UI_FLOATINGWIDGET`（悬浮球回位动画时序断言，单跑 53 断言全绿）两个已知环境噪声；
`build-and-test.bat` 13/13 全绿。

## 15. 增补（v1.2.7，2026-11-06 更新订阅改为整体进度）

owner 需求（2026-11-06）：「执行更新订阅任务时，不拆解显示具体订阅名称列表；复用队列进度条显示整体更新进度」。
本条**回退** §13.1（直接 UPDATE_ALL 订阅拆解）与 §14.1（自动任务 update_all 步骤内订阅拆解）的展示层，
保留 §14.2 终态保留与自动任务**步骤级**拆解。

### 15.1 行为变更

- 直接「更新全部订阅」（`AppController::doUpdateAllSubscriptions`）：不再向任务监控发布
  `upd-*` 订阅子行；`SubitemUpdaterV2::setStepObserver` 仅作为进度数据源（PENDING/RUNNING/
  SUCCESS/FAILED/SKIPPED/CANCELLED）。
- 自动任务 `update_all` 步骤：`AutoTaskManager::setUpdateProgressObserver`（原
  `setUpdateStepObserver`）继续转发订阅级快照，但接收端只折算计数，不再合并成第三层行；
  步骤行（`auto-N-*`）与 §14.2 终态保留不变。
- 「队列进度条」= `BatchQueuePanel` 的 `wxGauge`（`ID_BATCH_GAUGE`）。无批量测试时
  `AppController::getBatchQueueStats()` 返回更新任务的整体进度（`BatchQueueStats::updating=true`）：
  状态文案「正在更新订阅」/「更新已结束」，明细行「剩余 x　成功 y　失败 z」；空闲文案
  `无运行中的批量测试`（`UIIds.h::BatchIdleStatusName`）保持不变。
- 任务监控页当前行的「进度：x/y」同步显示 `更新订阅 x/y`（`TaskQueue::setCurrentProgress`）。

### 15.2 数据通道

```text
SubitemUpdaterV2 step observer 快照
  → countUpdateProgress(total/done/ok/failed)          // AppController.cpp 静态归约
  → AppController::publishUpdateProgress()             // 原子计数 + taskQueue_.setCurrentProgress()
  → getBatchQueueStats()（无 tester 时）→ BatchQueuePanel::refreshNow()
```

- `TaskQueue::setCurrentProgress(int,int,const std::string&)`：仅写当前行（`current_.taskId`
  为空时忽略），不产生行、不触碰 `queue_`。
- `AppController` 新增原子：`updateProgressActive_/Total_/Done_/Ok_/Failed_`；任务体退出时
  `resetUpdateProgress()` 清零。自动任务步骤 observer 在**无 RUNNING 的 UPDATE_ALL 步骤**时
  同样清零，避免跨步骤残留。
- 完成瞬间 `BatchQueueStats.running=false`，面板按既有 `endingGrace_=2` 帧保留最终数字后
  回到空闲文案。

### 15.3 删除项

- `include/task/TaskMonitorMerge.h`、`src/task/TaskMonitorMerge.cpp`、
  `tests/test_monitor_merge.cpp` 与 `MonitorMergeTest` 注册（merge 契约已无消费者）。
- `TaskHandle::indentLevel`（渲染深度回到固定一级前缀 `"  └ "`）。
- `AppController` 内 `AutoBreakdownState` / `publishAutoBreakdown` / `updateSubRowFromInfo`。

### 15.4 「测试队列」页更名「任务进度」（owner 2026-11-06）

该页现已同时承载批量测试队列与更新任务整体进度，原名称不再贴切：

- `MainFrame.cpp` `wxRadioBox` 选项「测试队列」→「任务进度」（顺序与页索引不变：
  0=任务 / 1=任务进度 / 2=日志）；`MainFrame.h::bottomViewIndex_` 与
  `onBottomViewTimer`/`switchBottomView` 注释同步。
- UIA 定位常量 `tests/ui/UIIds.h::BottomViewQueueOptionName` → `L"任务进度"`
  （Name 精确匹配，组名「监控」不变）；`TestBottomViewSwitch` V1/V7 用例名与 INFO 文案同步。
- 面板内部状态文案（「正在更新订阅」/「运行中」/「已结束」/空闲「无运行中的批量测试」）
  **不改**，`BatchIdleStatusName` 门禁断言依赖它。

### 15.5 测试与验收

- 新增 `TaskQueueSnapshotTest.SetCurrentProgressPublishesOnRunningTaskOnly`（空闲忽略 +
  运行中发布 + 快照字段）。
- 保留 `PendingVisibility.*`（children 契约 + 终态保留）与自动任务步骤级行为。
- 全量 `ctest --test-dir build`：67 例中仅 `XrayManagerStartupTest`（真实 xray 冷启动超时）/
  `UI_FLOATINGWIDGET`（悬浮球回位动画时序断言，单跑全绿）两个已知环境噪声失败；
  `scripts/build-and-test.bat` 13/13 全绿。

*本 spec v1.2 修订依据 `docs/reviews/2026-11-03-Review-TaskQueueMonitorAndLogTabs-v1.2.md`（B1–B4 / I1–I6 / D1–D6）与 owner 需求变更（底部三视图改单选切换 + 共用窗口）。所有 wxWidgets 行为断言均已对本地源码核对（`src/common/sizer.cpp`、`src/common/nbkbase.cpp`、`src/msw/notebook.cpp`），所有 C++ 可行性结论均经 g++ 14.2.0 最小复现验证。*
