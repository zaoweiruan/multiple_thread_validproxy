# Spec — 互斥任务准入规范化 + 完成任务留存与右键管理（TaskQueueAdmissionAndFinishedHistory）

- **日期**: 2026-11-06
- **类型**: Spec (SDD)
- **模块**: TaskQueue / AppController / MainFrame / TaskMonitorPanel / SubscriptionPanel / ProxyListPanel
- **版本**: v1.2（定稿——owner 裁决已并入）
- **作者**: AI Assistant
- **状态**: ✅ implemented（v1.2 定稿后 U1–U8 全部落地，2026-11-10 验收；偏差回写见文末修订历史 v1.2-impl）
- **上游**: `docs/specs/2026-11-03-Spec-TaskQueueMonitorAndLogTabs-v1.2.md`（本 spec 是其 v1.2.8/v1.2.9 增补的技术依据）

**v1.2 变更**（owner 裁决，2026-11-06）：
1. **R-1 关闭**：排队任务执行前**不做** Yes/No 复核；
2. **R-3 关闭**：「清理全部完成记录」**不加**确认弹框；
3. **R-2 本期做，但勘察修正落点**：评审 F5 所称 `auto_task_config.json` **并不存在**——自动任务
   步骤配置在 `config.json`（已被 G1 保存门禁保护）。真实运行期整文件重写的是
   **`bin/worker/autotask_state.json`**（`AutoTaskManager::saveStateFile`，裸 `ofstream`
   截断写，崩溃即半文件；读方全走队列/进程串行，无并发读）。故 R-2 以 **U8 原子写**落地
   （`.tmp` + `std::filesystem::rename` 覆盖），非弹框门禁；`AppController::saveConfig`
   （`AppController.cpp:1318`）同为裸 ofstream，一并修复。

**v1.1 关键变更**（相对 v1.0）：
1. 「点击即入队」改为「**点击入快照、执行才生效**」（评审 blocker：入队时刻副作用会造成错类型执行/取消串扰）；
2. 新增 **关机/切库排空护栏**（评审 critical：带积压退出会卡死 5s 并访问已释放资源）；
3. G10（编辑订阅）/G11/G12（删除订阅/删除代理）**不放开**——门禁从 `isRunning()` 收紧为 `isBusy()`（v1.0 分类基于错误事实：编辑路径含同步 DB 写）；
4. `finished_` 取消，改由 `history_` 直接服务 UI（评审一致：双列表纯冗余）；
5. 右键事件改用 `wxEVT_LIST_ITEM_RIGHT_CLICK`（`wxEVT_LIST_CONTEXT_MENU` 不存在，v1.0 无法编译）；
6. 徽章改 `wxStaticText`（`SetString` 会整框重建、1s 周期销毁 UIA 元素）；
7. 砍掉软上限 50 / RowKind 枚举 / onStarted 回调 / G13 半成品入队。

---

## 1. 背景与目标

owner 需求（2026-11-06）：

1. **规范互斥任务管理**：除「保存配置」与「切库（切换数据库）」在队列有执行任务时**保留提示门禁**外，
   其它任务均可纳入队列排队，不再被「操作进行中，请等待完成后再试」弹框拒绝。
2. **完成任务留存**：执行完成的任务**不从列表清除**，只标识完成状态；列表提供**右键菜单**，可
   清理全部完成任务、终止正在执行的任务、取消排队的任务。

### 1.1 现状（勘察实证，2026-11-06，行号已四重独立核验）

**任务执行模型**：`TaskQueue` 是**单 worker 串行队列**（`src/task/TaskQueue.cpp` `workerLoop()`）。
「同一时刻只有一个任务在跑」由队列保证，UI 层 `isRunning()` 弹框拒绝把本可排队的操作直接丢弃。

**当前拒绝点**（`grep isRunning()`，13 处）：

| # | 位置 | 入口 | 现行为 | v1.1 目标 |
|---|------|------|--------|-----------|
| G1 | `MainFrame.cpp:1292` | 保存配置 | 拒绝弹框 | **保留门禁**（owner 明示，v1.2 缺口见 R-2） |
| G2 | `MainFrame.cpp:1306-1315` | 保存配置内切库（`db_switch_policy::decide`） | 拒绝弹框 | **保留门禁**（owner 明示） |
| G3 | `MainFrame.cpp:1470` | 菜单 自动任务 | 拒绝弹框 | 移除，入队（快照参数） |
| G4 | `MainFrame.cpp:1482` | 菜单 恢复自动任务 | 拒绝弹框 | 移除，入队 |
| G5 | `MainFrame.cpp:1539` | 工具栏 批量测试 | 拒绝弹框 | 移除，入队 |
| G6 | `ProxyListPanel.cpp:720` | 导出分享链接（**同步执行**） | 拒绝弹框 | **本次不动**（owner 决策：导出/导入保持同步） |
| G7 | `ProxyListPanel.cpp:753` | 右键 解析单代理地区 → `resolveSingleProxyRegionAsync` | 拒绝弹框 | **移除，入队反馈**（v1.0/v1.1 误判为 detach：实施勘察实测 `AppController.cpp:4078` 起 `resolveSingleProxyRegionAsync` **走 `taskQueue_.submit(TaskType::RESOLVE_SINGLE_REGION, …)`**，是标准队列任务，detach 说法不成立 → 归入 U4 去门禁） |
| G8 | `ProxyListPanel.cpp:781` | 右键 批量解析地区 → `resolveRegionsBatchAsync` | 拒绝弹框 | **移除，入队反馈**（同上：`AppController.cpp:3985` 亦为 `taskQueue_.submit`，非 detach） |
| G9 | `SubscriptionPanel.cpp:372` | 刷新订阅列表（`loadSubscriptions`，只读） | 拒绝弹框 | 移除（只读重载本不该被拦） |
| G10 | `SubscriptionPanel.cpp:395` | 编辑订阅 | 拒绝弹框 | **收紧为 `isBusy()`**（v1.0 称「纯 UI 模态」为事实错误：确认路径 `updateSubitem`/`deleteSubscription`（`SubscriptionPanel.cpp:633/649/658`）是同步 DB 写，且 `updateSubscrip(db_,…)`（`SubitemUpdaterV2.cpp:1224-1252`）先 DELETE 后 INSERT，与测试 worker 并发可**重建行、复活已删订阅**；owner 只豁免 G1/G2） |
| G11 | `SubscriptionPanel.cpp:417` | 删除订阅（同步 DB 写） | `isRunning()` 门禁 | **收紧为 `isBusy()`**（排队≠安全：删除在跑的任务之后执行，测试 worker 复活行 → 孤儿行；`DatabaseHelper` 无跨句柄互斥） |
| G12 | `SubscriptionPanel.cpp:476` | 删除订阅下代理（同步 DB 写） | `isRunning()` 门禁 | **收紧为 `isBusy()`**（同上） |
| G13 | `AppController.cpp:3856-3873` | `findProxyByIndexIdAsync`（CLI `-F` 镜像） | MutexPolicy REJECT + `isBusy()` 三重拒绝 | **本次不动**（UI 无入口、CLI 镜像本就无队列语义、`AUTO_RESUME` 分支 submit 的 handler 在 `onComplete` 丢弃返回值——入队会**静默丢恢复**。独立跟进项 §9-F2） |

**MutexPolicy 现状**：生产代码唯一调用点即 G13（评审四方一致确认）。G13 不动 ⇒ `decide()` 与
`MutexPolicyTest` **原样保留、零改动**（v1.0「降级为探针 + 注释」取消——改无消费者的死代码是纯风险）。

**完成任务现状**：`TaskQueue` 完成任务落入 `history_`（`historyLimit_=100`，`std::deque<TaskHandle>`，
含 `completedAtMs`），但 `snapshot()` 不暴露；面板仅有 `recentParent` 单条（下一任务 START 即清）。

**面板现状**：底部三按钮「终止当前 / 取消选中 / 取消全部排队」；`pendingList_` 无任何右键绑定；
`refreshNow()` 每秒 `DeleteAllItems()` 全量重建。

**退出链现状**（评审 critical 证据）：`MainFrame::onClose` → `cancelTest()`（只掐当前任务，清排队）
→ `~MainFrame` 等 current ≤5s，超时即 `delete controller_` → `~AppController` 体内
`XrayManager::release()`（`AppController.cpp:135`）、`sqlite3_close(db_)`（:144）→ **之后**才析构成员
`taskQueue_`。带积压时：worker 执行下一个排队任务 → 任务 lambda 首行 `ensureDatabaseInitialized()`
在 close 失败的句柄上**重新 fopen 已 close 的 .db**（无 DB close 世代计数）→ onComplete 投递到已销毁的
MainFrame handler → 队列永不排空 → 5s 超时强拆。

---

## 2. 需求 1：互斥任务准入规范化

### 2.1 设计原则

> **TaskQueue 是任务互斥的唯一裁决者**。任务类入口点击即入队；只有不能安全并行的操作保留门禁：
> (a) owner 明示的 保存配置/切库；(b) **同步 DB 写**（编辑订阅确认、删除订阅、删除代理——它们不走
> TaskQueue，与队列 worker 并发有实证的数据损坏路径）。

### 2.2 操作四分类

| 类别 | 判据 | 处理 | 映射 |
|------|------|------|------|
| **A 队列任务** | 走 `taskQueue_.submit` | 移除弹框门禁，点击即入队 | G3/G4/G5/G7/G8/G14(去重) |
| **B 只读 UI** | 只读、无 DB 写 | 移除门禁 | G9 |
| **C 保留门禁** | ①②owner 明示 ③同步 DB 写且与 worker 有实证冲突 | G1/G2 维持 `isRunning()`；G10/G11/G12 收紧为 `isBusy()` | G1/G2/G10/G11/G12 |
| **D 本次不动** | detach 线程 / 同步执行 / CLI 镜像 | 零改动 + 代码注释引用本 spec | G6/G13 |

### 2.3 核心机制：「点击入快照、执行才生效」

**问题**（评审 blocker，多路实证）：现有入口把**点击时刻**的副作用写进共享状态
（`isRunning_ = true`、`currentTaskType_`、lambda 捕获 `currentSubId`/`currentTaskType`）。
一旦任务只是排队，这些副作用发生在 CLICK 时而非执行时，导致：

- 排队 `UPDATE_ALL` → 当前 `TEST_ALL` 的取消按钮**误路由**到 `cancelUpdateSubscriptions()`
  （`AppController.cpp:889-893` 读 `currentTaskType_`）→ 停错任务，且 `cancelTest()`
  无条件 `cancelRequested_=true`（:895）**毒化排队任务的 worker 首步**（worker 侧复位要到
  取任务之后 `TaskQueue.cpp:241`）；
- 排队 `UPDATE_SUB(5)` 期间用户切换视图/编辑列表 → 执行时用**点击时刻的旧 subId**，
  完成回调写错行（`SubscriptionPanel.cpp:575-579` 按 subId 写任意行）；
- 两次 `updateSubscrip`（先 DELETE 后 INSERT，按 `info.SubID` 定位）被 `UPDATE_SUB(3)` 交错 →
  **复活已删订阅并复制给别的订阅**；
- `isRunning_` 恒真直到出队，「运行中」显示与终止按钮语义全错。

**方案**（快照式，改动最小）：

1. **入口副作用延迟到任务体**：`runUpdateSubscriptionsAsync` / `runUpdateSubscriptionAsync` /
   `runAutoTaskAsync` / `resumeAutoTaskAsync` / `testAllProxiesAsync` 重构为：
   - 入口只**拍快照**（勾选的 subId 列表、任务参数、`TestTarget`）并 `submit`；
   - `isRunning_ = true`、`currentTaskType_`、进度计数初始化、`cancelRequested_ = false`
     移入 worker 任务体（`TaskQueue.cpp:241` 已复位 `cancelRequested_`，出队即净化——
   `TaskCancelResetGuardTest` 的文本守护继续强制这行存在）；
   - `onStarted` 回调机制**不引入**（评审：快照后 UI 不再需要「执行开始」通知，按钮态由 1s
     轮询驱动，v1.2.2 已确立此模式）。
2. **取消路由改读队列权威状态**：`cancelTest()` 与 `isTestRunning()` 的 `currentTaskType_` 分支
   改读 `taskQueue_.currentTask().type`（TaskQueue 是单 worker 唯一真相）；`AppController.h:117`
   注释「仅用于 isRunning() 查询」相应更新。
3. **面板数据实时化**：
   - `SubscriptionPanel::updateSubscriptionItem(subId,…)`：subId 已不在当前列表 → 跳过（防错位写行）；
   - `ProxyListPanel::updateProxyItem(indexId,…)`：indexId 找不到 → 跳过；找到则按 indexId 匹配
     现有行（勾选态随行保留）；
   - `onTestTaskFinished`：任务结束时**重新读 DB**（`loadProxies()`）而非回写点击行号；
   - `onAutoTaskFinished` 的 `loadSubscriptions()+loadProxies()` 保持执行时刻读——已正确，
     补注释：「不得挪到 submit 之前」。
4. **执行前目标校验（防陈旧任务盲发）**：worker 任务体开头校验快照目标仍有效——
   `UPDATE_SUB(subId)`：`SubitemUpdaterV2::updateSubscrip` 开头查不到 `subinfo` 即
   `return -1`（`SubitemUpdaterV2.cpp:1215-1222`，现成护栏，复用）；`AUTO_TASK_RESUME`：
   状态槽为空则现有逻辑本就 no-op。校验失败 → 任务快速 FAILED（记日志），不算崩溃不算挂。

### 2.4 入队反馈（owner 决策：状态栏文本 + 队列徽章）

1. **状态栏 field0**：`已加入队列：更新全部订阅，前方还有 2 个任务`（N = `queueDepth`）。
   **新增 `MainFrame::setStatusNotice(const wxString&)`**：封装
   `statusTextProtected_ = true; setStatusText(0, msg);`（复用 v1.2 §10.3 保护位机制，
   `MainFrame.cpp:345/350/377/1660`），否则批量测试运行中 field0 每秒被
   `updateTestStatusField()` 覆盖，反馈必然消失；并复用既有 30s 自动恢复。
2. **徽章**：radio 旁独立 `wxStaticText m_queueBadge`（**不用** `wxRadioBox::SetString`——MSW
   实现销毁并重建整个控件，1s 周期 = 每 5 次轮询销毁一次 UIA 元素与键盘焦点；空/非空交替
   还会引起 sizer 布局抖动）。文本 `排队: N`（ASCII 数字防截断），`queueDepth==0` 时
   `Hide()`（隐藏控件不占 UIA 树，门禁零干扰）；在 `refreshNow()` 内**仅当 N 变化时**更新。
   徽章只算排队数，不含运行中与完成任务。
3. **不自动切页**（owner 已裁决，不再重开）。
4. **去重**：enqueue 提示与完成提示（`showTaskFinishedOnStatusBar`）都走 `setStatusNotice`，
   同一刷新周期内以 enqueue 优先（后写覆盖可接受，30s 后都恢复常态）。

### 2.5 各入口目标行为

```text
G3  onMenuAutoTask        → 去弹框 → 快照 → runAutoTaskAsync（体首：isRunning_=true,
                            currentTaskType_=AUTO_TASK, cancelRequested_=false,
                            statusText=AUTO_START, progress=0）→ setStatusNotice
G4  onMenuAutoTaskResume  → 同上（AUTO_RESUME）
G5  onToolTest            → 去弹框 → testAllProxiesAsync(target=点击时刻 targetsSnapshot)
G9  onRefreshSubscription → 去弹框（只读）
G10 onEditSubscription    → isRunning() 改 isBusy()，文案：「任务队列有任务（执行中或排队），
                            为避免数据竞争，保存订阅修改前请先清理队列」
G11 删除订阅              → isBusy()，同上文案
G12 删除订阅下代理        → isBusy()，同上文案
G14 去重(Dedup)           → onMenuDedup 裸 std::thread 改为 taskQueue_.submit（新类型
                            TaskType::DEDUP）。两表（profileitem/subitem）各库都读
                            Profileitem+Subitem（DeduplicationManager.cpp:186,365,385），
                            与测试/更新 worker 并发 = 删正在用的行/复活行；重路径
                            （updateSubscrip 先删后插）正是冲突源。门禁升级：isBusy()。
A 类 G7/G8               → ProxyListPanel 去弹框，点击即入队（实施修正：两者本就走
                            taskQueue_.submit，非 detach）
D 类 G6/G13              → 零改动 + 一行注释引用 §2.2-D
```

**submit 结构红线**：新/改任务体必须含 `cancelRequested_ = false;` 且保持 8 空格 `        }, `
收尾（`TaskCancelResetGuardTest` 按文本扫描，缺一处即 ctest RED）。

### 2.6 关机与切库排空护栏（评审 critical，纳入本期）

**根因**：worker 与资源销毁（`sqlite3_close`/`XrayManager::release`）之间没有排空阶段；
`cancelAllPending()` 的排空等待被阻塞在当前任务的 worker `join` 上（`TaskQueue.cpp:200-211`）。

**修复**（小改动，全部在既有对象内）：

1. `TaskQueue::stop()` **开头**（join 之前）先 `queue_.clear()`、`children_.clear()`，并对每个被
   丢弃的排队任务以 `CANCELLED` 终态触发 `onComplete`（与 `cancelAllPending` 相同的回调语义）。
   此后 worker 干完当前任务即空队列退出，`~TaskQueue` 的 join 有界。
2. `~AppController` 体首行调用 `taskQueue_.stop()`（析构体内成员仍完全存活，回调可安全访问
   `db_`），确保 `XrayManager::release()` 与 `sqlite3_close(db_)` 之前没有任何任务体再运行。
3. **切库路径**：G2 门禁从 `isRunning()` 收紧为 **`isBusy()`**——有排队任务时禁止切库
   （否则当前任务结束后，后续任务经 `ensureDatabaseInitialized()` 在新库继续跑，用户以为切了、
   一半任务在旧库语义上入的队）。保存配置仍 `isRunning()`（owner 明示，不收紧排队项）。
4. 验证：`tests/test_task_queue.cpp` 新增 `StopClearsQueueWithoutExecuting`（塞 3 个会置标志位的
   nop 任务 → stop() → join 返回且标志位仍 false、3 个 onComplete 收到 CANCELLED）。

### 2.7 自动任务状态文件与配置原子写（U8，owner R-2 落地）

**勘察结论**（2026-11-06，替代评审 F5 的文件名假设）：

- `auto_task_config.json` **不存在**。自动任务步骤/开关在 `config.json` 的 `auto_task` 段，
  保存路径已被 G1 门禁（`MainFrame.cpp:1292 isRunning()`）保护，无需重复设防。
- 运行期**每步都整文件重写**的是 `bin/worker/autotask_state.json`
  （`AutoTaskManager::saveStateFile`，`AutoTaskManager.cpp:502-544`）：裸
  `std::ofstream file(filePath)` 截断写 → 进程崩溃/断电即**半文件**，下次
  `resume`/CLI 启动 `loadStateFile` 解析失败（catch 后返回默认 state）→ **静默丢失断点续跑**。
- 读方（`resumeAutoTask` 任务体、`main_cli` 启动路径、`AutoTaskManager` 构造）全部经
  TaskQueue 串行或进程独占，**无并发读**——所以正确修复是**原子写**，弹框门禁无的放矢。

**U8 方案**（两文件同法，工具函数一处）：

1. `AutoTaskManager::saveStateFile`：序列化 → 写 `filePath + ".tmp"` → `flush`+`close` →
   `std::filesystem::rename(tmp, filePath)`（Windows 上 rename 目标存在时行为：先
   `fs::remove(filePath)` 再 rename，或 `std::filesystem::copy_file` + rename——**采用
   `MoveFileExW` 语义等价写法**：`fs::rename` 失败且目标存在时 `fs::remove` 后重试一次）。
   失败返回 false（现状契约不变，`test_autotask.cpp` `AutoTaskStatePersistenceTest.*` 4 例
   round-trip 必须继续全绿）。
2. `AppController::saveConfig`（`AppController.cpp:1318`）：同法改造（config.json 半文件会让
   下次启动整配置不可用，比状态文件更严重）。
3. 不做 fsync（Windows `FlushFileBuffers` 需句柄且收益边际——目标只是消除「截断后写一半被
   崩溃打断」窗口，tmp+rename 已消除）。

### 2.8 排队任务的用户体验诚实性（R-1 已裁决：不做复核）

`RESOLVE_REGION` 已剥离（D 类）。剩余 A 类（自动任务/更新/批量测试）都是**用户明确会等的批处理**，
且徽章+状态栏可见。评审提出「批量测试运行 40 分钟后陈旧任务盲发」——对 UPDATE_SUB 由 §2.3-4
执行前校验兜底（订阅已删→快速 FAILED）；对 TEST_ALL/AUTO 本身幂等只读为主，接受。
**owner 裁决（R-1）：不做执行前复核**，维持入队即执行序语义。

---

## 3. 需求 2：完成任务留存 + 右键菜单

### 3.1 后端：`history_` 直接服务 UI（v1.0 双列表方案取消）

评审一致：`finished_`+`clearFinished`+fallback 与 `history_` 几乎同构，纯冗余。**v1.1**：

- `TaskQueueSnapshot` 新增 `std::vector<TaskHandle> history;`（拷贝最新在前，上限 `historyLimit_`），
  由 UI 层过滤 `status != QUEUED && status != RUNNING`（`isTerminal()`，`TaskType.h:55-57` 已存在，
  不新增 `isFinished()`）。
- **`clearHistory()` 语义 = 全清**（`history_.clear()` + `historySeq_ = 0` + 返回条数）。
  **必须重置 seq**：`historySeq_` 单调递增，只清 vector 不清计数器 → 新一轮任务 seq 从 101 起 →
  `TaskQueueSnapshotTest.SequenceMonotonicallyIncreasing`（断言 `1,2,3`）与
  `HistoryRecordsEveryParentWithSeqAndTimestamp` RED。此耦合写入方法注释，防止后人只清 vector。
  `history()` 诊断接口保留不动（`TaskQueueSnapshotTest` 现有断言零改动即通过）。
- 单条清理 `removeFromHistory(taskId)`（deque `std::remove_if`，返回 bool）。
- **终态补漏**（评审 blocker）：`stop()` 丢弃排队任务（§2.6-1）与 `cancelAllPending()` 移除的排队
  任务，其 `onComplete` 回调负责写入 `history_`（status=CANCELLED）——否则「取消全部排队」的
  任务凭空消失，右键清理也无从谈起。落点：两处排空分支在锁外触发回调前 push。
- `cancel(const std::string&)`（`TaskQueue.h:69`）语义：QUEUED → 移除+回调；RUNNING →
  **no-op（连日志都没有）**。因此：`cancelTask(taskId)` 若命中当前任务必须走 `cancelTest()`
  既有路由（现状已如此，`AppController.cpp:959-963`）；对 RUNNING 行的右键菜单**只给「终止」**。
- **taskId 唯一性已保证**：`nextTaskId()` = 毫秒时间戳 + 单调 seq（`TaskQueue.cpp:33-38`），
  单条清理按 id 精确命中，无碰撞。
- `recentParent` 机制退役：`snapshot().recentParent` 字段**保留**但面板不再依赖；
  `PendingVisibility.FinishedTaskStaysVisibleAsRecentUntilNextStart` **迁移**为「完成任务出现在
  `history` 且面板渲染为完成行」——需求直接要求，属**契约演进非放宽**，commit message 注明。

### 3.2 面板渲染

`TaskMonitorPanel::refreshNow()` 行序：

```text
1. RUNNING 当前（snap.current）
2. 其 children（运行中自动任务）
3. QUEUED（snap.pending）
4. FINISHED（snap.history 中的终态项，最新在前）
```

- **行类型**：现有 `std::vector<bool> rowIsChild_` 扩为第二列 `std::vector<int> rowKind_`
  （0=current 1=child 2=pending 3=finished；不引入枚举/结构体，YAGNI——评审指出 v1.0 RowKind
  方案还暗含未声明的 `rowTaskId_` 平行数组）。
- **选择与滚动保持**（评审 blocker，200 行规模下为第一 UX 缺陷）：`refreshNow()` 每秒
  `DeleteAllItems()` 会静默销毁选中与滚动位置。改为：渲染前记录
  `GetFirstSelected()` 对应行的 taskId + `GetCountPerColumn()`/首个可见项（用
  `GetTopItem()`），渲染后按 taskId 恢复选中、`EnsureVisible` 恢复滚动。仅当行仍存在时恢复；
  被清理的行消失则清除按钮禁用。
- 完成行状态列直接用 `statusToString`（COMPLETED/FAILED/CANCELLED）。**FAILED 用醒目文本**
  `FAILED!`（wxListCtrl owner-draw 成本高，纯文本区分，评审 D5）。
- 空列表占位行：`（无任务）`，`rowKind_=1` 复用「不可操作」通道（评审 D2）。
- 列宽 `wxLIST_AUTOSIZE_USEHEADER`（评审 D11，防空表头截断）。
- 完成行**不进** `updateButtonStates` 的「取消选中」使能（终态无取消可言；清理走右键）。

### 3.3 右键菜单（`wxEVT_LIST_ITEM_RIGHT_CLICK`）

> v1.0 的 `wxEVT_LIST_CONTEXT_MENU` **不存在**（wxWidgets 全库 0 命中；本地源码实为 3.3.1a）。
> 行内命中直接用 `wxEVT_LIST_ITEM_RIGHT_CLICK`（`listbase.h:598`，handler 自带 `GetIndex()`，
> 无需 `SubItemHitTest`）；**空白区右键**用 `pendingList_->Bind(wxEVT_CONTEXT_MENU, …)`
> （`HitTest(wxPoint(-1,-1))` 返回 NULL = 空白）。

按 `rowKind_` 动态构建 `wxMenu`（每次 new，`wxMENU_TYPE_POP_UP`，`TrackPopupMenu` 后 delete）：

| 行类型 | 菜单项 | 动作 | 使能 |
|--------|--------|------|------|
| current | 终止当前任务 | `controller_->cancelTask(current.taskId)` → `cancelTest()` | 恒 |
| pending | 取消此排队任务 | `controller_->cancelTask(taskId)`（QUEUED 分支：移除+回调入历史） | 恒 |
| finished | 移除此记录 | `controller_->removeFinishedTask(taskId)` | 恒 |
| 任意 | 清理全部完成记录 | `controller_->clearFinishedTasks()` | 有终态行 |
| 空白区 | 取消全部排队 / 清理全部完成 | 现有 `cancelAllQueuedTasks()` / 上项 | 队列/历史非空 |
| child | （空白区同款两项） | — | 同上 |

- **动词**（评审 D7）：v1.0「清理」→ v1.1「**移除**此记录 / **清理**全部完成记录」；
  与 owner 原话「清理全部完成任务」对齐的是后者。
- **确认弹框：不加**（owner 裁决 R-3：单条幂等、全清是低价值可重做操作、1s 轮询可观察）。
- **菜单命令 ID**：`ID_TASK_CTX_CLEAR_ALL = wxID_HIGHEST + 1500`、`ID_TASK_CTX_CANCEL_ONE = +1501`、
  `ID_TASK_CTX_REMOVE_ONE = +1502`、`ID_TASK_CTX_CANCEL_ALL_QUEUED = +1503`（+1500 段确认空闲，
  现用到 +1406）。`pendingList_` 上 `Bind(wxEVT_MENU, …, ID)`。
- **动作后置刷新**：清理/取消成功后立即 `refreshNow()` + 状态栏 `setStatusNotice`（复用既有
  `wxCommandEventHandler onCancelAll` 模式）。

### 3.4 控制器 API（`AppController`）

```cpp
std::size_t clearFinishedTasks();            // → taskQueue_.clearHistory()，返回条数
bool removeFinishedTask(const std::string& taskId); // → removeFromHistory
```

取消路由**复用现状**：`cancelTask()` 已正确分流 current→`cancelTest()` / queued→queue.cancel()
（`AppController.cpp:953-968`），零改动。

### 3.5 底部按钮区

三按钮保留（UIA 门禁依赖其 Name）。「取消选中」仅对 pending 行使能（`rowKind_==2`）。

---

## 4. 交互状态与视觉

- 完成行：状态列文字区分（COMPLETED / FAILED! / CANCELLED），不 owner-draw。
- 徽章 `排队: N`：`wxStaticText`，N 变化才 SetLabel，0 时隐藏。
- 状态栏通知统一走 `setStatusNotice()`（保护位 + 30s 自动恢复）。
- 排序：当前→排队→完成（最新在前）；1s 轮询内恢复选中/滚动（§3.2）。

---

## 5. 实施单元与文件清单

| U | 内容 | 文件 | 依赖 |
|---|------|------|------|
| U1 | `history` 进 snapshot / `clearHistory()`(含 seq 归零) / `removeFromHistory` / 排空任务回调写 history / `stop()` 先清队列 | `include/task/TaskQueue.h` `src/task/TaskQueue.cpp` | — |
| U2 | `~AppController` 首行 `taskQueue_.stop()`；`cancelTest()`/`isTestRunning()` 改读 `currentTask().type`；`cancelProbeIfRunning` 改读队列类型；5 个 submit 方法入口快照化（副作用移任务体） | `src/ui/AppController.{h,cpp}` | U1 |
| U3 | `TaskType` 新增 `DEDUP`（`toEnum`/`toString`/`isExclusiveWorker`）；`onMenuDedup` 改 `submit`（快照 `dedupType`，体首复位，进度经 `setCurrentProgress`）；去重门禁 `isBusy()` | `include/task/TaskType.h` `src/ui/MainFrame.cpp` | U1,U2 |
| U4 | 门禁增删（G3/G4/G5/G7/G8/G9 删；G10/G11/G12 收紧 isBusy；切库 isBusy + `queueDepth>0` 拒绝；G6/G13 注释）+ `setStatusNotice` + 徽章 `queueBadge_`（`onBottomViewTimer` 驱动，N 变化才更新） | `src/ui/MainFrame.cpp` `src/ui/SubscriptionPanel.cpp` `src/ui/ProxyListPanel.cpp` `src/ui/UiControlIds.h` | U2,U3 |
| U5 | 面板：history 完成行 / rowKind_ / 选中滚动保持 / 右键菜单 / 空占位 / 按钮使能 | `src/ui/TaskMonitorPanel.{h,cpp}` | U1 |
| U6 | 面板数据实时化：`updateSubscriptionItem` subId 守卫 / `updateProxyItem` indexId 守卫 / `onTestTaskFinished` 重读 DB | `src/ui/SubscriptionPanel.cpp` `src/ui/ProxyListPanel.cpp` | — |
| U7 | 测试与门禁（§6） | `tests/**` | U1-U6,U8 |
| U8 | `autotask_state.json` 与 `config.json` 原子写（tmp+rename，§2.7） | `src/AutoTaskManager.cpp` `src/ui/AppController.cpp` | — |

红线：新代码禁 `auto`；submit 结构满足 `TaskCancelResetGuardTest` 文本模式（含 DEDUP 新增处）。

---

## 6. 测试与验收

**后端 ctest**（`tests/test_task_queue.cpp`）：
- `HistoryServedInSnapshotNewestFirst`：2 个 nop 完成 → `snapshot().history` 含 2 条，最新在前，终态正确。
- `ClearHistoryEmptiesAndResetsSeq`：3 任务 → `clearHistory()` → history 空且后续新任务 seq 从 1 重新计数。
- `RemoveFromHistoryRemovesOnlyTarget`、`StopClearsQueueWithoutExecuting`（§2.6-4）、
  `CancelledQueuedTasksLandInHistory`（cancelAllPending 后 history 含 CANCELLED 记录）。
- 既有：`TaskQueueSnapshotTest.*`、`PendingVisibility.*`（后者按 §3.1 迁移措辞）、
  `MutexPolicyTest.*`（零改动）、`TaskCancelResetGuardTest`（DEDUP 新 submit 纳入守护）。
- `test_task_queue.cpp` 若断言类型字符串全集，补 `DEDUP`。
- **前置修复**：`test_task_cancel_reset.cpp:185-191` `UpdateSingleSubPassesSubIdToProgress` 现 RED
  （v1.2.7 删 `countUpdateProgress` 所致，既有欠账，本期一并修复为断言 `publishUpdateProgress`）。
- **U8**：`AutoTaskStatePersistenceTest.*` 4 例（round-trip/missing/completed/cancelled）必须
  继续全绿；新增 `AtomicStateWriteLeavesNoTmpOnSuccess`（saveStateFile 成功后同目录无 `*.tmp`、
  目标文件可解析）；`saveConfig` 路径经既有 ConfigReader 回归（`ctest -R Config`）。

**UIA `[bottomview]`**：V1–V7 保持全绿；徽章用 `Hide()` 方案，V7 不受影响。
**V8（可达，非降级）**：批量测试完成 → 轮询列表出现 `COMPLETED` 行 → `postContextMenu`（SendMessage
保序，菜单为 `#32768` 模态循环，工具栏断言须排在菜单处理后）→ 点击菜单项 `清理全部完成记录` → 行消失。
既有教训沿用：`SetFocus` 先于 `postContextMenu`；禁用 `postMessage(WM_RBUTTONUP)`（PostMessage 会饿死模态循环）。
若 V8 在 CI 抖动，降级为「COMPLETED 行存在性断言」并在测试注释记录原因，不静默删断言。

> **实施对齐（2026-11-10）**：菜单项点击最终未用 `clickMenu`，改为「主线程轮询 `#32768` 弹窗 → UIA
> `InvokePattern` 命中 `TaskLedgerClearMenuItemName`」——原生菜单项不暴露为常规 UIA 子元素，
> `clickMenu` 的 menubar 路径不适用；`SendMessageW(WM_CONTEXTMENU)` 仍在 worker 线程阻塞至菜单循环退出，
> ESC（`keybd_event`）收尾替代 `PostMessage(WM_CANCELMODE)`。核心留存/清理断言不降级。

**全量**：`ctest --test-dir build` 仅 `XrayManagerStartupTest` 已知噪声；门禁
`scripts/build-and-test.bat` 14/14（新增 `UI_TASKLEDGER` 后由 13 升至 14）。

**手工验收（tracker 遗留，owner 确认）**：
- 批量测试运行中点「自动任务」→ 无弹框，状态栏「已加入队列…前方 N 个」，徽章 `排队: 1`；
- 完成 → 列表保留 COMPLETED 行且**不消失**；右键 → 清理全部完成记录 → 行消失、选中不丢；
- 运行中「编辑订阅」保存 → 被 `isBusy()` 拦（含排队场景）。

---

## 7. 风险与回滚

| 风险 | 缓解 |
|------|------|
| U2 快照化重构触碰 5 个 submit 方法 | `TaskCancelResetGuardTest` 文本守护 + 全量 ctest；分步提交（U1/U2 后端一笔、U4-U6 UI 一笔） |
| `stop()` 排空回调在析构期触发面板事件 | 回调只做队列内部 push + onComplete；MainFrame 侧 `~MainFrame` 先 `RemovePendingEvents`（既有）；单测覆盖 |
| 1s 轮询恢复选中的竞态 | 按 taskId 恢复（非 index）；行不存在则清 selection |
| UIA V8 模态菜单循环阻塞 | SendMessage 保序 + 断言后置；不可达则降级存在性断言（§6） |
| AUTO_RESUME 恢复语义在 `currentTaskType_` 迁移后丢失 | MainFrame 已有 `autoResumeTaskType_`（`MainFrame.cpp:1506-1508`）持有语义；`currentTaskType_` 降级为只读显示，`grep currentTaskType_` 全量清点后迁移 |
| Windows `fs::rename` 目标已存在时失败（EREADY/EPERM） | U8 实现：先写 `.tmp`，rename 失败且目标存在 → `fs::remove` 目标后重试一次；单测覆盖覆盖写路径 |
| G7/G8 用户感知「原来能弹框拒绝，现在点了就排队」 | 实施修正后二者归入 A 类（本就 `taskQueue_.submit`）；改为状态栏 + 徽章入队反馈，与 G3/G4/G5 一致 |

回滚：四笔提交（U1 / U2-U3 / U8 / U4-U6）各自可独立 revert。

---

## 8. owner 裁决结果（2026-11-06 全部关闭）

- **R-1**：~~陈旧排队任务执行前 Yes/No 复核~~ → **不做**（UPDATE_SUB 保留 FAILED 护栏，见 §2.3-4/§2.8）。
- **R-2**：运行中自动任务状态文件半文件问题 → **本期做**。勘察修正：所指文件实为
  `bin/worker/autotask_state.json`（非 `auto_task_config.json`，后者不存在；步骤配置在
  `config.json` 已受 G1 保护），读方无并发 → 落点为 **U8 原子写**（tmp+rename），非门禁。
- **R-3**：「清理全部完成记录」确认弹框 → **不加**（幂等低价值 + 1s 轮询可观察，维持 §3.3 默认）。
- **R-4**：（已裁决关闭）G10 编辑订阅确认路径含同步 DB 写且冲突面更大（先删后插复活订阅），
  v1.1 收紧为 `isBusy()`，不放开。

## 9. 范围外登记（不做，留跟进）

- F2 `findProxyByIndexIdAsync` 补全（submit + `onComplete` 消费返回值 + AUTO_RESUME 语义），
  与 `-F` CLI 镜像语义一并定夺（G13）。
- F6 后台 reader 连接 `db_reader_` 不随切库重开（`switchDatabase` 只换 `db_`）——G2 门禁收紧
  （isBusy）后触发面缩小，独立修。
- F7 `TaskQueue::cancel(runningId)` 静默 no-op——API 陷阱，加注释或改返回 bool。
- F8 detach 线程路径（G7/G8/订阅测试）纳入队列的完整改造。
- `historyLimit_` 100→200 调参（观察 1s 全表重绘性能后再定）。

---

## 10. 评审处置表（2026-11-06，5 persona，29 条去重发现）

| # | 发现（来源） | 处置 |
|---|--------------|------|
| 1 | 入队时刻副作用=错类型执行/取消串弹/毒化 flag（ADV1,FEAS2,DES1,IMP-1） | ✅ §2.3 快照化 |
| 2 | 退出/切库带积压 → 卡死 5s + UAF（ADV2 critical） | ✅ §2.6 排空护栏 |
| 3 | `wxEVT_LIST_CONTEXT_MENU` 不存在（FEAS1） | ✅ `wxEVT_LIST_ITEM_RIGHT_CLICK` |
| 4 | `SetString` 整框重建销毁 UIA（FEAS4） | ✅ `wxStaticText` 徽章 |
| 5 | G10 事实错误 + G11/G12 排队≠安全（SCOPE1,ADV3） | ✅ 收紧 isBusy（R-4） |
| 6 | recent/finished 双账本矛盾（COH1,FEAS3） | ✅ 单 `history_` 服务 UI，recent 退役 |
| 7 | cancel(running) 静默 no-op（FEAS9） | ✅ 菜单只给「终止」；F7 跟进 |
| 8 | clearFinished 不重置 seq 破坏测试（FEAS10） | ✅ `clearHistory` 含 seq 归零 |
| 9 | 1s 全量重建销毁 selection（DES4,D-7） | ✅ 按 taskId 恢复 |
| 10 | V8 可测不必降级（FEAS4） | ✅ 完整菜单路径，降级仅作兜底 |
| 11 | G7/G8「已入队」事实错误（FEAS2b） | ✅ 实施期反证并纠正：二者确为 `taskQueue_.submit`（非 detach），归入 A 类去门禁（见修订历史 v1.2-impl） |
| 12 | G13 半成品入队静默丢恢复（ADV8） | ✅ 移出本期（F2） |
| 13 | 软上限 50 违背 owner 裁决且无反馈通道（SCOPE5,FEAS6） | ✅ 砍掉 |
| 14 | RowKind/onStarted 过度设计（SCOPE8,DES-10） | ✅ int 数组 / 不引入 |
| 15 | §2.5 标题自相矛盾、16≠13 计数（COH1,COH2） | ✅ 修正 |
| 16 | DEDUP 双读表未声明（COH4） | ✅ §2.5-G14 两表+isBusy |
| 17 | `autoResumeTaskType_` 已持 RESUME 语义（COH5） | ✅ 风险表注记 |
| 18 | 「清理」动词含混（DES7） | ✅ 移除/清理分工 |
| 19 | FAILED 与 COMPLETED 无视觉区分（DES5） | ✅ `FAILED!` 文本 |
| 20 | 空列表无占位、列宽截断（DES2,DES11） | ✅ 占位行 + AUTOSIZE_USEHEADER |
| 21 | 状态栏 1s 被覆盖（DES3） | ✅ `setStatusNotice` 保护位 |
| 22 | 自动切页被重开（SCOPE6） | ✅ owner 裁决关闭 |
| 23 | RESOLVE 队列 40 分钟盲发（ADV5） | ✅ G7/G8 去门禁后由「入队反馈（状态栏+徽章）+ 右键取消排队」承接，不再有静默盲发 |
| 24 | 回调指针无失效机制（ADV4） | ✅ 既有 `RemovePendingEvents` 链覆盖；风险表注记 |
| 25 | 测试欠账 `UpdateSingleSub…` RED（FEAS8） | ✅ §6 前置修复 |
| 26 | R-2/R-4/确认框需 owner 裁决（SCOPE10,ADV-11,DES7） | ✅ §8 裁决点 |
| 27 | 关机其他未门禁路径（切库 G2 已收紧；退出=§2.6；其余 F5/F6 登记 §9） | ✅ |
| 28 | 面板回调写陈旧行（ADV9） | ✅ U6 实时化 |
| 29 | MutexPolicy 改注释零收益（SCOPE4） | ✅ 零改动 |

**评审确认稳健、未改动的设计**：owner 门禁 G1/G2 保留；D 类同步路径不动；完成行顺序最新在前；
不 owner-draw；`cancelTask` 路由复用；`history()` 诊断接口与 `MutexPolicyTest` 零改动。

---

## 修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-11-06 | 初始草案（互斥准入三分类 + finished_ 留存 + 右键菜单） |
| v1.1 | 2026-11-06 | 评审修订：快照式入队（blocker）、关机/切库排空护栏（critical）、G10 收紧、history_ 单列表、右键事件修正、徽章控件替换、砍软上限/RowKind/onStarted/G13 入队；7 实施单元；3 owner 裁决点；8 项范围外登记 |
| v1.2 | 2026-11-06 | **定稿（owner 裁决并入）**：R-1 不做复核、R-3 不加确认框、R-2 本期做——勘察修正落点为 `autotask_state.json`/`config.json` **原子写 U8**（`auto_task_config.json` 不存在，评审 F5 文件名有误；裸 ofstream 截断写才是真实风险）；§9 F5 关闭、风险表补 Windows rename 坑；实施单元 U1-U8 |
| v1.2-impl | 2026-11-10 | **实施完成后的偏差回写（不改需求，只纠正事实与实现细节）**：①G7/G8（`resolveSingleProxyRegionAsync`/`resolveRegionsBatchAsync`）由 D 类**改为 A 类**——实证二者走 `taskQueue_.submit`（`AppController.cpp:3985/4078`），"detach 路径"判断有误，随 U4 去门禁并入队反馈（§1.1 表、§2.2 四分类、§2.5 清单、§5 U4、§7 风险、§10 第 11/23 项同步修正）；②V8 菜单项点击改为 UIA `InvokePattern`（`clickMenu` 不适用于原生弹出菜单），见 §6 实施对齐注记；③门禁数 13/13→14/14（新增 `UI_TASKLEDGER`）；④徽章成员最终名 `queueBadge_`（独立 `wxStaticText`），刷新挂 `onBottomViewTimer` 且仅在队列深度变化时更新 |
