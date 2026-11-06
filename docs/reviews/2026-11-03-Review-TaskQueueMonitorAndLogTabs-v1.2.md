# Review — 任务与队列监控面板 + 日志多标签页 (TaskQueueMonitorAndLogTabs) v1.2

- **日期**: 2026-11-03
- **被评审文档**: docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.1.md（382 行）
- **评审类型**: 第二轮独立评审（实施前门禁）— 代码事实核对 + 可编译性实测 + 取消链路验证 + UI 测试可行性
- **评审基线**: 分支 `tasks-and-test-queue-management`（HEAD `add852e`，与 `main` 同点）
- **评审结论**: **FAIL（需修订后重新评审）** — 4 blockers / 6 important / 6 consistency

> 本评审是 spec v1.1 的**实施前门禁评审**，与 `docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.1.md`（下文称"上一轮评审"）相互独立。
> 上一轮判定"0 blockers"；本轮通过**实际编译实验**与**调用链追踪**推翻了其中 B1 的结论，并新发现 3 条阻断项。修订后的 spec 建议升为 v1.2。

---

## 0. 检索基线（本次"新增任务及测试队列管理文档"命中的文档）

| 文档 | 角色 | 状态 |
|---|---|---|
| `docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.0.md` | 原始方案 | 🟡 已被 v1.1 取代 |
| `docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.0.md` | v1.0 评审（4 blockers） | 🟡 draft |
| `docs/specs/2026-10-30-Spec-TaskQueueMonitorAndLogTabs-v1.1.md` | **本轮评审对象**（最新方案） | 🟡 reviewed |
| `docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.1.md` | 上一轮评审（0 blockers） | 🟡 draft |
| `docs/specs/2026-10-27-Spec-BatchTaskQueue-Optimization-v1.0.md` | 上游：TaskQueue/Scheme B 的来源 | 🟢 implemented |
| `docs/specs/2026-10-29-Spec-ProxyTestUnified-v1.0.md` | 上游：`pendingTasks()/hasPending()` 与 Phase D 分工来源 | 🟡 reviewed |

代码侧现状核对：`TaskMonitorPanel` / `BatchQueuePanel` / `TaskType.cpp` / `TaskQueue::snapshot()` / `ProxyBatchTester::getQueueStats()` **均不存在**，即 spec 全量未实施（`src/task/`、`src/ui/`、`include/ProxyBatchTester.h` 均无对应符号）。

---

## 1. 结论摘要

| 编号 | 阻断项 | 性质 | 依据 |
|---|---|---|---|
| **B1** | §3.2 `snapshot()` 访问 `queue_.c` **不可编译** | 编译级 | `std::priority_queue::c` 是 protected，TaskQueue 非派生类；g++ 14.2.0 实测报错 |
| **B2** | §3.3 `getQueueStats() const` + §3.4 `getBatchQueueStats() const` **不可编译** | 编译级 | `queueMutex_/insertedMutex_/proxyBatchTesterMutex_` 均非 `mutable`；g++ 14.2.0 实测报错 |
| **B3** | "终止当前"按钮的取消链路**不成立**，且 `TaskQueue::cancel` 回调**持锁执行**（可自死锁） | 功能级 | `AppController::cancelTest()` 才置 `cancelRequested_`；`TaskQueue.cpp:77-99` 回调在 `mutex_` 内 |
| **B4** | `proxyBatchTester_` 改 shared_ptr 后 **AutoTaskManager 所有权未定义** | 设计级 | `AutoTaskManager.cpp:244-259` 的 tester 是栈对象，裸指针无法被 shared_ptr 安全接管 |

**判定**：B1/B2 使 Phase 1 第一行代码就无法编译；B3 使核心卖点（手工终止）实测无效；B4 使 B2（use-after-free）修复方案在自动任务路径上失效。**必须先修订 spec，不建议按现状进入 Phase 1。**

---

## 2. 阻断项详情

### B1. `queue_.c` 是 protected，成员函数同样无权访问（推翻上一轮 B1"已解决"）

spec §3.2 实现要点原文：

> `TaskQueue` 是 `snapshot()` 的成员函数，可访问 `queue_`（`std::priority_queue<...>`）的 protected 成员 `c`。

**该论断错误**。C++ 的 `protected` 访问权限仅对**派生类**的成员（及友元）开放；`TaskQueue` 与 `std::priority_queue` 无继承关系，故 `queue_.c` 在 `TaskQueue` 成员函数中同样非法。

实测（`g++ 14.2.0`, `-std=c++17`，最小复现）：

```text
error: 'std::vector<Entry> std::priority_queue<Entry, std::vector<Entry>, Cmp>::c'
       is protected within this context
note: declared protected here   →  stl_queue.h:537   _Sequence  c;
```

**修复方案（推荐 A）**：

- **A. 复制 + 排空**（与仓库现有 `TaskQueue::pendingTasks()` 完全同构，`src/task/TaskQueue.cpp:154-163`）：
  ```cpp
  std::priority_queue<Entry, std::vector<Entry>, EntryCompare> tmp = queue_;  // public copy ctor
  while (!tmp.empty()) { snap.pending.push_back(tmp.top().handle); tmp.pop(); }
  ```
  排空顺序天然等于 pop 顺序（priority desc → createdAt asc），**不需要 `std::sort`**，也绕开 `TaskHandle::createdAt` 是字符串的字典序陷阱。
- **B. 维护 `std::vector<TaskHandle> pendingMirror_`**（submit/cancel/workerLoop 三处同步维护），零 `std::function` 拷贝、O(1) 取快照；代价是新增不变量，需单测守护。
- **C.（禁止）** `#define protected public` / 继承 `std::priority_queue` 后 `using c`：前者污染全局，后者让 `queue_` 变成自定义类型，改动面反而更大。

**额外警告**：`snapshot()` **不可**在持有 `mutex_` 时调用 `pendingTasks()`——后者自身 `lock_guard` 同一把非递归 `std::mutex`，会**立即自死锁**。必须在 `snapshot()` 内部内联复制+排空（方案 A 的代码应整体放在同一把锁内）。

### B2. `const` 方法锁非 `mutable` 互斥量 → 编译失败

- spec §3.3：`BatchQueueStats getQueueStats() const;`，实现要持 `queueMutex_` / `insertedMutex_` 读 size。
- `include/ProxyBatchTester.h:124,132`：`std::mutex queueMutex_;` / `std::mutex insertedMutex_;` —— **均未声明 `mutable`**。
- spec §3.4：`BatchQueueStats getBatchQueueStats() const;`，实现要持 `proxyBatchTesterMutex_`；该成员在 spec §2.3 的声明同样未加 `mutable`。

实测（最小复现）：

```text
error: binding reference of type 'std::mutex&' to 'const std::mutex' discards qualifiers
```

**修复方案**：三选一并写进 §4 文件清单——

1. 去掉 `const`（`BatchQueueStats getQueueStats();` / `getBatchQueueStats();`）——调用方都是 UI 线程，无 const 需求，**改动最小，推荐**；
2. 或把三个 mutex 声明为 `mutable`（`mutable std::mutex queueMutex_;`），语义上"锁不改变对象可见状态"，也正确；
3. 或改用 `std::atomic<int>` 镜像队列深度（每次 push/pop 同步维护），彻底避免持锁。

### B3. 取消链路不成立 + 回调持锁（功能级阻断）

**(a) "终止当前"按 spec 写法不会停掉批量测试。**
spec §3.6 写"按钮调 `controller_->cancelTask(id)` 或 `cancelCurrent()`"。但：

- `TaskQueue::cancelCurrent()`（`src/task/TaskQueue.cpp:103-106`）只置 TaskQueue 内部 `cancelRequested_` / `currentCancelFlag_`；
- 真正让 `ProxyBatchTester` 停下的是 **`AppController::cancelRequested_`**（构造时作为 `externalCancel_` 传入，`AppController.cpp:713/779/3249`；tester 在 `src/ProxyBatchTester.cpp:640/661/689/893` 轮询它，并在 `:504` 传给 `ProxyTester`）；
- 该标志**只有** `AppController::cancelTest()` 会置位（`AppController.cpp:861`），且它额外调用 `taskQueue_.forceRelease()`（`:878`）恢复 `isBusy()`。

现有任务 lambda 一律 `(void)ctx;`（如 `AppController.cpp:660/691/710/778`），**没有任何任务检查 `TaskContext::isCancelled()`**。因此只调 `TaskQueue::cancel/cancelCurrent` 时：批量测试继续跑、`isBusy()` 持续 true、UI 显示"已取消"但实际未停。

**修复**：`AppController::cancelTask(const std::string& id)` 必须分流——`id == snapshot.current.taskId` → `cancelTest()`（完整链路）；否则 `taskQueue_.cancel(id)`。§3.6 与 §5 验收 #3 需按此改写。

**(b) `TaskQueue::cancel()` 在持锁状态下同步调用 `onComplete`，UI 回调重入即死锁。**
`src/task/TaskQueue.cpp:76-101`：`lock_guard` 在 `:77` 获取，`e.callbacks.onComplete(e.handle)` 在 `:94` 于锁内执行。若回调内做任何 `TaskQueue` 访问（`getTaskQueueSnapshot()` / `snapshot()` / `queueDepth()` / 再次 `cancel`）→ 同一线程重复锁 `std::mutex` → **死锁**（UB/挂起）。而 `TaskMonitorPanel` 的取消按钮极可能"取消后立即刷新"。

**修复**：把回调移出临界区（workerLoop 已是此模式，`TaskQueue.cpp:231-238`）：

```cpp
std::vector<task::TaskHandle> cancelled;
{
    std::lock_guard<std::mutex> lock(mutex_);
    // 重建队列；命中的 Entry 只 push 到 cancelled，不调用回调
}
for (const task::TaskHandle& h : cancelled) {
    if (cb.onComplete) cb.onComplete(h);   // 锁外
}
```

**(c) "取消全部排队"无 API。**
spec §3.6 列出该按钮，但 `TaskQueue` 无 `cancelAllPending()`。用循环调 `cancel(id)` 会 N 次重建优先级队列（每次 O(n log n)）且每次都持锁触发回调 → O(n² log n) + 重入风险。**需新增 `cancelAllPending()`**（一次持锁清空并收集，锁外批量回调），并补进 §4 文件清单与 §5 用例。

### B4. shared_ptr 迁移在 AutoTaskManager 路径上不闭合

spec §2.3 只写了"`AutoTaskManager::PoolObserver` 签名改为 shared_ptr（**或保留 raw + AppController 在 observer 内 lock+store**）"。后者不成立：

- `src/AutoTaskManager.cpp:244` `ProxyBatchTester tester(db_, cfg, baseDir_, cancelPtr, netMon_);` 是**栈对象**；
- `:247` `poolObserver_(&tester);` 传裸地址，`:253/:258` 传 `nullptr` 撤销；
- 把裸指针塞进 `std::shared_ptr<ProxyBatchTester>` 只能靠 aliasing + 空 deleter（`std::shared_ptr<T>(std::shared_ptr<void>(), ptr)`），**引用计数不控制生命周期**，栈对象出作用域后 UI 侧 shared_ptr 副本变悬垂 → B2 想消灭的 UAF 原样复活（且更隐蔽）。

**修复（必须写死，不留"或"）**：

1. `AutoTaskManager::stepTestAll()` 改为 `std::shared_ptr<ProxyBatchTester> tester = std::make_shared<ProxyBatchTester>(...)`，`tester->run()` 期间由该局部 shared_ptr 保活；
2. `using PoolObserver = std::function<void(const std::shared_ptr<ProxyBatchTester>&)>;`，`:253/:258` 传 `nullptr`（构造空的 shared_ptr 参数）；
3. AppController 的 observer lambda 内 `{ lock; proxyBatchTester_ = tester; }`；
4. §4 文件清单补 `src/AutoTaskManager.cpp`（当前只列了 `.h`）。

---

## 3. 重要问题（不阻断，但需在 v1.2 明确）

### I1. 时间戳字段无法显示为"提交时间 / 开始时间"

`TaskHandle::createdAt` / `startedAt` / `completedAt` 是 **`steady_clock` 计数值的字符串**（`include/task/TaskType.h:47-49`；写入见 `TaskQueue.cpp:41-42/187-188/227-228`）。`steady_clock` 的 epoch 是开机时刻，**无法还原墙上时间**。spec §3.6 要求列"提交时间"、§3.6/§5 要求显示"开始时间"，但未定义格式。

**建议**：UI 层显示相对量——`elapsedSec = (steady_clock::now().time_since_epoch().count() - std::stoll(handle.startedAt)) / freq`，渲染为"已运行 12s / 提交于 5s 前"；或在 `submit()` 时额外写入 `system_clock` 的 ISO 字符串（新增字段，需同步 §2.1 接口表）。

### I2. `snapshot.running` 语义是"worker 线程存活"，恒为 true

`TaskQueue::running_` 在构造后由 `workerLoop` 首行置 true（`TaskQueue.cpp:166`），仅在析构时 false。仓库已有前车之鉴（tracker 2026-10-28 记录："`TaskQueue::isRunning()` 反映 worker 存活而非任务执行中，新增 `isBusy()` 修正")。spec §3.2 同时暴露 `running`/`busy` 却不说明差异，面板极易误用。

**建议**：§3.2 注明 `running = worker 线程存活（调试用）`、`busy = 有任务在执行（UI 判据）`；UI 空闲判定统一用 `busy == false && current.taskId.empty()`；或直接从快照中移除 `running`。

### I3. `wxEVT_NOTEBOOK_PAGE_CHANGED` 不会送达 page，timer 门控绑定对象错误

spec §3.6/§3.7 写"面板监听 `wxEVT_NOTEBOOK_PAGE_CHANGED`，激活时 Start、离开时 Stop"。但 wxWidgets 把该事件投递给 **notebook 自身**的 handler：

```cpp
// wxWidgets src/common/nbkbase.cpp:191-198
void wxNotebookBase::SendPageChangedEvent(int nPageOld, int nPageNew) {
    wxBookCtrlEvent event(wxEVT_NOTEBOOK_PAGE_CHANGED, GetId());
    ...
    GetEventHandler()->ProcessEvent(event);   // ← notebook，不是 page
}
```

事件不会向下传播到子窗口，`TaskMonitorPanel` 内 `Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, ...)` **永不触发**（timer 永不启动或永不停止，直接导致验收 #8 失败）。

**修复**：在 `MainFrame::initPanels()` 里 `bottomNotebook_->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &MainFrame::onBottomPageChanged, this)`，再按 `event.GetSelection()` 转发给对应面板的 `setActive(bool)`；或面板构造时 `GetParent()->Bind(...)`。**不要**写在面板自身的 Bind 里。

### I4. 控件 ID 与既有 ID 撞号，`FindWindowById` 会歧义

spec §3.6/§3.7 用 `wxID_HIGHEST + 100` / `+ 200` 作面板 ID，与现有 ID 直接冲突：

| spec 提议 | 冲突对象 | 位置 |
|---|---|---|
| `wxID_HIGHEST + 100`（`ID_TASK_MONITOR_PANEL`） | `ID_MENU_IMPORT_SUB` | `src/ui/MainFrame.cpp:42` |
| `wxID_HIGHEST + 200`（`ID_BATCH_QUEUE_PANEL`） | `ID_LOG_CLEAR`（`LogPanel.h:18`）、`ID_TOOL_UPDATE_ALL`（`MainFrame.cpp:57`） | — |

同进程内 ID 重复对事件分发通常无碍（按窗口路由），但 `FindWindowById` 是**全树按 ID 搜索**，会命中错误的控件——恰好是 spec §5 T1 想用的断言方式。

**建议**：改用互不重叠的段位（如 `wxID_HIGHEST + 1200` / `+ 1300`），并把枚举集中到 `tests/ui/UIIds.h` 同源的单一头文件，避免后续再撞。

### I5. UI 测试方案与现有 UITests 架构不匹配；且会打破既有 `[layout]` 门禁

- **T1 不可实现**：`FindWindowById(ID_TASK_MONITOR_PANEL)` 是**进程内 wx API**；`UITests.exe` 是**独立进程的 UIA 自动化**（`CMakeLists.txt:1615-1622`，链接 `uiautomationcore`），无法调用。T1 必须改写为 UIA 定位器（如按 tab 名 "任务"/"测试队列" 或控件 Name）。
- **T2/T3 需新增定位器**：tab 数量/标题、页面切换，需在 `tests/ui/UIIds.h` 增补 notebook 的 Name/Class 常量（该文件现有注释明确写着"main frame has NO notebook tabs"，见 `tests/ui/UIIds.h:4-7,17-22`，本次改动将使其失效，需同步更新注释与常量）。
- **既有门禁会被打破**：`tests/ui/TestLayoutSensing.cpp:59-61` 用 `expectVisible` 断言三个日志按钮（"清空日志窗口"/"日志统计"/"打开日志"）在 UIA 树中可见。改为 notebook 后只有**当前激活页**的子控件进入 UIA 树——默认激活页**必须**是"日志"（`AddPage` 的第一个），否则 `UI_LAYOUTSENSING` / `UI_CLEAR` 立即变红。§5 需显式写死"默认页 = 日志"。

### I6. `taskTypeToString` 覆盖度与项目编码约束

- `TaskType` 共 **20 个枚举 + `NONE`**（`include/task/TaskType.h:8-29`），spec 只给了 `TEST_ALL` 一个示例，未给映射表，也未要求"全枚举覆盖 + 未知值兜底"——漏一个就会在面板上显示空串。建议 §3.1 附完整映射表，并要求 `default` 返回 `"未知 (<数值>)"`。
- spec §3.2 代码片段使用 `const auto& e : entries`，**违反 AGENTS.md "禁止 `auto` 类型推导"** 的红线（仓库既有 `test_task_queue.cpp` 的历史 `auto` 不在本次范围，但新增代码必须无 `auto`）。v1.2 需逐段扫除 `auto`。

---

## 4. 文档一致性（D）

| # | 问题 | 位置 | 修正 |
|---|---|---|---|
| D1 | 风险表仍写"`TaskQueue::cancel(taskId)` … 回调 **onRejected**"，与 §3.6 的 onComplete 自相矛盾 | §6 表第 5 行（原文 363 行） | 改为 onComplete（status=CANCELLED） |
| D2 | "AppController.cpp 共 **11 处**"调用点，且把**尚不存在**的 `getBatchQueueStats` 列为现有调用点 | §2.3 | 实测 **13 处**：`doTestAllProxies` 4（672/721/729/732）、`testSelectedProxiesAsync` 4（748/789/797/800）、`doTestSubscription` 4（3258/3260/3293/3299）、`runAutoTask/resumeAutoTask` 1（4122） |
| D3 | §8 末两段重复表述"与 ProxyTestUnified Phase D 分工" | §8（377、378 行） | 删除重复段 |
| D4 | 未定义任何**后端单测**；验收仅"ctest 全量通过" | §4/§5 | 补 `TaskQueueSnapshotTest`（排序/空队列/取消后快照）与 `BatchQueueStatsTest`（total=0、钳制、双队列计数）到 `tests/test_task_queue.cpp` / 新增 `tests/test_proxy_batch_stats.cpp`，并在 §4 列出 |
| D5 | §4 文件清单缺 `src/AutoTaskManager.cpp`、`include/ProxyBatchTester.h` 的 `mutable` 变更、`tests/` 与 `tests/ui/UIIds.h` | §4 | 补全 |
| D6 | 验收 #3 "UI 在 ≤6s 内恢复空闲" 无依据（`forceRelease()` 与 worker 卡死场景未定义） | §5 #3 | 改为可测：点"终止当前"后 ≤2s 状态栏出现"已取消当前任务: <type>"，且 `busy==false`（正常路径）；卡死路径单列为已知限制 |

---

## 5. 代码事实核对表（本轮实测）

| 断言 | 实测结果 | 证据 |
|---|---|---|
| `TaskQueue::pendingTasks()` 已存在且顺序正确 | ✅ 存在，复制队列后排空 = pop 顺序 | `TaskQueue.cpp:154-163` |
| `snapshot()` 可访问 `queue_.c` | ❌ **不可编译**（protected） | `stl_queue.h:537` + g++ 14.2.0 实测 |
| `getQueueStats() const` 可持 `queueMutex_` | ❌ **不可编译**（非 mutable） | `ProxyBatchTester.h:124/132` + g++ 实测 |
| `TaskQueue::cancel` 排队任务回调 onComplete | ✅ 正确 | `TaskQueue.cpp:92-94` |
| 回调在锁内执行 | ⚠️ **是**（可重入死锁） | `TaskQueue.cpp:77` 锁 / `:94` 回调 |
| 任务 lambda 检查 `ctx.isCancelled()` | ❌ 全部 `(void)ctx;` | `AppController.cpp:660/691/710/778` |
| 批量取消真正依赖 `AppController::cancelRequested_` | ✅ | `ProxyBatchTester.cpp:504/640/661/689/893`、`AppController.cpp:861` |
| `TaskHandle` 时间戳是 steady_clock 字符串 | ✅ | `TaskType.h:47-49`、`TaskQueue.cpp:41/187/227` |
| `proxyBatchTester_` 为 `std::atomic<ProxyBatchTester*>` | ✅ | `AppController.h:312` |
| 调用点 13 处（非 11） | ✅ | 见 D2 |
| `make_unique<ProxyBatchTester>` 4 处 | ✅ 3 处 AppController（713/779/3249）+ 1 处 AutoTaskManager（244） | — |
| AutoTaskManager tester 为栈对象 | ✅ | `AutoTaskManager.cpp:244-259` |
| `wxEVT_NOTEBOOK_PAGE_CHANGED` 投递给 notebook | ✅ | wxWidgets `src/common/nbkbase.cpp:197` |
| UITests 为外部 UIA 进程 | ✅ | `CMakeLists.txt:1615-1622` |
| `[layout]` 依赖日志按钮 UIA 可见 | ✅ | `tests/ui/TestLayoutSensing.cpp:59-61` |
| LogPanel 目前挂在 centerPanel 下（非 AUI pane） | ✅ | `MainFrame.cpp:900-910` |
| `ID_*` 撞号 | ✅ 两处 | 见 I4 |

---

## 6. 对上一轮评审（v1.1）结论的更正

| 上一轮判定 | 本轮更正 |
|---|---|
| B1「成员函数内部访问 `queue_.c`」✅ RESOLVED | ❌ **不成立**，protected 访问规则不允许；须改为复制+排空（已有 `pendingTasks()` 可复用） |
| B2「shared_ptr 方案已闭合」✅ RESOLVED | ⚠️ **部分成立**：AppController 侧策略正确，但 AutoTaskManager 所有权缺失（B4），"保留 raw + lock+store"分支是错的 |
| "0 blockers" | ❌ 实为 **4 blockers**（B1/B2 编译级、B3 功能级、B4 设计级） |
| 技术可行性 8.5 / 综合 8.3 | 下调为 技术 7.0 / 综合 **6.8**（方案方向对，但实现细节未经编译验证） |

方法论建议（沿用本仓库 2026-10-20 评审的教训）：**spec 中的关键代码片段必须先落地编译（哪怕最小复现）再写进文档**，否则"看起来对"的写法会把阻断项推迟到实施阶段。

---

## 7. 风险评估

| 风险 | 等级 | 缓解 |
|---|---|---|
| B1/B2 编译阻断 | 高 | 按 §2 修复后，Phase 1 第一件事就是 `cmake --build build --parallel 8` 验证 |
| B3 取消无效（用户以为停了其实在跑） | 高 | `cancelTask` 分流到 `cancelTest()`；补 UI 测试：点终止后 `busy==false` |
| B3 回调持锁死锁 | 高 | 回调移出临界区（改 `TaskQueue::cancel`），并加单测：回调内调 `snapshot()` 不挂 |
| B4 自动任务路径 UAF 复发 | 中高 | AutoTaskManager 持有 shared_ptr，删掉"保留 raw"备选 |
| notebook 改动打破 `UI_LAYOUTSENSING`/`UI_CLEAR` | 中 | 默认页固定"日志"；同步更新 `UIIds.h` 注释与常量 |
| 500ms 轮询开销 | 低 | 面板只读内存/原子；仅在激活页跑 timer |
| `[layout]` 对控件几何的假设 | 低 | 面板 MinSize 与 centerSizer 比例保持；切换后回归 `build-and-test.bat` |

---

## 8. 评分

| 维度 | v1.0 | v1.1（上一轮） | 本轮（v1.2） | 说明 |
|---|---|---|---|---|
| 技术可行性 | 7.5 | 8.5 | **7.0** | 方案方向正确，但 2 处核心实现不可编译 |
| 可实施性 | 6.5 | 8.0 | **6.0** | 取消链路与 AutoTask 所有权需重写；无后端单测定义 |
| 文档完整性 | 7.0 | 8.5 | **7.5** | 结构清晰，但 §6 与 §3.6 矛盾、调用点数字错误 |
| 验收可测性 | 6.5 | 8.0 | **6.5** | T1 不可实现、默认页未约束、≤6s 无依据 |
| **综合** | 7.0 | 8.3 | **6.8 / 10** | **FAIL — 需修订后重新评审** |

---

## 9. 修订清单（v1.2 可直接执行）

**阻断项**
1. §3.2 删除 `queue_.c` 方案，改为"持锁内联复制+排空"（或镜像 vector），并注明**不得在锁内调用 `pendingTasks()`**。
2. §3.3/§3.4 去掉 `getQueueStats()`/`getBatchQueueStats()` 的 `const`（或把三个 mutex 声明为 `mutable`），并写进 §4 清单。
3. §3.6 重写取消语义：
   - `cancelTask(id)`：`id == current.taskId` → `AppController::cancelTest()`；否则 `taskQueue_.cancel(id)`；
   - 新增 `TaskQueue::cancelAllPending()`；
   - 明确 `TaskQueue::cancel` 的回调**移出锁外**（附 `TaskQueue.cpp` 修改点）。
4. §2.3/§4 把 AutoTaskManager 改为持有 `std::shared_ptr<ProxyBatchTester>`，删除"保留 raw + lock+store"备选，文件清单补 `src/AutoTaskManager.cpp`。

**重要问题**
5. 定义时间显示格式（相对时间）或新增 `system_clock` 字段。
6. 注明 `snapshot.running`（worker 存活）与 `busy`（任务执行）语义差异，UI 用 `busy`。
7. timer 门控改为在 `MainFrame` 绑定 `wxEVT_NOTEBOOK_PAGE_CHANGED` 后转发（附 wx 源码依据）。
8. 控件 ID 改到 `+1200/+1300` 段，并与 `tests/ui/UIIds.h` 同源。
9. UI 测试用例改写为 UIA 可执行形式（T1 删除 `FindWindowById`），补"默认页=日志"约束与 `UIIds.h` 更新项。
10. §3.1 补全 20+1 个 `TaskType` 的中文映射表与 `default` 兜底；全文清除 `auto`。

**一致性**
11. §6 风险表 onRejected → onComplete。
12. §2.3 调用点数字 11 → 13，并修正函数分布（4/4/4/1）。
13. 删除 §8 重复段。
14. §4 补后端单测文件与用例；§5 #3 改为可测判据。

---

## 10. 结论

- 本 spec 的**目标与分层设计正确**（复用 TaskQueue/Scheme B、只做可视化、不动互斥矩阵），不需要推翻重来。
- 但当前 v1.1 的**实现细节未经编译验证**，且取消链路与自动任务所有权两处存在功能性缺陷。**建议：按 §9 修订为 v1.2 → 重新评审 → 再进入 Phase 1**。
- 修订完成后，Phase 1 的第一条验收建议固定为："`cmake --build build --parallel 8` 零错误 + `TaskQueueSnapshotTest`/`BatchQueueStatsTest` 绿"，以此作为编译级阻断项的回归闸门。

### 10.1 v1.2 修订后状态（2026-11-03 追加）

- §9 十四条修订清单已全部落入 `docs/specs/2026-11-03-Spec-TaskQueueMonitorAndLogTabs-v1.2.md`（单选共用窗口取代 notebook；B1-B4 修法逐条采纳）。作者自判 **APPROVE WITH CHANGES**，遗留 3 处文档级口径（FitInside 仅非日志页、timer 统一 `wxTimer*`+带 ID 绑定+`onBottomViewTimer` 命名、`UiControlIds.h` 在 `tests/ui` 与生产代码中的 include 路径差异）已于实施前在 spec 内修正完毕。
- 计划派发的两个后台复评 Agent（对抗视角 A / 一致性视角 B）均因模型网关不可用而终止，未产出独立复评结论；本轮以作者自判 + §9 清单逐项核销 + 实测编译/测试证据替代。**风险敞口**：v1.2 未经第二双眼睛复核——但 Phase 1 的实际实施以"可编译 + 测试红→绿 + 变异测试复现旧死锁"为闸门，编译级风险已由实证覆盖。
- **Phase 1（后端）实施验证记录**：`cmake --build build --parallel 8` 0 error；`TaskQueueSnapshotTest` 5/5、`TaskTypeLabelTest` 3/3、`TaskFormatTest` 3/3、`MutexPolicyTest`/`AutoTaskTest` 无回归；全量 ctest 61/62，唯一失败 `XrayManagerStartupTest.RealXrayStartsBounded`（真实 xray 冷启动 6.1s > 3s 阈值）经 `git stash` 干净树复测同样失败（6.125s，两次 6123/6126ms 高度一致），判定为本机环境计时噪声、与本改动无关（该测试链接输入不含任何被改文件，ninja 判定无需重建）。死锁回归的 RED 有效性经变异测试证实：临时把 `onComplete` 移回持锁区后 `CancelCallbackMayReenterQueueSafely` 21.8s 超时 FAILED，恢复后 PASSED。
- 实施中还发现并修复一个 spec 未覆盖的既有竞态：`workerLoop` 首行 `running_.store(true)` 可能在 `~TaskQueue` 已置 false 后复活该标志导致 join 永久挂起（新增空快照测试在本机 100% 复现挂死）；修法为构造线程启动前发布 `running_`、workerLoop 不再写（`src/task/TaskQueue.cpp`）。

---

*本评审为 spec v1.1 的第二轮独立评审（实施前门禁）。上一轮评审 `docs/reviews/2026-10-30-Review-TaskQueueMonitorAndLogTabs-v1.1.md` 的结论已在 §6 逐条更正。所有"不可编译"结论均由 g++ 14.2.0（w64devkit, `-std=c++17`）最小复现实证，非静态推断。*
