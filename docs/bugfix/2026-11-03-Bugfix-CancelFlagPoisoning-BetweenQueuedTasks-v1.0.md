---
title: "bugfix: 取消标志跨任务污染导致排队订阅测试秒退"
type: bugfix
status: fixed
date: 2026-11-03
updated: 2026-11-03
---

# 取消标志跨任务污染（queued task cancel-flag poisoning）

## 1. 症状

用户在订阅面板连续右键"测试订阅"多个订阅（或排队中点了"终止"后再轮到下一个），日志表现为：

```
16:08:36  [WARN] cancelTest() requested cancellation          ← 用户点终止（针对任务 A）
16:08:39  [Summary] Total: 1480 ... (exit: cancelled)          ← A 正常收尾
16:08:39  [Testing] 939 proxies total                          ← B（另一订阅）启动
16:08:42  [Summary] Total: 939 / Success: 0 / Failed: 0 / Remaining: 939 (exit: cancelled)
16:08:42  Batch test succeeded                                 ← 期间无任何 cancelTest WARN！
```

任务 B **一个代理都没测**（Failed: 0 / Remaining: 全量）就在 3 秒内以 `exit: cancelled` 中止，且全程没有用户取消动作。用户观感："订阅测试没有被任务管理接管"。

## 2. 根因（RCA）

`AppController::cancelRequested_` 是**进程级单一** `std::atomic<bool>`，批量测试（`externalCancel_`）、在线探测共用它；其复位点只在 **UI 线程入队时刻**（各 `*Async` 的 `cancelRequested_ = false`），**worker 侧任务启动时不重置**，也无 per-task 隔离。

时序：`入队A(false) → 入队B(false) → cancelTest(true) → A 观察 true 自行中止 → worker 出队 B → B 的 tester 读到的仍是 A 遗留的 true → 秒退`。

**仓库先例**：`AUTO_TASK` 任务体早已按"P2 模式"修复（`AppController.cpp:4110-4113`："reset cancel flag when the task actually starts running (on the worker), not just at submit time. A cancel requested while the task was queued must not poison the run."），但 `TEST_SUBSCRIPTION / TEST_ALL / TEST_SINGLE_PROXY / UPDATE_* / FIND_* / SYNC_DB / RESOLVE_*` 共 12 处任务体**漏加**，本次事故即 `TEST_SUBSCRIPTION` 命中。

次要可见性缺口（"看起来没被管理"的放大器）：
- `TaskQueue::submit` 无统一日志，`testSubscriptionAsync` 等 submit 又不带 `onQueued` → 日志无 `Task queued:` 痕迹；
- 批量队列只有计数（`batchQueueSize`），无待测代理成员清单可展示。

## 3. 修复

1. **P2 模式推广到全部任务体**：每个 `taskQueue_.submit` 的任务体在 `isRunning_ = true` 之后、执行前加 `cancelRequested_ = false;`（与 AUTO_TASK 同款注释锚点），共 12 处。排队期间的取消意图由 `taskQueue_.cancel(taskId)`（移除队列项）承担，不依赖共享标志。
2. **`TaskQueue::submit` 统一入队日志**：`TaskQueue: queued <类型标签> id=<taskId>`（INFO），14 个调用点一次覆盖；`test_task_queue` 目标按 `test_result_queue` 先例补链 `Logger.cpp LoggerInstance.cpp`。
3. **待测代理清单**（配套 spec v1.3 增补）：`ProxyBatchTester::getPendingProxies(limit)` 按 worker 取用顺序（高优先级先、各自 FIFO、越界防御）lock+copy 导出，`BatchQueuePanel` 列表展示。

## 4.1 标识可见性（追加，2026-11-04）

owner 反馈"进行两个不同订阅测试，任务、队列都没有标识"：`TaskHandle` 新增 `targetId` 字段（空=无单目标，
如 `TEST_ALL`/`UPDATE_ALL`），`TaskQueue::submit` 接受可选 `targetId` 参数，`testSubscriptionAsync`
将 `subId` 传入。`TaskMonitorPanel` 当前任务行显示 `[<subId>]`，排队列表新增"目标"列。`BatchQueuePanel`
的待测代理清单不变（代理级，非任务级）。

- 纯函数 `proxy::drainPendingView`（队列复制→导出）单测：`tests/test_batch_pending.cpp`（顺序/上限/越界/空队列/limit=0，5 例全绿）；
- **取消标志复位守护**：`tests/test_task_cancel_reset.cpp`（`TaskCancelResetGuardTest`）扫描 `AppController.cpp`
  每个 `taskQueue_.submit(` 任务体必须含 `cancelRequested_ = false;`。已用**变异测试**证伪：删除任一处 →
  `submit at AppController.cpp:359 — task body must set cancelRequested_ = false on the worker` FAIL；恢复 → PASS；
- 后端回归：`TaskQueueTest / TaskTypeLabelTest / TaskFormatTest / BatchPendingViewTest / TaskCancelResetGuardTest /
  MutexPolicyTest / AutoTaskTest / ProxyBatchComponentsTest / ProxyBatchQueryTest` **9/9 全绿**；
- UI 验收：`[bottomview]` V1–V7（含新增 V7「测试队列页可见状态行 + 待测代理清单」）6 用例 42 断言全绿；
  `scripts/build-and-test.bat` **13/13 全绿**；
- 手工复现步骤（交付说明，供 owner 复验）：连测 A、B 两订阅，中途终止 A ⇒ B 必须全量跑完，日志应有
  `TaskQueue: queued TEST_SUBSCRIPTION id=...`（两次）、B 的 `[Testing] n proxies total` 与**非 cancelled** 的
  `[Summary]`，且「测试队列」页可见 B 的待测代理逐条递减。


## 4.2 「两个订阅测试无排队任务、无标识」复验结论（追加，2026-11-04）

owner 再次反馈"任务列表只有第一次提交的任务、队列没有排队任务显示"。运行时取证结论：

1. **新版功能本身工作正常**（`bin\validproxy.exe`，Build 2026-11-04 10:49）：
   `bin\log\ui_20261104_105641.log` 含两条 `TaskQueue: queued 测试订阅 id=`（10:57:02 / 10:57:04），
   第一订阅正常跑完（`exit: normal`），第二订阅自动接力（65 代理，`exit: cancelled` 为用户手动终止）——
   队列、排队、接力全部正确。
2. **新增队列侧契约单测**：`tests/test_pending_visibility.cpp`（`PendingVisibilityTest`，ctest 全绿）——
   任务 A 执行期间提交 B/C，`snapshot()` 必须看到 A=current（targetId=`sub-AAA`）、B=pending
   （targetId=`sub-BBB`）、C=pending（targetId 空）。**队列侧无缺陷。**
3. **真正原因：owner 操作的是陈旧副本 `bin\worker\validproxy.exe`（2026-11-03 14:13 构建）**。
   二进制标记扫描证实该副本不含任何新功能标记（无「视图」radio、无「当前无运行任务」、无
   `TaskQueue: queued`、无「待测代理」），而它正是 11:16 起运行、11:28 仍有 `cancelTest()` 活动的进程
   （`bin\worker\log\ui_20261104_111636.log` 对应）。旧版没有任务/队列监控页与标识，症状完全吻合。
   **验证一律使用 `bin\validproxy.exe`**；`bin\worker\` 是运行时工作目录，其 exe 为陈旧手工副本。

处置建议：关闭正在运行的 worker 副本进程，改用 `bin\validproxy.exe` 复验；如希望杜绝此类混淆，可将
`bin\worker\validproxy.exe` 重命名为 `validproxy.exe.stale-20261103`（不影响运行时数据）。


## 4.3 更新订阅任务名称为空（追加，2026-11-04）

owner 反馈「更新订阅任务的名称为空」。根因：`TaskMonitorPanel` 的「名称」列由 `targetId` 经
`AppController::getSubscriptionRemarks` 解析，而 `updateSubscriptionAsync` 的 `taskQueue_.submit`
**漏传 `targetId` 第 5 参**（`testSubscriptionAsync` 有传，故测试订阅有名称、更新订阅为空，显示 `-`）。

修复：`UPDATE_SUBSCRIPTION` 提交补传 `subId` 作为 `targetId`；顺带修正同类的 `RESOLVE_REGION`
（`resolveRegionsBatchAsync` 按订阅解析区域，同样漏传）一并补 `subId`。无订阅语义的任务
（FIND_*/TEST_ALL/SYNC_DB/RESOLVE_SINGLE_REGION 等）保持空 `targetId`（显示 `-`），符合 spec v1.2.2。

验证：`build-and-test.bat` 13/13 全绿（UI_FLOATINGWIDGET 首轮偶发失败为既有环境计时噪声，单跑即绿，复跑全绿）；
新增代码 `auto` 0 处。
