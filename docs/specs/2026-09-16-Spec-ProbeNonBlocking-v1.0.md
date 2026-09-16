# Spec: 探活不干扰正常操作（ProbeNonBlocking）阶段1

**日期**: 2026-09-16  
**类型**: Spec（功能优化）  
**模块**: 独立代理探活 / UI 刷新  
**版本**: v1.0（阶段 1：最小修复；阶段 2：统一健康探测服务——另行立项）

---

## 1. 背景与痛点

用户报告：
1. 界面操作（配置修改、滚动条拖动）**反应迟滞**
2. 开启"自动任务"**多次提示"当前有其它处理"**，需多次尝试才成功
3. 疑似健康度探测影响，期望统一探测 + 异步、不干扰正常操作

### 1.1 根因分析（已实证）

**痛点 ② 根因**：周期探活（`proxy_process_monitor.check_interval_ms=5000`）复用共享 `workerThread_/isRunning_`：
```
onProxyMonTimer(5s) → testOnlineProxiesAsync(silent) → guard 允许 → isRunning_=true
  → doTestOnlineProxies 运行 1-2s（每个代理测试）→ ScopeGuard 复位
```
探活高频（5s 周期 + 1-2s 运行）→ `isRunning_` 约 20-40% 时间为 true → 用户点击"自动任务"（MainFrame::onMenuAutoTask 检查 `isRunning()`）大概率被拒。

**痛点 ① 根因**：探活完成发 `ONLINE_PROBE_DONE` → `MainFrame::onStatusUpdate` → `proxyPanel_->refreshResults()`：
- `refreshResults()` = `loadProxyResults()` 全表重读（53k 行）+ `rebuildMaps()` + `Refresh()`——UI 线程重型操作
- 每 5s 触发一次 → UI 线程周期性卡顿；叠加探活 worker 线程 SQLite 写（FULLMUTEX）与 UI 读排队 → 滚动/配置操作迟滞

### 1.2 现状参照（轻量模式已存在）

`ProxyListPanel` 的 `historyTimer`（3s）已是**轻量增量异步模式**：
- `refreshHistoryPeriodic()`：后台读 running durations + `refreshInFlight_` 防重入
- `onRunningDurationsLoaded`：只 notify 变更行，空闲时不重绘

阶段 1 复用此模式，把探活完成的全量刷新改为增量。

## 2. 目标（阶段 1）

1. **探活不再占用共享 `isRunning_`**（独立线程 + 独立标志）→ 自动任务/配置保存/批量测试不被探活阻塞
2. **探活完成的 UI 刷新改为增量**（只更新受测代理的行）→ UI 迟滞消除
3. 保持手动测试/启动/停止等低频操作的全量刷新语义

## 3. 设计

### 3.1 探活独立线程（不占 `isRunning_`）

**`src/ui/AppController.h`**：
```cpp
// 独立探活线程（silent 周期探活专用，不占共享 workerThread_/isRunning_）
std::thread probeThread_;
// 析构时对 probeThread_ 做有界 join/detach（与 workerThread_ 同模式）
```

**`src/ui/AppController.cpp` — `testOnlineProxiesAsync`**：
```cpp
void AppController::testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent) {
    if (silent) {
        // 后台周期探活：独立线程，不占共享 isRunning_（用户操作优先）。
        // 若用户操作在跑、或上一轮探活尚未结束 → 静默跳过本轮。
        if (isRunning_ || probeThread_.joinable()) {
            Logger::write("[OnlineProbe] skipped: user operation running "
                          "or previous probe still in flight", LogLevel::DEBUG);
            return;
        }
        onlineProbeRunning_ = true;   // 复用既有标志（config 保存判断用）
        probeThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, true);
        return;
    }
    // 手动「测试在线代理」：优先取消正在运行的探活（可中断后台任务），
    // 再走共享 workerThread_（移动用户操作占 isRunning_）。
    if (probeThread_.joinable()) {
        cancelRequested_ = true;
        probeThread_.join();
        cancelRequested_ = false;
    }
    AsyncOperationGuard guard{workerThread_, isRunning_, cancelRequested_, wxHandler};
    if (!guard.isAllowed()) return;
    workerThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, false);
}
```

**`doTestOnlineProxies`**：**零改动**——现有两个 ScopeGuard 对两条路径均已正确复位：
- silent 路径（probeThread_）：`ScopeGuard{onlineProbeRunning_}` 复位探活标志；`ScopeGuard{isRunning_}` 复位 false（本来 false，无害）
- 手动路径（workerThread_）：`ScopeGuard{isRunning_}` 复位 guard 置位的 true；`ScopeGuard{onlineProbeRunning_}` 复位 false（无害）

**竞态分析**：
- silent 探活与手动测试互斥由 `probeThread_.joinable()` + 手动优先取消保证（同一 UI 线程顺序调用，无 data race）
- `cancelRequested_` 取消探活：doTestOnlineProxies 循环每代理前检查 `cancelRequested_.load()` → 快速退出；在途 cURL 经 progress callback 中止（最长 test_timeout_ms=5000ms）
- 探活与手动测试不与彼此并发：手动先 join 探活；探活启动前检查 `isRunning_`

### 3.2 探活完成 → 增量刷新

**`src/ui/Events.h/.cpp`**：新增自定义事件（先例：StandaloneProxyEvent）：
```cpp
wxDECLARE_EVENT(wxEVT_ONLINE_PROBE_FINISHED, OnlineProbeFinishedEvent);
class OnlineProbeFinishedEvent : public wxEvent {
public:
    OnlineProbeFinishedEvent(const std::vector<std::string>& indexIds);
    const std::vector<std::string>& getIndexIds() const;
    // wxEvent 虚函数（GetEventType/Clone 等，照 StandaloneProxyEvent 模式）
};
```

**`src/ui/AppController.cpp` — `doTestOnlineProxies`**：silent 完成 && total>0：
```cpp
if (wxHandler && silent && total > 0) {
    wxQueueEvent(wxHandler, new OnlineProbeFinishedEvent(monitorIndexIds));  // 受测 indexId 列表
}
```
（替代原 `StatusUpdateEvent("ONLINE_PROBE_DONE")`；`monitorIndexIds` 在循环中收集所有受测 indexId）

**`src/ui/MainFrame.cpp`**：
- Bind `wxEVT_ONLINE_PROBE_FINISHED` → `onOnlineProbeFinished`
- **移除** `onStatusUpdate` 中 `ONLINE_PROBE_DONE` 分支（由 `OnlineProbeFinishedEvent` 完全替代，避免双路径重复刷新）
```cpp
void MainFrame::onOnlineProbeFinished(OnlineProbeFinishedEvent& event) {
    if (proxyPanel_) {
        proxyPanel_->refreshResultsFor(event.getIndexIds());
    }
}
```

**`src/ui/ProxyListPanel.cpp/.h`**：新增增量刷新：
```cpp
void ProxyListPanel::refreshResultsFor(const std::vector<std::string>& indexIds) {
    if (indexIds.empty() || !model_ || !controller_) return;
    std::vector<db::models::ProfileExItem> rows =
        controller_->loadProxyResultsFor(indexIds);   // 小查询（只读受测行）
    if (model_->updateResultsFor(rows)) {             // 按 indexId 更新，不 rebuild 全表
        listCtrl_->Refresh();
    }
}
```

**`src/ui/AppController.cpp/.h`**：新增小查询：
```cpp
std::vector<db::models::ProfileExItem> loadProxyResultsFor(const std::vector<std::string>& indexIds);
// 实现：exDao_ 按 indexId 批量查（IN (...) 或逐查小批），只取 delay/health/message 相关列
```

**`src/ui/ProxyListModel.cpp/.h`**：新增增量更新：
```cpp
bool updateResultsFor(const std::vector<db::models::ProfileExItem>& rows);
// 按 indexId 在 exItems_ 中定位并更新对应项；返回是否有变化。
// 复用既有 lookup maps（indexId → dataIdx），无变化时返回 false 跳过重绘；
// 有变化时 data-view 依赖 notifyHistoryChanged() 使受影响行重查询。
```

### 3.3 不变项

- 手动测试/代理启动/停止/池事件 → 全量 `refreshResults()`（低频，保留）
- `historyTimer` 增量模式不变
- 池观测/评估/探针池**不迁移**（阶段 2 统一健康探测服务另行规划）
- 失败 WARN、阈值日志、`OnlineProbe skipped` DEBUG 全部保留

## 4. 行为对照（阶段 1 后）

| 场景 | 改动前 | 改动后 |
|------|--------|--------|
| 自动任务/批量测试在探活窗口点击 | 被拒（"当前有其它处理"） | **可立即启动**（探活不再占 isRunning_） |
| UI 滚动/配置修改期间探活完成 | 全量 53k 重读 → 迟滞 | **增量几行更新** → 无感 |
| 手动测试时探活正在跑 | guard 拒绝（"Operation Busy"） | **取消探活 → 手动立即启动** |
| 手动测试/启动/停止事件 UI | 全量刷新 | 不变（低频） |
| 探活与手动并发 | 互斥（guard） | 互斥（手动优先 join 探活） |

## 5. 验证

1. 构建：validproxy 0 error
2. 运行时日志验证：
   - 探活独立线程日志（手动操作时探活跳过 DEBUG）
   - 手动测试取消探活顺序（cancel → join → 手动启动）
3. 增量刷新正确性：探活完成只更新受测行 delay/health/message
4. `test_config_reader` 43/43（不受影响）
5. UI 冒烟：代理列表滚动/配置保存/自动任务在探活周期内可正常操作

## 6. 阶段 2 预告（另行立项）

统一健康探测服务（UnifiedHealthService）：将 独立代理探活 + 池观测/评估 + 探针池收拢为单一后台低优先级服务，统一健康数据变更事件与 UI 刷新节流。依赖本阶段打下的"独立探活线程 + 增量刷新"基础。

## 7. 风险

| 风险 | 缓解 |
|------|------|
| 手动 join 探活最长阻塞 test_timeout_ms（5s） | 探活可中断（cancelRequested_），实际 <1s；手动操作低频 |
| 探活与 loadProxyResultsFor 并发 DB 读 | SQLite FULLMUTEX + 读-读不互斥；探活写 updateTestResult 与 UI 小查询短暂排队（毫秒级） |
| old config.json 无 probeWorkers | 默认 2（既有）