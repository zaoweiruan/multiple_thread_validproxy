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

**`src/ui/AppController.cpp` — `testOnlineProxiesAsync`**（最终实现，见 §8）：
```cpp
void AppController::testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent) {
    if (silent) {
        // 周期 silent 探活：独立线程，不占共享 workerThread_/isRunning_
        // （用户操作优先）。用户在跑其它操作、或上一轮探活尚未结束 →
        // 静默跳过本轮。
        if (isRunning_ || onlineProbeRunning_) {
            Logger::write("[OnlineProbe] skipped: user operation running "
                          "or previous probe still in flight", LogLevel::DEBUG);
            return;
        }
        // 上一轮探活已结束但尚未 join（std::thread 完成后 joinable() 仍
        // true，直到 join/detach 为止）：此处快速回收，否则后续周期探活
        // 会被残留 joinable 状态永久跳过。
        if (probeThread_.joinable()) {
            probeThread_.join();
        }
        onlineProbeRunning_ = true;   // 复用既有标志（config 保存判断用）
        // 探活独立线程不消费/复位 cancelRequested_（AsyncOperationGuard 仅
        // 服务用户操作路径）。断连或 cancelTest() 残留的 true 会让每轮探活
        // 线程在循环首行立即退出 → 周期探活静默停摆。此处置位安全：上方已
        // 保证 isRunning_==false；且本函数在 UI 线程执行，与用户操作串行。
        cancelRequested_ = false;
        probeThread_ = std::thread(&AppController::doTestOnlineProxies, this, wxHandler, true);
        return;
    }

    // 手动「测试在线代理」：优先取消正在运行的探活（可中断后台任务），
    // 再走共享 workerThread_（用户操作占 isRunning_，原逻辑不变）。
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

**`doTestOnlineProxies`**：**非零改动（与初稿偏离，见 §8.1）**——`isRunning_` 复位改为**仅手动路径**的 `std::optional<ScopeGuard> runningGuard`：
- silent 路径（probeThread_）：只置 `ScopeGuard{onlineProbeRunning_}` 复位探活标志；**不**复位 `isRunning_`（探活不占该标志，若照旧复位会误清探活期间用户操作置位的标志 → 用户操作守卫被提前释放 = UB）
- 手动路径（workerThread_）：`runningGuard.emplace(isRunning_)` 复位 guard 置位的 true；`_probeGuard{onlineProbeRunning_}` 复位 false（无害）

**竞态分析**：
- silent 探活与手动测试互斥由**活标志 `onlineProbeRunning_`** + 手动优先取消保证（同一 UI 线程顺序调用，无 data race）；`probeThread_.joinable()` **不能**直接判"上一轮未结束"（已结束未 join 时仍 true，见 §8.2），仅用于快速回收残留 thread
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

**`src/ui/AppController.cpp` — `doTestOnlineProxies`**：silent 完成 && 有受测行：
```cpp
if (wxHandler && silent && !monitorIndexIds.empty()) {
    wxQueueEvent(wxHandler, new OnlineProbeFinishedEvent(monitorIndexIds));  // 受测 indexId 列表
}
```
（**完全替代**原 `StatusUpdateEvent("ONLINE_PROBE_DONE")`，`onStatusUpdate` 中该分支已删除；`monitorIndexIds` 在循环中收集所有受测 indexId——含 port<=0 分支写入 -1 的行）

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

**最终实现修正（与初稿偏离，见 §8）**：

- **增量方法落地形态**：`updateResultsFor` 最终拆为 `updateResultFor`（单数，按 indexId 逐行更新并返回是否变化）+ `notifyTestResultChangedFor`（仅通知受影响行重查询）；`ProxyListPanel::refreshResultsFor(indexIds)` 经 `AppController::loadProxyResultsFor` 小查询后逐行调用二者。
- **MainFrame 守卫修正**（探活不再占 `isRunning_` 后）：
  - 配置保存守卫**改回仅 `isRunning()`**——初稿 `&& !isOnlineProbeRunning()` 在「探活+用户操作并存」（isRunning_=true 且 onlineProbeRunning_=true）时会绕过保存守卫 → 用户操作进行中保存配置 = 与 worker 读 config_ 竞态（UB）
  - **DB 切换守卫追加 `|| isOnlineProbeRunning()`**——探活线程经 `exDao_` 持有旧 `db_` 指针，切库 `switchDatabase()` 交换 db_ 期间探活仍读写旧指针 → UAF 防护，切库需探活空闲
- **silent 启动前复位 `cancelRequested_`**：探活独立线程不消费该标志（AsyncOperationGuard 仅服务用户操作路径），断连 / cancelTest() 残留的 true 会让每轮探活循环首行即退出 → 周期探活静默停摆；silent 分支启动线程前 `cancelRequested_ = false`（安全：已保证 isRunning_==false，UI 线程串行）。
- **`ONLINE_PROBE_DONE` 已完全移除**：`onStatusUpdate` 中该分支删除，由 `wxEVT_ONLINE_PROBE_FINISHED` 唯一路径触发增量刷新；`Events.h` 注释保留历史说明。

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
| **探活+用户操作并存**（isRunning_=true 且 onlineProbeRunning_=true，探活先占、用户操作随后允许） | 互斥/拒绝 | **并存**：保存配置拒绝（守卫仅查 isRunning()）、切库拒绝（守卫追加 `\|\| isOnlineProbeRunning()`）；手动「测试在线代理」仍优先取消探活 |

## 5. 验证

1. 构建：validproxy 0 error
2. 运行时日志验证：
   - 探活独立线程日志（手动操作时探活跳过 DEBUG）
   - 手动测试取消探活顺序（cancel → join → 手动启动）
3. 增量刷新正确性：探活完成只更新受测行 **Delay/Message/Failures 三列**（`updateResultFor`）+ **历史三列 Starts/Runtime/Health**（`syncHistoryForIndexId` 单行重算，公式与 `rebuildMaps()` L86-104 逐字一致，见 §8.9）；`rowChanged || historyChanged` 才 `notifyTestResultChangedFor`（`ItemChanged(item)` 通知整行重绘，经源码核实 datavgen.cpp:737/3350-3393）
4. `test_config_reader` 43/43、`test_proxy_list_model` **27/27**（阶段 1 初稿新增 6 用例：`UpdateResultFor_FirstSeenIndexId_StoresAndReturnsTrue` / `SameValuesAgain_ReturnsFalse` / `PartialChange_UpdatesOnlyChangedField` / `NotifyTestResultChangedFor_ExistingIndexId_NotifiesThatRow` / `MissingIndexId_DoesNotNotify` / `NoData_DoesNotCrash`；最终审查 fix 4c646e9 再新增 6 用例：`SyncHistoryForIndexId_FirstSeenIndexId_ReturnsTrue` / `SameValuesAgain_ReturnsFalse` / `MissingIndexId_ReturnsFalse` / `PartialChange_UpdatesOnlyChangedField` / `HistoryMapsPreserveNonZeroBase` / `HistoryFormulaMatchesRebuildMaps`（45 组 `(start, crash, runtime)` 输入断言 sync 与全量 `rebuildMaps()` 对 health/runtime 等价））
5. UI 冒烟：代理列表滚动/配置保存/自动任务在探活周期内可正常操作

## 6. 阶段 2 预告（另行立项）

统一健康探测服务（UnifiedHealthService）：将 独立代理探活 + 池观测/评估 + 探针池收拢为单一后台低优先级服务，统一健康数据变更事件与 UI 刷新节流。依赖本阶段打下的"独立探活线程 + 增量刷新"基础。

## 7. 风险

| 风险 | 缓解 |
|------|------|
| 手动 join 探活最长阻塞 test_timeout_ms（5s） | 探活可中断（cancelRequested_），实际 <1s；手动操作低频 |
| 探活与 loadProxyResultsFor 并发 DB 读 | SQLite FULLMUTEX + 读-读不互斥；探活写 updateTestResult 与 UI 小查询短暂排队（毫秒级） |
| old config.json 无 probeWorkers | 默认 2（既有）

---

## 8. 实施偏离记录（2026-09-16 最终实现）

阶段 1 已实现（提交 `66f9635..4e3a269`，11 commits = 初稿 9 + 最终审查 fix 1 + 文档同步 1；全量回归：test_config_reader 43/43、test_proxy_list_model 27/27、全量 `ctest --test-dir build` 47/47、validproxy/validproxy-cli/UITests 构建 0 error；最终审查追加 exDao_ 切库重绑 + 历史列同步两处修复，见 §8.8/§8.9）。初稿设计与最终实现存在以下偏离（保留原设计意图，此处汇总）：

### 8.1 §3.1「doTestOnlineProxies 零改动」不成立

silent 探活不再占 `isRunning_` 后，`doTestOnlineProxies` 内既有 `ScopeGuard{isRunning_}` 会在探活期间用户操作置位该标志时**误清用户操作守卫**。最终实现：

```cpp
std::optional<ui::ScopeGuard<std::atomic<bool>>> runningGuard;
if (!silent) {
    runningGuard.emplace(isRunning_);
}
ui::ScopeGuard<std::atomic<bool>> _probeGuard{onlineProbeRunning_};
```

仅 silent==false（手动路径）才 emplace `runningGuard`；silent 路径只复位 `onlineProbeRunning_`。

### 8.2 §3.1「probeThread_.joinable() 判上一轮未结束」不正确

`std::thread::joinable()` 在线程已结束但未 join 时仍返回 true → 用其判"上一轮未结束"会让每轮周期探活被残留 joinable 状态永久跳过。最终实现以**活标志 `onlineProbeRunning_`**（探活线程 ScopeGuard 复位）判定上一轮是否在跑；`joinable()` 仅用于快速回收已结束未 join 的线程（`if (probeThread_.joinable()) probeThread_.join();`）。

### 8.3 §3.2 MainFrame/DB 守卫补充

- 配置保存守卫改回仅 `isRunning()`（原 `&& !isOnlineProbeRunning()` 在探活+用户操作并存时绕过守卫 → 与 worker 读 config_ 竞态 UB）
- DB 切换守卫追加 `|| isOnlineProbeRunning()`（探活经 exDao_ 持有旧 db_ 指针，切库需探活空闲 → UAF 防护）

### 8.4 §3.2 silent 启动前复位 cancelRequested_

探活独立线程不消费 `cancelRequested_`（AsyncOperationGuard 仅服务用户操作路径），断连 / cancelTest() 残留会让每轮探活线程在循环首行立即退出（探活静默停摆）。silent 分支启动线程前 `cancelRequested_ = false`（安全：已保证 isRunning_==false，且 UI 线程串行）。

### 8.5 新事件替代

`ONLINE_PROBE_DONE` 已**完全移除**（`onStatusUpdate` 分支删除），由 `OnlineProbeFinishedEvent`（携带受测 indexId 列表）唯一路径触发增量刷新；`Events.h` 注释保留历史说明。

### 8.6 §4 行为对照新增"探活+用户操作并存"状态

isRunning_=true 且 onlineProbeRunning_=true（探活先占、用户操作随后允许）——保存配置拒绝、切库拒绝；手动「测试在线代理」仍优先取消探活。

### 8.7 测试补充

`tests/test_proxy_list_model.cpp` 新增 6 用例（15 → 21/21）：`UpdateResultFor_FirstSeenIndexId_StoresAndReturnsTrue`、`UpdateResultFor_SameValuesAgain_ReturnsFalse`、`UpdateResultFor_PartialChange_UpdatesOnlyChangedField`、`NotifyTestResultChangedFor_ExistingIndexId_NotifiesThatRow`、`NotifyTestResultChangedFor_MissingIndexId_DoesNotNotify`、`NotifyTestResultChangedFor_NoData_DoesNotCrash`；`test_config_reader` 43/43 不受影响。

### 8.8 exDao_ 切库重绑（原守卫必要但不充分，已补 setDb）

§8.3 引入的「DB 切换守卫追加 `|| isOnlineProbeRunning()`」只挡住了**探活跨切换窗口**（切换瞬间探活仍在跑 → 拒绝切换），无法防止**切库后下一轮探活**用旧句柄：`ProfileExItemDAO` 构造时捕获当时的 `sqlite3*` 指针，`switchDatabase()` 关闭旧库、打开新库并交换 `db_` 后，`exDao_` 内部 `db_` 仍指向已关闭的旧 `sqlite3*` → 下一轮周期探活经 `exDao_` 读写 = UAF。

修复（commit 4c646e9）：
- `ProfileExItemDAO::setDb(sqlite3* db)` 新增（include/Profileexitem.h:105，`void setDb(sqlite3* db) { db_ = db; }`）——DAO 是构造一次、句柄可换的绑定契约；
- `AppController::switchDatabase()` 交换 `db_` 后新增 `exDao_.setDb(db_)`（AppController.cpp:178）。

原守卫保留（探测窗口内的用户操作仍需拒绝切库，防止并发写冲突），`setDb` 补齐切库后 DAO 句柄陈旧的第二层缺口。

### 8.9 历史列同步（syncHistoryForIndexId 补偿增量路径）

初稿 `refreshResultsFor` 只调 `updateResultFor`（Delay/Message/Failures 三列），假设探活失败不影响历史三列。实际探活失败经 `updateTestResult` 写回时会重置 `start_count` / `total_runtime_ms` / `crash_count` = 0（与独立代理测试失败语义一致），全量 `refreshResults()` → `rebuildMaps()` 会同步刷新 Health/Starts/Runtime 三列，而增量路径缺这一步 → 探活失败的死代理在列表里一直显示上一轮的健康值（本交付引入的回归，最终审查发现）。

修复（commit 4c646e9）：
- `ProxyListModel::syncHistoryForIndexId(indexId)` 新增（ProxyListModel.cpp:203-264）——单行重算 `startCountMap_` / `runtimeMap_` / `healthMap_`，**逐字复制 `rebuildMaps()` L86-104 三公式**（start 直读 / runtime 保留非零历史 base 的三态 / health 贝叶斯平滑 + 冷启动强制 0.0）；
- `refreshResultsFor` 在 `*it = *rit` 同步源数据后调用 `syncHistoryForIndexId`，`rowChanged || historyChanged` 才 `notifyTestResultChangedFor`（ProxyListPanel.cpp:235-246）；
- **`ItemChanged(item)` 整行重绘已核实**：MSW generic dataview 下 `wxDataViewModel::ItemChanged`（dataview.h 公开成员，ProxyListModel.cpp:267-281 直接调用）触发 `wxDataViewStore::UpdateItem`（datavgen.cpp:737）→ `UpdateItemInternal`（3350-3393）重绘整个 row（非单列），因此受测行的历史列能随增量通知一起刷新，无需额外 `notifyHistoryChanged()`；
- 新增 6 个 GTest（21 → 27/27），含 `SyncHistoryForIndexId_HistoryFormulaMatchesRebuildMaps`：45 组 `(start, crash, runtime)` 输入，断言 sync 路径与全量 `rebuildMaps()` 对 health（`EXPECT_NEAR 1e-12`）与 runtime 严格等价，把单行公式钉死在全量重建公式上（防止未来单侧漂移）。

### 8.10 既有测试断言漂移（非本交付引入）

最终审查另发现一处既有测试断言漂移，已随本次提交一并修复：`ConfigParserRejectsInvalidEnum`（tests/TestStandaloneProxyPool.cpp）旧断言「非正值 probeWorkers 保持默认 2」，但 commit 5471274（PoolConfigDialogAdjust）已将解析器合法区间扩为 `0..64`（0 = 禁用常驻探针池），故 `probeWorkers=0` 应解析为 0 而非保持默认，旧断言失败导致全量 ctest 47 项中 1 失败。修复：`65 → 保持默认 2`（超上限）、`0 → 解析为 0`（合法禁用）、`-1 → 保持默认 2`（负值拒绝）三段断言；`StandaloneProxyPool.h` 探针池成员过期注释（"probeWorkers <= 0 disallowed by parser"）同步更新为「0 禁用，start 失败保持 null-safe」。修复后 **StandaloneProxyPoolTest 100% PASS**；修复后最终全量 `ctest --test-dir build` **47/47 PASS（126.79s，2026-09-16）**，其中此前非确定性失败过的既有 flaky 项 `UI_FLOATINGWIDGET`「Orb hit-test」OS 级 WindowFromPoint 网格探针本轮亦通过。