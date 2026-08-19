# 2026-08-18-Spec-ProxyListRefresh-v2.0.md

> 文档类型：技术方案（Spec）
> 模块：UI 代理列表面板评价列定期刷新 / 网络监控状态刷新
> 版本：v2.0
> 日期：2026-08-18

## 1. 背景与问题

### 1.1 现象

GUI 启动后，代理列表（ProxyListPanel）的"评价"列（Starts / Runtime / Health）每 2 秒
定时刷新（`historyTimer_`），但**刷新期间主窗口无法操作**（拖动、点击、菜单均无响应，
表现为 UI 冻结）。

### 1.2 根因分析（RCA）

`ProxyListPanel::refreshHistoryPeriodic()` 在 **UI 线程**（wxTimer 事件处理器）内同步执行：

| 步骤 | 操作 | 成本 |
| --- | --- | --- |
| 1 | `controller_->loadProxyResults()` → `ProfileExItemDAO::getAll()` | **全表查询**（生产库 53,837 行）+ `computeBatch` 评分计算，秒级 |
| 2 | `model_->rebuildMaps()` | 全量重建 delay/message/failures/runtime/health 等 map，O(N) |
| 3 | `controller_->getRunningDurations()` | 第二次 DB 查询（proxy_runtime_history） |
| 4 | `model_->setRunningDurations()` + `notifyHistoryChanged()` + `Refresh()` | 视图重查模型 |

第 1、3 步是 **DB I/O**，第 2 步是 **O(N) 全量重建**——全部压在 UI 线程，
每 2 秒执行一次，导致 UI 消息循环被长时间占用 → mainframe 无响应。

对比：项目已有正确模式 `AppController::loadProxiesAsync()`（后台线程 + 独立 SQLite
连接 + `wxQueueEvent` 回 UI 线程），但 `loadProxyResults()` / `getRunningDurations()`
仍是同步的。

### 1.3 网络监控定时器现状

`MainFrame::onNetMonTimer`（`netMonTimer_`，2 秒）仅执行：
- `NetworkMonitor::IsConnected()` → 读 `std::atomic<bool>`（微秒级）
- 状态变化时才 `netMonPanel_->Refresh()`（状态栏小面板重绘，毫秒级）

**结论：网络监控轮询本身不构成 UI 阻塞根因**（无 DB I/O、无 O(N) 计算）。
但作为"定期刷新"的另一个实例，本次规划一并明确其策略，避免未来引入同类问题。

## 2. 设计目标

1. **UI 线程零 DB I/O**：评价列刷新不再在 UI 线程执行任何 SQL 查询。
2. **增量刷新**：定时刷新只关心"运行中会话"（`ended_at IS NULL`）的实时时长，
   不再全表重查 exItems、不再全量重建 map。
3. **防抖**：后台查询未返回前不重复发起，避免线程堆积。
4. **网络监控**：保持轻量轮询，明确其刷新策略，与评价刷新节奏协调但不耦合。

## 3. 方案设计

### 3.1 数据流（新）

```
[wxTimer 3s]  onHistoryTimer (UI线程)
   └─> 若 refreshInFlight_ 已置位 → 跳过本次
   └─> getRunningDurationsAsync(this)      // 后台线程，不阻塞 UI
         ├─ 独立 sqlite3 连接（WAL + busy_timeout 5000）
         ├─ SELECT index_id, duration_ms FROM proxy_runtime_history
         │      WHERE ended_at IS NULL AND duration_ms IS NOT NULL
         └─ wxQueueEvent(handler, RunningDurationsLoadedEvent(map))

[UI线程]  onRunningDurationsLoaded
   ├─ model_->setRunningDurations(map)     // 仅合并 running 行 runtime/health（O(running)）
   ├─ model_->notifyHistoryChanged()       // 仅通知 start_count>0 的行（既有优化）
   ├─ listCtrl_->Refresh()
   └─ refreshInFlight_ = false
```

### 3.2 变更清单

| 文件 | 变更 |
| --- | --- |
| `src/ui/Events.h` | 新增 `RunningDurationsLoadedEvent`（持有 `std::unordered_map<std::string,long long>`，move 语义）+ `wxEVT_RUNNING_DURATIONS_LOADED` 声明 |
| `src/ui/Events.cpp` | 新增 `wxDEFINE_EVENT(wxEVT_RUNNING_DURATIONS_LOADED, RunningDurationsLoadedEvent)` |
| `src/ui/AppController.h` | 新增 `void getRunningDurationsAsync(wxEvtHandler* handler);` |
| `src/ui/AppController.cpp` | 实现 `getRunningDurationsAsync`：后台线程 + 独立连接 + wxQueueEvent（复用 loadProxiesAsync 的 PRAGMA 配置） |
| `src/ui/ProxyListPanel.h` | 新增 `onRunningDurationsLoaded(RunningDurationsLoadedEvent&)` 声明；新增 `std::atomic<bool> refreshInFlight_{false}`；`refreshHistoryPeriodic()` 改为触发异步 |
| `src/ui/ProxyListPanel.cpp` | 事件表加绑定；定时器 2000→3000ms；`refreshHistoryPeriodic()` 移除 `loadProxyResults()` + `rebuildMaps()`，改为防抖 + `getRunningDurationsAsync(this)`；新增 `onRunningDurationsLoaded` 处理器 |
| `src/ui/MainFrame.cpp` | 不改（网络监控保持 2s atomic 轮询，见 §3.3） |

### 3.3 网络监控刷新策略

- 保持 `netMonTimer_` 2 秒轮询：仅 `IsConnected()`（atomic 读）+ 状态变化条件重绘。
- **不合并**两个定时器：`netMonPanel_` 属于 MainFrame 状态栏，`historyTimer_` 属于
  ProxyListPanel 子面板，合并会增加跨组件耦合；两者均轻量后无性能问题。
- 后续如需统一节奏，可单独把 `netMonTimer_` 调整为 3s（本次不改，最小改动）。

### 3.4 刷新频率说明

- 评价列定时刷新：2000ms → **3000ms**。心跳（`proxy_runtime_history.duration_ms`）每
  30 秒更新一次，3 秒轮询足以及时捕捉变化，同时降低 UI 事件频率。
- 定时刷新**只重查 running 行**，非运行态代理的 Starts/Runtime/Health 在
  测试完成事件（`notifyTestResultChanged` 路径）时更新，不由定时器负责。

## 4. 边界与风险

| 风险 | 对策 |
| --- | --- |
| 后台线程与 UI 线程并发访问 `db_` | `getRunningDurationsAsync` 使用**独立 sqlite3 连接**（读连接），不触碰 `db_`；与 `loadProxiesAsync` 模式一致 |
| 定时触发时上次查询未完成 | `refreshInFlight_`（`std::atomic<bool>`）防抖，未完成则跳过本次 |
| 事件携带大 map | running 行数通常极小（进行中的测试会话）；事件按 move 语义传递 |
| 析构时后台线程仍在查询 | 线程 detach；连接生命周期由线程内管理（局部变量，线程退出自动关闭）；查询为只读短事务，风险可接受（与既有 loadProxiesAsync 相同） |
| `notifyHistoryChanged` 仍 O(可见行) | 仅遍历 start_count>0 的行，既有实现已避免全库 ValueChanged |

## 5. 验证计划

1. `cmake --build build --parallel 2`：主程序 + CLI 编译通过。
2. `ctest --test-dir build -R ProxyListModelTest -V`：模型增量更新行为不回归。
3. `ctest --test-dir build`：全量测试回归。
4. 手动 GUI：启动后连续操作窗口（拖动/点击/切订阅）应无冻结；评价列每 3 秒
   捕捉 running 时长变化（配合运行代理验证）。

## 6. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v2.0 | 2026-08-18 | 定时刷新移出 UI 线程、改为增量查询 + 后台线程；网络监控策略明确 |
