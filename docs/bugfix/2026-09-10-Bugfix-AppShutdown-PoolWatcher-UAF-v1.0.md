---
title: "Bugfix: 关闭程序时代理池评估线程与进程退出监视线程 UAF 崩溃"
type: bugfix
status: completed
date: 2026-09-10
version: v1.0
module: AppController / ProcessExitListener / StandaloneProxyPool
---

# 关闭程序时崩溃（统一代理池与悬浮窗监控后）

## 1. Summary（概述）

统一代理池与悬浮窗监控变更（未提交工作树，ebd025b "添加悬浮框、弹窗、代理池功能" + 89b4c9a 之上）引入两条关闭期释放顺序缺陷，导致「启动池 → 关闭程序」场景在启动后 ~5 秒稳定崩溃（c0000005，Attempt to execute non-executable address）：

1. `~AppController` 从不调用 `stopProxyPool()`——代理池评估线程（evaluatorLoop）在 AppController 与 MainFrame 释放之后继续运行，其 `onMembersChanged` 回调捕获了垂死的 `this` 与悬垂 `topWindow_`，持续 `wxQueueEvent` 投递事件到正在析构的 MainFrame。
2. `ProcessExitListener::shutdown()` 对监视线程 **detach 后立即释放 Watcher 对象**——分离线程在 freed Watcher 上继续循环调用 `heartbeatFn`/`notifyFn`（释放后的 std::function）并对 freed `topWindow_` 投递事件，构成「执行不可执行地址」的直接来源（c0000005 param 8 = execute violation）。

## 2. Problem Frame（问题定位）

### 2.1 崩溃现场（bin/temp/ crash dumps 2026-09-10 10:32-10:33）

- 异常：c0000005 AV，Parameter[0]=8（DEP execute violation），RIP=0x2123e245530（堆地址，非模块）
- 崩溃线程：主线程（thread 16436），uptime 5s，8 threads/71 modules，libcurl/sqlite/xray 模块未加载（crash dump 时点早于其加载）
- 崩溃栈：wxAppConsoleBase::MainLoop → ProcessIdle → **DeletePendingObjects+0x97** → (虚拟调用悬垂对象 → RIP 跳堆垃圾) → DEP AV
- stack 残留 validproxy+0xb4aa2/0xb4c1c = MainFrame::startMonitoring 内联区（CallAfter(startMonitoring) 之后的过期帧数据，非调用链）
- cdb 可用调用：`& "C:\Program Files\Windows Kits\10\Debuggers\x64\cdb.exe" -z <dump> -y <本地bin目录> -c ".symopt+0x40; .exr; k; q" -logo <log>`（本地 -y 避开 srv* 符号服务器挂死）

### 2.2 复现链（日志取证 bin/log/ui_20260910_103305.log）

1. 启动 → onFirstShow → startMonitoring（启动悬浮窗 + 5s proxyMonTimer）
2. UI 测试 `TestStandaloneProxyPool.cpp` TEST_CASE 3（[pooldialog][add]）BM_CLICK「启动池」→ 真实 xray 池启动（socks 11009/api 11010，sandbox config）
3. ~3 秒后关闭程序（cancelTest + onClose）
4. ~MainFrame → delete controller_ → ~AppController（缺陷窗口开启）
5. 下一 idle DeletePendingObjects 删除 MainFrame → 虚拟调用跳到堆垃圾 → AV

### 2.3 缺陷一：~AppController 不停池（新增代理池引入）

`~AppController`（src/ui/AppController.cpp:69-105）顺序：netMon_.Stop() → shutdownRequested_ → exitListener_->shutdown()（detach+clear！）→ delete exitListener_ → workerThread_ join/detach → XrayManager::release()。**全程无 stopProxyPool()**。代理池 `proxyPool_`（shared_ptr 成员）在析构函数体之后才释放，其 evaluatorLoop 在整个 ~AppController 窗口内持续 `notifyChanged()` → `onMembersChanged` lambda（捕获 `this`）→ `if (topWindow_) wxQueueEvent(topWindow_, new PoolMembersUpdatedEvent)`——投递到正在析构的 MainFrame（已从 wxPendingDelete 出队、进入析构体），事件写入垂死 wxEvtHandler 队列 = 堆践踏。

### 2.4 缺陷二：ProcessExitListener::shutdown() detach 后释放（既有缺陷，池场景放大）

`shutdown()`（src/ProcessExitListener.cpp:83-91）：
```
shutdownRequested_=true; lock; for each watcher: running=false; thread.detach(); watchers_.clear()
```
分离线程循环体（:38-70）在 `WaitForSingleObject(w->processHandle, intervalMs)` 超时后 `if (!w->running.load()) break` + `w->heartbeatFn(...)`——全部是对已 clear() 释放的 Watcher 的 UAF。**detach 与 clear 之间没有任何同步**，线程可能正在执行 `heartbeatFn`（捕获垂死 AppController `this` 的 std::function）中被释放。

### 2.5 为什么是 5 秒

proxy_process_monitor.check_interval_ms = 5000（生产与 sandbox config 一致）。第一个 proxyMonTimer tick 在 t≈5s，与 close 竞速；崩溃在 close 后第一个 idle。存活 run（10:33:05 等）= 池未启动的 run，池启动 run（10:33:07）= 崩溃 run。

## 3. Requirements（修复需求）

R1. 关闭程序时代理池必须先于 exitListener/controller 释放被停止，评估线程 join 后才允许任何后续释放。
R2. ProcessExitListener::shutdown() 不得 detach 后释放 Watcher；必须先 join 全部监视线程，再释放 Watcher 及其资源。
R3. 修复不改变运行时行为（启动池、监控、心跳、通知语义不变）；仅收紧关闭期顺序。
R4. 遵守项目规范：C++17、禁用 `auto`、PowerShell 命令、TDD（回归测试先行）。

## 4. Scope Boundaries（范围边界）

- 不改：悬浮窗、统一监控列表、池启动/停止业务语义、菜单/事件绑定。
- 不解决（记录遗留）：(a) `takeoverFn` 捕获局部 `&configFileNameCopy` 按引用悬垂（AppController.cpp:1093 起，进程退出事件延迟触发才可达，属低频路径）；(b) MainFrame onMenuDedup 分离线程 UAF（用户触发低频）；(c) detached adopt 线程（VALIDPROXY_NO_ADOPT 门控，运行态用户可关）。核心修复只做 (1)+(2)。

## 5. 修复方案（Implementation）

### 5.1 ~AppController 顺序修复

在析构函数体最前插入 `stopProxyPool()`（幂等：poolMutex_ + reset + null 检查；stop() join evaluatorThread_ ≤ intervalSec（1s 步进）+ instance_->stop()）：

```cpp
AppController::~AppController() {
    // 代理池评估线程持有 this + topWindow_ 回调，必须最先停止，
    // 早于 exitListener_ / workerThread_ / XrayManager 的任何释放。
    stopProxyPool();

    netMon_.Stop();
    shutdownRequested_ = true;
    ...（原顺序不变）
}
```

### 5.2 ProcessExitListener::shutdown() join 修复（终版含 wake-event 即时唤醒）

> **v1.0 修订（实施期发现）**：首版「锁外逐个纯 join」在实测中暴露阻塞缺陷——监视线程正坐在 `WaitForSingleObject(processHandle, intervalMs)` 内部时无法感知 running=false，join 会等完整个 interval（生产默认 30s 心跳间隔 = 主线程关闭冻结 ≤30s/线程）。TDD 用例 `ShutdownThenDestroy_LongIntervalWatch_NoCrash`（30s 间隔）首跑 31.21s 捕获该问题。终版为每 Watcher 增设 `wakeEvent`（自动复位事件），线程循环改 `WaitForMultipleObjects(2, {processHandle, wakeEvent})`，shutdown()/unwatch() 置 running=false 后 `SetEvent` 即时唤醒；修后 ProcessExitListenerTest 22/22 总耗时 3.71s（含唤醒语义），wake 生效。

锁内仅做标记（running=false）并把 `watchers_` **move 到局部容器**、清空成员；锁外先对所有 pending Watcher `SetEvent(wakeEvent)`（全部唤醒信号先发，避免多 watcher 关闭串行等待），再逐个 join（join 延迟上界从「一个完整 interval」降为「线程到达下一个检查点的微秒级」）。~ProcessExitListener 同样调用 shutdown()，析构安全；`watch()` 增设 `shutdownRequested_` 防御性拒绝（shutdown 完成后不再产生孤儿监视线程）；`unwatch()` 同步改 move-out-then-join（原 erase 后 join 在线程仍持 raw `w` 循环时即释放 Watcher = 引入过新 UAF，已修正）。

```cpp
void ProcessExitListener::shutdown() {
    shutdownRequested_.store(true);
    std::map<WatchKey, std::unique_ptr<Watcher>> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending = std::move(watchers_);
        watchers_.clear();
        for (auto& kv : pending) {
            kv.second->running.store(false);
        }
    }
    for (auto& kv : pending) {
        if (kv.second->thread.joinable()) {
            kv.second->thread.join();
        }
    }
    // pending 离开作用域 → 逐个 ~Watcher（含 HANDLE 关闭与 std::function 释放）
}
```

### 5.3 TDD 回归测试

`tests/test_process_exit_listener.cpp` 追加：
- `ShutdownJoinsWatcherThreads`：watch 一个长睡眠进程（30s heartbeat 间隔）→ shutdown() → 断言在时限内返回（join 语义验证；若仍是 detach 语义，detach 立即返回，但 Watcher 已被 clear 释放 → 线程仍在跑 → 无法直接断言；改为行为断言：shutdown() 后 heartbeatFn 不再被调用（等 2×interval 确认静默）且 shutdown() 全程无 UAF/崩溃即 PASS）。用 never-signaled event handle 模拟长进程，interval 30ms，shutdown 后等 200ms，断言无新 heartbeat。
- `HeartbeatSilentAfterShutdown`（合并入上例）：shutdown 前 heartbeat 计数 >0，shutdown 后计数不再增长。
- ~AppController 顺序（AppController 依赖 wx + 全量业务层，超出单测可组装范围）由 UI 自动化 `TestStandaloneProxyPool` 关闭链路覆盖（其 AppFixture teardown 正是崩溃复现场景）。

### 5.4 风险与缓解

- join 延迟上界：watcher 最长一次 WaitForSingleObject(intervalMs) 超时 → 检查 running=false → break。最坏 30s/线程（生产默认）。可接受（进程退出路径）。若未来需要更快，可加 event-handle 唤醒（超出本次范围）。
- shutdown 与 unwatch 并发：unwatch 在锁内 erase，move 出去的 key 不在成员 map 中 → unwatch 找不到 → return。move 后 shutdown 对已 move 的 watcher join，与 unwatch 无竞态（unwatch 只操作成员 map）。
- watch() 与 shutdown() 竞态：watch 在锁内插入成员 map；若 shutdown 已完成（成员已空），watch 后插入的线程永远无人 join → 追加 shutdownRequested_ 检查：watch() 锁内在插入前若 shutdownRequested_ 已置位则不插入、不开线程（防御未来并发路径）。**AppController 实际序：仅主线程 watch/unwatch，无此竞态，防御性检查即可**。

## 6. Verification（验证记录 — 2026-09-10/11 实测）

| 项目 | 结果 |
|------|------|
| 全量构建 `cmake --build build --parallel 8` | **[10/10] OK，0 error**（2026-09-10 09:21 链接，exe CheckSum 0x0b1c2a85） |
| ProcessExitListenerTest（含 2 新 TDD 用例） | **22/22 PASS**；纯 join 首测总 31.21s 暴露阻塞缺陷 → wake-event 终版 **3.71s**（后续稳定 ~1.3s） |
| 非 UI ctest 全套 | **38/38 PASS**（168s）无回归 |
| UI 自动化（本修复落地当轮） | 6/7：UI_CLEAR 稳定失败 + UI_EXITASSERT 92.82s 超时 —— **当时未知的缺陷②③所致**（见下） |
| 后续追踪 | UI 残留失败与新 dump（crash_20260911_09xx，exe 校验和与本修复构建精确匹配）符号化后追出**真根因②悬浮窗 WM_CLOSE 双删除**与**缺陷③托盘 m_win 同族双删除 + enumProc 竞态**——本修复消除的 detach-UAF 是关闭期堆践踏的构成部分，但非该批 dump 的直接崩溃点 |
| 四层修复全部落地后终态（2026-09-11） | **非 UI 38/38 + UI 7/7 ALL TESTS PASSED + bin/temp/ 0 新 dump + 事件日志 0 新 AppHang**（含本缺陷①②③④全链，UI_POOL 时长 53.80s→10.09s 见 ④） |

手动复现验证（启动池 → 关闭程序）由 UI 自动化 `TestStandaloneProxyPool` 池启动+关闭场景持续覆盖（PASS）。

## 7. Sources & References

- 崩溃分析全程：temp/cdb_analysis.log、bin/temp/crash_20260910_103310.dmp、bin/log/ui_20260910_103305.log
- 统一变更计划：docs/plans/2026-09-09-Plan-ProxyPoolMonitorUnify-v1.0.md、docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md
- 相关既有修复：docs/bugfix/2026-09-08-Bugfix-UITestHarness-Deadlock-Stall-v1.0.md（UI 测试基建）、2026-08-26-Bugfix-StandaloneProxyPool-PortCollision-v1.0.md（池端口）
