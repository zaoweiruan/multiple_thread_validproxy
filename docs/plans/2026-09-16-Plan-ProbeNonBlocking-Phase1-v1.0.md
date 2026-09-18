# ProbeNonBlocking Phase 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让周期探活不再占用共享 `isRunning_`（独立线程），探活完成的 UI 刷新改为增量（只更新受测行），消除"自动任务被拒 / 界面迟滞"。

**Architecture:** ① `testOnlineProxiesAsync(silent=true)` 改用独立 `probeThread_` + `probeRunning_`（不占共享 workerThread_/isRunning_）；手动路径先取消探活再走原有 guard。② 探活完成发新事件 `OnlineProbeFinishedEvent`（携带受测 indexId 列表），MainFrame 转发 `ProxyListPanel::refreshResultsFor` 增量刷新（模型 maps 按 indexId 更新，只通知变更行）。

**Tech Stack:** C++17, wxWidgets (wxEvent/wxTimer/wxDataViewIndexListModel), SQLite (ProfileExItemDAO), CMake + Ninja (MinGW/GCC)。

**Spec:** `docs/specs/2026-09-16-Spec-ProbeNonBlocking-v1.0.md`

---

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `src/ui/Events.h` / `src/ui/Events.cpp` | 新事件 `OnlineProbeFinishedEvent`（携带受测 indexId 列表） | Modify |
| `src/ui/AppController.h` / `src/ui/AppController.cpp` | `probeThread_` 成员 + 析构清理 + `testOnlineProxiesAsync` 独立线程/手动优先 + `doTestOnlineProxies` 发新事件 + `loadProxyResultsFor` | Modify |
| `src/ui/ProxyListModel.h` / `src/ui/ProxyListModel.cpp` | `updateResultFor` 增量 maps 更新 + `notifyTestResultChangedFor` 单行通知 | Modify |
| `src/ui/ProxyListPanel.h` / `src/ui/ProxyListPanel.cpp` | `refreshResultsFor(indexIds)` 增量刷新入口 | Modify |
| `src/ui/MainFrame.h` / `src/ui/MainFrame.cpp` | Bind 新事件 + `onOnlineProbeFinished`；移除 `ONLINE_PROBE_DONE` 分支 | Modify |

---

## Task 1: OnlineProbeFinishedEvent

**Files:**
- Modify: `src/ui/Events.h`（前置声明 + wxDECLARE_EVENT + 类定义）
- Modify: `src/ui/Events.cpp`（wxDEFINE_EVENT）
- Test: 构建 `validproxy`（线程路径编译含 Events）

- [ ] **Step 1: Events.h 前置声明与事件声明**

在 `src/ui/Events.h` 顶部声明区（`class StandaloneProxyEvent;` 附近）加：

```cpp
class OnlineProbeFinishedEvent;
```

在事件声明区（`wxDECLARE_EVENT(wxEVT_STANDALONE_PROXY, StandaloneProxyEvent);` 附近，L64-67 区域）加：

```cpp
wxDECLARE_EVENT(wxEVT_ONLINE_PROBE_FINISHED, OnlineProbeFinishedEvent);
```

- [ ] **Step 2: Events.h 类定义**

在 `StandaloneProxyEvent` 类（L314-337）之后插入：

```cpp
// ---------------------------------------------------------------
// OnlineProbeFinishedEvent — posted by AppController after the periodic
// SILENT probe completes. Carries the indexIds that were actually tested
// so the proxy list can refresh only those rows (incremental, no full
// 53k-row reload). Replaces the old StatusUpdateEvent("ONLINE_PROBE_DONE").
// ---------------------------------------------------------------
class OnlineProbeFinishedEvent : public wxEvent {
public:
    explicit OnlineProbeFinishedEvent(std::vector<std::string> indexIds = std::vector<std::string>())
        : wxEvent(0, wxEVT_ONLINE_PROBE_FINISHED),
          indexIds_(std::move(indexIds)) {}

    wxEvent* Clone() const override { return new OnlineProbeFinishedEvent(*this); }

    std::vector<std::string> takeIndexIds() { return std::move(indexIds_); }
    const std::vector<std::string>& getIndexIds() const { return indexIds_; }

private:
    std::vector<std::string> indexIds_;
};
```

- [ ] **Step 3: Events.cpp 定义事件**

在 `src/ui/Events.cpp`（`wxDEFINE_EVENT(wxEVT_STANDALONE_PROXY, StandaloneProxyEvent);` 附近）加：

```cpp
wxDEFINE_EVENT(wxEVT_ONLINE_PROBE_FINISHED, OnlineProbeFinishedEvent);
```

- [ ] **Step 4: 构建验证**

```powershell
Get-Process validproxy -ErrorAction SilentlyContinue | Stop-Process -Force
cmake --build build --target validproxy --parallel 8
```

Expected: 0 error, `Linking CXX executable ...\bin\validproxy.exe`（`OnlineProbeFinishedEvent` 未被引用可能不链接，但头文件必须编译通过——`Events.h` 被多处 include）。

- [ ] **Step 5: Commit**

```bash
git add src/ui/Events.h src/ui/Events.cpp
git commit -m "feat(ui): add OnlineProbeFinishedEvent carrying tested indexIds"
```

---

## Task 2: AppController 独立探活线程

**Files:**
- Modify: `src/ui/AppController.h`（`probeThread_` 成员）
- Modify: `src/ui/AppController.cpp`（析构清理 + `testOnlineProxiesAsync` 重构）

- [ ] **Step 1: AppController.h 新增独立探活线程成员**

在 `src/ui/AppController.h`（`onlineProbeRunning_` 声明后，L252-256 区域）加：

```cpp
  // 周期 silent 探活专用独立线程：不占共享 workerThread_/isRunning_，
  // 用户操作优先（手动测试先取消探活再启动）。
  std::thread probeThread_;
```

- [ ] **Step 2: 析构函数清理 probeThread_**

在 `src/ui/AppController.cpp` 析构（`~AppController` 内 `workerThread_` join 块之后、`XrayManager::release()` 之前）加：

```cpp
    // Signal cancellation for any in-flight periodic probe, then join it
    // with the same bounded-wait pattern as workerThread_.
    cancelRequested_ = true;
    if (probeThread_.joinable()) {
        std::future<void> fut = std::async(std::launch::async, [this]() {
            if (probeThread_.joinable()) {
                probeThread_.join();
            }
        });
        if (fut.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            Logger::write("[AppController] Destructor: probe thread join timed out, detaching", LogLevel::WARN);
            probeThread_.detach();
        }
    }
    cancelRequested_ = false;
```

> 注意：析构顶部已有 `cancelRequested_ = true;`（L90），此处只需在 worker join 之后处理 probeThread_（worker 块 L96-108 之后插入）。`cancelRequested_ = true` 已设置，探活线程循环会快速退出。

- [ ] **Step 3: 重构 testOnlineProxiesAsync**

将 `src/ui/AppController.cpp` 的 `testOnlineProxiesAsync`（当前约 L484-512）整体替换为：

```cpp
void AppController::testOnlineProxiesAsync(wxEvtHandler* wxHandler, bool silent) {
    if (silent) {
        // 周期 silent 探活：独立线程，不占共享 isRunning_（用户操作优先）。
        // 用户在跑其它操作、或上一轮探活尚未结束 → 静默跳过本轮。
        if (isRunning_ || probeThread_.joinable()) {
            Logger::write("[OnlineProbe] skipped: user operation running "
                          "or previous probe still in flight", LogLevel::DEBUG);
            return;
        }
        onlineProbeRunning_ = true;   // 供 config 保存区分后台任务
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

- [ ] **Step 4: 构建验证**

```powershell
cmake --build build --target validproxy --parallel 8
```

Expected: 0 error。`doTestOnlineProxies` 零改动（两个 ScopeGuard 已覆盖两条路径复位）。

- [ ] **Step 5: Commit**

```bash
git add src/ui/AppController.h src/ui/AppController.cpp
git commit -m "feat(pool): run periodic silent probe on dedicated probeThread_ (no isRunning_ hold)"
```

---

## Task 3: doTestOnlineProxies 发新事件

**Files:**
- Modify: `src/ui/AppController.cpp`（`doTestOnlineProxies` silent 完成段）

- [ ] **Step 1: 收集受测 indexId + 替换事件**

在 `doTestOnlineProxies` 中：
1. 循环外（`int total = 0;` 附近）加：
```cpp
        std::vector<std::string> monitorIndexIds;
```
2. 循环内（`total++;` 之后）加收集（跳过未测的 port<=0 行？未测行也写入了 -1，应一并刷新，故无条件收集）：
```cpp
        monitorIndexIds.push_back(mon.indexId);
```
3. 将 silent 完成事件段（当前约 L2238-2241）：
```cpp
        // Periodic silent probe: no status-bar chatter, no result event — but
        // still notify the UI to refresh the Delay/Health/Message columns once.
        if (wxHandler && silent && total > 0) {
            wxQueueEvent(wxHandler, new StatusUpdateEvent(0, "ONLINE_PROBE_DONE"));
        }
```
替换为：
```cpp
        // Periodic silent probe: no status-bar chatter, no result event — but
        // still notify the UI to refresh ONLY the tested rows (incremental,
        // keeps the 53k-row reload off the UI thread).
        if (wxHandler && silent && !monitorIndexIds.empty()) {
            wxQueueEvent(wxHandler, new OnlineProbeFinishedEvent(monitorIndexIds));
        }
```

- [ ] **Step 2: 构建验证 + 确认无 ONLINE_PROBE_DONE 残留**

```powershell
cmake --build build --target validproxy --parallel 8
grep -rn "ONLINE_PROBE_DONE" src/   # 应只剩 MainFrame 处理器（Task 6 移除）
```

Expected: 0 error；`ONLINE_PROBE_DONE` 仅在 MainFrame.cpp 出现。

- [ ] **Step 3: Commit**

```bash
git add src/ui/AppController.cpp
git commit -m "feat(pool): post OnlineProbeFinishedEvent with tested indexIds after silent probe"
```

---

## Task 4: ProxyListModel 增量更新

**Files:**
- Modify: `src/ui/ProxyListModel.h` / `src/ui/ProxyListModel.cpp`

- [ ] **Step 1: 头文件新增两方法**

在 `src/ui/ProxyListModel.h`（`setRunningDurations` 声明之后，L74 附近）加：

```cpp
    // Update the test-result lookup maps for ONE indexId (delay/message/
    // failures columns).  Caller must also keep the panel-owned exItems_
    // vector in sync.  Returns true when any value actually changed (caller
    // may then notify only this row).
    bool updateResultFor(const std::string& indexId,
                         const std::string& delay,
                         const std::string& message,
                         int failures);

    // Notify the view that ONE row's test-result cells changed.  No-op when
    // the indexId is not currently visible (filtered out / not present).
    void notifyTestResultChangedFor(const std::string& indexId);
```

- [ ] **Step 2: 实现 updateResultFor + notifyTestResultChangedFor**

在 `src/ui/ProxyListModel.cpp`（`setRunningDurations` 实现之后）加：

```cpp
bool ProxyListModel::updateResultFor(const std::string& indexId,
                                     const std::string& delay,
                                     const std::string& message,
                                     int failures) {
    bool changed = false;
    auto it = delayMap_.find(indexId);
    const bool known = it != delayMap_.end();
    if (!known || it->second != delay) { delayMap_[indexId] = delay; changed = true; }
    auto mIt = messageMap_.find(indexId);
    if (!mIt || mIt->second != message) { messageMap_[indexId] = message; changed = true; }
    auto fIt = failuresMap_.find(indexId);
    if (!fIt || fIt->second != failures) { failuresMap_[indexId] = failures; changed = true; }
    return changed;
}

void ProxyListModel::notifyTestResultChangedFor(const std::string& indexId) {
    const int row = findRowByIndexId(indexId);
    if (row < 0) return;
    const wxDataViewItem item = wxDataViewItem(static_cast<unsigned int>(row) + idOffset_);
    // wxDataViewIndexListModel native ItemChanged refreshes the row's cells.
    ItemChanged(item);
}
```

> 说明：`findRowByIndexId` 返回 view row（-1=不存在）；`idOffset_` 已在 `detectIdOffset()` 设置。`ItemChanged` 是 `wxDataViewIndexListModel` 保护成员，模型内可直接调用；若编译报 access 问题，改用 `wxDataViewCtrl` 引用的 `ItemChanged`——实现时若 `ItemChanged` 不可见则以 `notifyTestResultChanged()` 全量通知兜底并注释说明。

- [ ] **Step 3: 构建验证**

```powershell
cmake --build build --target validproxy --parallel 8
```

Expected: 0 error。

- [ ] **Step 4: Commit**

```bash
git add src/ui/ProxyListModel.h src/ui/ProxyListModel.cpp
git commit -m "feat(ui): add ProxyListModel incremental result update (updateResultFor)"
```

---

## Task 5: loadProxyResultsFor + refreshResultsFor

**Files:**
- Modify: `src/ui/AppController.h` / `src/ui/AppController.cpp`（小查询）
- Modify: `src/ui/ProxyListPanel.h` / `src/ui/ProxyListPanel.cpp`（增量刷新入口）

- [ ] **Step 1: AppController 声明 + 实现 loadProxyResultsFor**

`src/ui/AppController.h`（`loadProxyResults` 声明 L125 附近）加：

```cpp
std::vector<db::models::ProfileExItem> loadProxyResultsFor(const std::vector<std::string>& indexIds);
```

`src/ui/AppController.cpp`（`loadProxyResults` 实现之后，约 L420 前）加：

```cpp
std::vector<db::models::ProfileExItem> AppController::loadProxyResultsFor(
        const std::vector<std::string>& indexIds) {
    std::vector<db::models::ProfileExItem> rows;
    rows.reserve(indexIds.size());
    db::models::ProfileExItemDAO dao(db_);
    for (const std::string& id : indexIds) {
        std::optional<db::models::ProfileExItem> ex = dao.getByIndexId(id);
        if (ex.has_value()) {
            rows.push_back(std::move(*ex));
        }
    }
    return rows;
}
```

- [ ] **Step 2: ProxyListPanel 声明 + 实现 refreshResultsFor**

`src/ui/ProxyListPanel.h`（`refreshResults` 声明 L43 附近）加：

```cpp
void refreshResultsFor(const std::vector<std::string>& indexIds);
```

`src/ui/ProxyListPanel.cpp`（`refreshResults` 实现之后）加：

```cpp
// -------------------------------------------------------------------
// Incremental refresh: only the tested proxy rows' Delay/Message/Failures
// columns are reloaded and updated (probe-triggered, see
// OnlineProbeFinishedEvent).  No full-table re-read and no map rebuild —
// keeps the 53k-row reload off the UI thread.
// -------------------------------------------------------------------
void ProxyListPanel::refreshResultsFor(const std::vector<std::string>& indexIds) {
    if (indexIds.empty() || !model_ || !controller_) {
        return;
    }
    std::vector<db::models::ProfileExItem> rows =
        controller_->loadProxyResultsFor(indexIds);

    bool anyChanged = false;
    // Keep the panel-owned exItems_ source vector in sync (model reads it
    // via non-owning pointer) and update the model's lookup maps per row.
    for (const db::models::ProfileExItem& row : rows) {
        for (std::vector<db::models::ProfileExItem>::iterator it = exItems_.begin();
             it != exItems_.end(); ++it) {
            if (it->indexid != row.indexid) continue;
            const std::string delay = utils::isTestResultValid(row.delay, row.delay)
                                          ? std::to_string(row.delay) : std::string("");
            const bool rowChanged = model_->updateResultFor(
                row.indexid, delay, row.message, row.consecutive_failures);
            *it = row;   // 同步源数据
            if (rowChanged) {
                anyChanged = true;
                model_->notifyTestResultChangedFor(row.indexid);
            }
            break;
        }
    }
    if (anyChanged) {
        listCtrl_->Refresh();
    }
}
```

> 说明：`row.delay` 字段类型参考 `ProfileExItem`（long）；`utils::isTestResultValid(success, latencyMs)` 签名若与实际不符，以 `loadProxyResults()`/`refreshResults()` 中现有 delay 格式化逻辑为准（复制其表达式）。`consecutive_failures` 字段名参考 DAO 用法。

- [ ] **Step 3: 构建验证**

```powershell
cmake --build build --target validproxy --parallel 8
```

Expected: 0 error。若 `ProfileExItem` 字段名或 `utils::isTestResultValid` 签名不匹配，以头文件实际定义修正后编译。

- [ ] **Step 4: Commit**

```bash
git add src/ui/AppController.h src/ui/AppController.cpp src/ui/ProxyListPanel.h src/ui/ProxyListPanel.cpp
git commit -m "feat(ui): incremental refreshResultsFor for probe-tested rows only"
```

---

## Task 6: MainFrame 接线

**Files:**
- Modify: `src/ui/MainFrame.h` / `src/ui/MainFrame.cpp`

- [ ] **Step 1: 头文件声明 handler**

`src/ui/MainFrame.h`（`onStatusUpdate` 声明附近）加：

```cpp
void onOnlineProbeFinished(OnlineProbeFinishedEvent& event);
```

- [ ] **Step 2: 构造函数 Bind**

`src/ui/MainFrame.cpp` 中绑定 `wxEVT_STANDALONE_PROXY` 的位置附近（L228-231 区域，事件 Bind 区）加：

```cpp
Bind(wxEVT_ONLINE_PROBE_FINISHED, &MainFrame::onOnlineProbeFinished, this);
```

- [ ] **Step 3: 实现 handler + 移除旧分支**

在 `MainFrame::onStatusUpdate` 之后（或 `onStatusUpdate` 内 `ONLINE_PROBE_DONE` 分支附近）新增：

```cpp
void MainFrame::onOnlineProbeFinished(OnlineProbeFinishedEvent& event) {
    if (proxyPanel_) {
        proxyPanel_->refreshResultsFor(event.getIndexIds());
    }
}
```

在 `onStatusUpdate` 中**删除** `ONLINE_PROBE_DONE` 分支（`else if (text == "ONLINE_PROBE_DONE") { ... }` 整块）。

- [ ] **Step 4: 构建验证 + 残留检查**

```powershell
cmake --build build --target validproxy --parallel 8
grep -rn "ONLINE_PROBE_DONE" src/
```

Expected: 0 error；`ONLINE_PROBE_DONE` 无残留。

- [ ] **Step 5: Commit**

```bash
git add src/ui/MainFrame.h src/ui/MainFrame.cpp
git commit -m "feat(ui): route probe-complete refresh via OnlineProbeFinishedEvent (incremental)"
```

---

## Task 7: 回归与文档

**Files:**
- Test: `tests/test_config_reader.exe`（不相关但确认无回归）
- Modify: `docs/INDEX.md`、`docs/plans/project-plans-tracker.md`

- [ ] **Step 1: 构建全部受影响目标 + 配置测试**

```powershell
cmake --build build --target validproxy validproxy-cli UITests test_config_reader --parallel 8
.\tests\test_config_reader.exe --gtest_brief=1
```

Expected: 构建 0 error；test_config_reader **43/43 PASSED**。

- [ ] **Step 2: 运行时日志冒烟（可选，需 GUI 环境）**

启动 `.\build\validproxy.exe`，观察日志：
- 手动操作（如批量测试）期间：`[OnlineProbe] skipped: user operation running or previous probe still in flight`（DEBUG）
- 手动「测试在线代理」时探活被取消顺序：探活线程退出 → 手动测试开始
- 探活完成无 `refreshResults called`（TRACE 已降级），增量刷新生效

- [ ] **Step 3: 文档登记**

`docs/INDEX.md` 与 `docs/plans/project-plans-tracker.md` 增加 2026-09-16 条目：

```
| 2026-09-16 | feat | docs/specs/2026-09-16-Spec-ProbeNonBlocking-v1.0.md + docs/plans/2026-09-16-Plan-ProbeNonBlocking-Phase1-v1.0.md | 探活不干扰正常操作阶段1 — silent 探活改独立线程（不占 isRunning_，自动任务/配置保存不再被拒）；探活完成 OnlineProbeFinishedEvent 携带受测 indexId → 增量刷新（不再全量 53k 重读，UI 不迟滞）；手动测试优先取消探活；commit 列表见 git log | ✅ completed |
```

- [ ] **Step 4: Commit**

```bash
git add docs/INDEX.md docs/plans/project-plans-tracker.md
git commit -m "docs(pool): register ProbeNonBlocking phase-1 in tracker and INDEX"
```

---

## Self-Review（已执行）

1. **Spec 覆盖**：§3.1（探活独立线程）→ Task 2；§3.2（新事件）→ Task 1/3/6；增量刷新（loadProxyResultsFor + updateResultFor + refreshResultsFor）→ Task 4/5；移除 ONLINE_PROBE_DONE → Task 3/6；析构清理 → Task 2 ✅
2. **占位符**：所有代码步骤给出完整代码；仅 Task 5 Step 2 对 `ProfileExItem` 字段名/`isTestResultValid` 签名给出"以实际定义为准"的修正指引（合理，非占位）。
3. **类型一致性**：`OnlineProbeFinishedEvent::takeIndexIds/getIndexIds`、`updateResultFor`、`loadProxyResultsFor`、`refreshResultsFor` 在 Task 间签名一致 ✅