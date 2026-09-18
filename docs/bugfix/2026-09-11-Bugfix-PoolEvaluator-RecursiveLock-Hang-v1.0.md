# Bugfix: 代理池 evaluatorLoop 递归锁导致点击启动池 AppHang（UI 冻结）

- **日期**: 2026-09-11
- **缺陷编号**: ④（统一池监控变更引入的第 4 个缺陷）
- **类型**: 死锁 / 主线程挂起（Application Hang B1，非崩溃）
- **受影响模块**: `src/StandaloneProxyPool.cpp`（evaluatorLoop / notifyChanged / snapshotMembers）
- **状态**: completed

---

## 1. 摘要

统一代理池与独立代理监控悬浮窗变更（2026-09-09）在 `evaluatorLoop` 的 `membersMutex_` 锁作用域内调用了 `notifyChanged()`，而 `notifyChanged → snapshotMembers` 会**再次加锁同一 mutex**。MinGW 工具链的 `std::mutex` 底层为 SRWLOCK（非递归），同线程二次加锁 = **确定性死锁**。生产配置（`reportHealth=true`）下点击「启动池」后 evaluator 线程永久冻结并持有锁；主线程 5 秒后因悬浮窗监控 timer → `getUnifiedMonitorRows → getPoolMembers → getMembers → snapshotMembers` 等待同一把锁而冻结 → Windows 判定 **AppHangB1**（ghost 窗口，无崩溃 dump）。修复 = 删除锁内的 `notifyChanged()` 调用（锁外已有等价的每周期通知）。

## 2. 现象与证据链

- 用户报告：修复 AV 崩溃后手动运行 bin/validproxy.exe，点击「启动池」UI 仍然死亡（m1322）。
- 会话日志 `bin/log/ui_20260911_113308.log`：onFirstShow → adopt 2 个悬垂 xray（真实 adopt watcher，非测试环境）→ 11:33:19 xray 10811 启动 → `[StandaloneProxyPool] started` → `[AppController] proxy pool started` → **日志戛然而止**（evaluator 冻结，无任何后续输出）。
- **无新崩溃 dump**（bin/temp 最新仍是 10:07:55）——不是 SEH 异常，crash::installHandler 不触发。
- Windows 事件日志两次 **AppHangB1**（Id=1002）：11:30:16 与 11:33:38，路径均为 bin\validproxy.exe，**签名完全相同**（Hang Signature P4=a889 / P23=318e，P22=P25=a8895326…，P27=P28=318eadf9…）= 确定性死锁。
- 11:27 会话日志 0 字节 = 同一死法。
- **递归锁实证**：`temp/mutex_recurse_test.cpp`（lock→lock 同线程，g++ 编译运行）→ 3s 后确认 **DEADLOCK，std::mutex 非递归（SRWLOCK 实现）**。
- **TDD RED 实证**：新增 `EvaluatorNoRecursiveLockOnNotify` 测试在修复前运行 → **60s+ 挂死**（evaluator 冻结 + stop() join 永久阻塞，测试进程无法退出）——与生产 AppHang 同一死锁的进程内复现。
- **WER 帧判读**：P23=318e=`buildPoolConfig`（池启动路径真帧）；P4=a889=`parseLogFile`（LogStatistics.cpp:89）为主线程最近调用残留，非卡死点。

## 3. 根因分析

### 3.1 死锁链（完整闭环）

1. 用户点击「启动池」→ `startProxyPool` → `pool->start()` spawn evaluatorThread；主线程返回按钮处理器（refreshRows）
2. evaluator 第一轮：`relaunchIfNeeded`（运行中跳过）→ `doProbe`（空成员毫秒级）→ **`{ lock_guard membersMutex_` → `mergeHealth` → `notifyChanged()` → `snapshotMembers` 二次 `lock(membersMutex_)` → 死锁**（evaluator 线程冻结，**永久持有 membersMutex_**）
3. 主线程 5s 后：悬浮窗监控 timer（`check_interval_ms=5000`）→ `refreshRows` → `getUnifiedMonitorRows` → `getPoolMembers` → `pool->getMembers` → `snapshotMembers` → 等待 membersMutex_ → **UI 冻结 → AppHangB1**（WER Termination Time 5 = timer 周期）
4. 日志停在 "proxy pool started" 之后 ✓（evaluator 冻结，无后续）；同签名确定性重复 ✓

### 3.2 为什么三层 AV 修复后仍死

前三个缺陷（①~AppController 不先停池 + Watcher detach-UAF；②悬浮窗 WM_CLOSE 双删除；③托盘 m_win 双删除）都是**崩溃**（c0000005 执行 AV）已修复并验证（0 新 dump）。④是**独立的挂起缺陷**——生产独有路径（`reportHealth=true` 触发锁内通知路径；沙箱 UI 测试三开关均 false 且测试环境不 wire onMembersChanged），先前 7/7 绿的 UI 套件未覆盖。

### 3.3 为什么 UI 测试没抓住（测试盲区分析）

- 单元测试裸 pool：不 wire `onMembersChanged` → `notifyChanged` 首行 `if(!onMembersChanged) return` 直接返回，永远不会递归加锁 → 测试全绿。
- UI_POOL 测试：主线程 refreshRows 竞态通常赢（evaluator 线程启动有延迟）→ 断言（启动池→停止池标签翻转）先完成 → teardown `terminate()` WM_CLOSE 广播 → ~AppController stopProxyPool → pool->stop() join 冻结的 evaluator → **挂起被 terminate() 的 3s 等待 + TerminateProcess 强杀掩盖**（测试仍 PASS，53.80s）。
- 用户真实运行无强杀兜底 → AppHang 暴露。

## 4. 修复方案

**最小变更**：删除 `evaluatorLoop` 锁作用域内的 `notifyChanged()` 调用（src/StandaloneProxyPool.cpp 原行号 :284，连同其 UI 注释 2 行）。

- 锁外（reportHealth 快照后）已有第二个 `notifyChanged()` 调用覆盖**每评估周期一次**的 UI 通知（单快照/周期，功能等价、时序等价）。
- 其余锁结构审查结论（全部安全）：injectMember/removeMember/probeNow 均锁外 notify；reinjectAll 锁内拷贝锁外 gRPC；doProbe 锁内仅拷贝 targets 后释放再 curl。

### 4.1 TDD 验证

新增 `TEST(StandaloneProxyPool, EvaluatorNoRecursiveLockOnNotify)`（tests/TestStandaloneProxyPool.cpp）：

- XRAY_REAL_EXE 真实 xray 门控（未设则 GTEST_SKIP）；temp 独立 cfgDir；cfg 20130/20131 + intervalSec=1 + resolvePoolPorts
- `std::atomic<int> notifyCount` + `onMembersChanged` lambda 计数；start() 后 40×100ms 轮询断言 notified
- stop() 用 `std::async` + `wait_for(5s)` 守护断言完成（pre-fix join 挂起 → 超时失败不挂死测试进程）
- 结果：**RED（修复前）= 60s+ 进程挂死（实证死锁）；GREEN（修复后）= PASS 2.17s**
- 全套 StandaloneProxyPoolTest **10/10 PASS**（13.4s，无回归）

## 5. 风险与边界

- UI 每周期仍收到一次成员快照通知（锁外 notifyChanged 等价覆盖）——**零功能损失**。
- 快照时序：由「mergeHealth 后立刻」推迟到「reportHealth 决策后」——同一评估周期内，用户不可感知。
- **遗留加固项**（本次不动，最小变更）：两阶段优雅移除流程在 membersMutex_ 锁内调用 `api_->removeOutbound`（gRPC 网络IO 持锁）——若 gRPC 超时将长时间持锁阻塞主线程读快照（暂无死锁，仅延迟风险）；后续应将 gRPC 移出锁外。

## 6. 验证记录

| 项目 | 结果 |
|------|------|
| 新测试 RED（修复前） | **60s+ 挂死（死锁实证）** |
| 新测试 GREEN（修复后） | PASS 2.17s |
| StandaloneProxyPoolTest 全套 | 10/10 PASS（13.4s） |
| 全量构建 `cmake --build build --parallel 8` | **[10/10] OK，0 error**（StandaloneProxyPool.cpp 重编，validproxy.exe + validproxy-cli.exe + test_autotask 重链） |
| 非 UI ctest 全套 | **38/38 PASS（89.72s）** 无回归 |
| UI 自动化全套 | **7/7 ALL TESTS PASSED（33.00s）**；**UI_POOL 53.80s → 10.09s** = stop() 真正完成 join、不再被 teardown terminate() 3s 等待 + TerminateProcess 强杀掩盖（修复生效直接证据）；UI_EXITASSERT 0.74s / UI_CLEAR 3.47s / UI_MAINWINDOW 1.08s / UI_SEARCH 3.67s / UI_FLOATINGWIDGET 3.65s / UI_PORTCLOSE 10.29s 全绿 |
| bin/temp 新 dump | **0 枚**（10:08 后持续为零） |
| 新 AppHang（事件日志 Id=1002，2026-09-11 12:00 后） | **0 次**（修复前 11:30:16 / 11:33:38 两次同签名 AppHangB1） |

## 7. 关联文档

- `docs/bugfix/2026-09-10-Bugfix-AppShutdown-PoolWatcher-UAF-v1.0.md`（缺陷①，shutdown 排序 + wakeEvent join）
- `docs/bugfix/2026-09-11-Bugfix-FloatingWidget-CloseDoubleDelete-v1.0.md`（缺陷②）
- `docs/bugfix/2026-09-11-Bugfix-TrayIcon-HelperWindow-DoubleDelete-v1.0.md`（缺陷③）
- `docs/specs/2026-09-09-Spec-ProxyPoolMonitorUnify-v1.0.md`（统一变更源头）
