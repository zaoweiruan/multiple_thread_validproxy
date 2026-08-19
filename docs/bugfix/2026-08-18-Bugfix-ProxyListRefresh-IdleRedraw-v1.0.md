# Bugfix: ProxyListPanel 未启动代理评价空转刷新 + Runtime/Health 语义修正

- 日期: 2026-08-18
- 版本: v1.0
- 影响模块: `src/ui/ProxyListModel.h/.cpp`, `src/ui/ProxyListPanel.cpp`, `tests/test_proxy_list_model.cpp`
- 关联 Spec: `docs/specs/2026-08-18-Spec-ProxyListRefresh-v2.0.md`

## 1. 现象

1. **未启动任何 standalone 代理**时，评价列（Runtime/Health/Starts）仍每 3 秒被强制刷新重绘。
2. **连带发现**：有代理运行期间，Runtime 列数值每次 3 秒刷新后持续膨胀（5s → 15s → 30s …），而非显示当前真实运行时长。
3. **二次修正**：用户明确要求 **Starts/Runtimes 均显示历史累计值**，Health 仅为运算结果；周期刷新不应影响前两列。

## 2. 根因

### Bug A — 空快照仍无条件重绘

周期刷新链路（每 3 秒）：

```
historyTimer_ → refreshHistoryPeriodic() → getRunningDurationsAsync()
  → 后台查询 proxy_runtime_history WHERE ended_at IS NULL
  → wxQueueEvent(RunningDurationsLoadedEvent)
  → ProxyListPanel::onRunningDurationsLoaded()
```

`onRunningDurationsLoaded()` 收到**空 map**（无 running 会话）时仍无条件执行：

```cpp
model_->setRunningDurations(event.takeDurations());
model_->notifyHistoryChanged();   // 遍历所有 start_count>0 行 ValueChanged
listCtrl_->Refresh();             // 强制整个 DataViewCtrl 重绘
```

即使模型数据**没有任何变化**，UI 也每 3 秒被强制重绘 → 未启动代理时评价列仍在"刷新显示"。

### Bug B — setRunningDurations 累加而非覆盖

`getRunningDurations()` / `getRunningDurationsAsync()` 返回的是 `proxy_runtime_history.duration_ms`（watch 心跳累计的**当前总运行时长**，绝对值）。但旧版 `ProxyListModel::setRunningDurations()` 中：

```cpp
runtimeMap_[it->first] += it->second;   // 累加！
```

- 第 1 次刷新: `total + 5s`     ✅
- 第 2 次刷新: `total + 5s + 10s`  ❌（应为 total + 10s）
- 第 3 次刷新: `total + 15s + 15s` ❌（应为 total + 15s）

Runtime 列每 3 秒翻倍膨胀（healthMap_ 是覆盖式重算，正确）。

### Bug C — notifyHistoryChanged 全量通知（设计缺陷）

`notifyHistoryChanged()` 通知所有 `start_count>0` 的行（含未运行代理）。周期刷新本应只通知 running 的行，但旧实现未区分。

## 3. 修复

### ProxyListModel

- 新增成员 `runningDurations_`（当前 in-progress 会话快照），与 `runtimeMap_`（历史累计 total）分离。
- `setRunningDurations()` 改为**整体替换**语义，返回 `bool changed`（快照是否变化）：

```cpp
bool ProxyListModel::setRunningDurations(
    const std::unordered_map<std::string, long long>& runningMs) {
    bool changed = (runningDurations_ != runningMs);
    runningDurations_ = runningMs;
    if (!changed) return false;   // 无变化（含恒空）→ 调用方可跳过重绘
    // … healthMap_ 覆盖式重算 base + bonus（原逻辑保留）
    return true;
}
```

- `getRuntime()` 与 `GetValueByRow(COL_TOTAL_RUNTIME_MS)` 返回纯 `runtimeMap_[id]`（历史累计，不含 running live）。
- `clear()` 同步清空 `runningDurations_`。
- 新增 `notifyRunningChanged()`：仅通知 `runningDurations_` 中存在的行的 **COL_HEALTH**（唯一随 running 变化的列）。

### ProxyListPanel

`onRunningDurationsLoaded()` 仅在快照变化时才通知并重绘，且使用 `notifyRunningChanged()`：

```cpp
if (model_->setRunningDurations(event.takeDurations())) {
    model_->notifyRunningChanged();   // 仅 running 行的 Health
    listCtrl_->Refresh();
}
refreshInFlight_ = false;
```

`refreshResults()`（低频全量刷新，代理启停/测试完成触发）仍保留 `notifyHistoryChanged()` 全量通知，合理。

### 列语义（最终）

| 列 | 数据来源 | 是否历史累计 | 周期刷新是否更新 |
|---|---|---|---|
| **Starts** | `startCountMap_` ← `ex.start_count` | ✅ 是 | ❌ 不更新 |
| **Runtimes** | `runtimeMap_` ← `ex.total_runtime_ms` | ✅ 是 | ❌ 不更新 |
| **Health** | `healthMap_` 每次 `setRunningDurations()` 覆盖重算 | ❌ 纯运算结果 | ✅ 仅 running 行 |

## 4. 验证

- TDD 回归测试（`tests/test_proxy_list_model.cpp`）：
  - `RepeatedRefreshDoesNotAccumulateRuntime` — 相同快照幂等、空快照后 Runtime 仍为纯历史
  - `EmptySnapshotReportsNoChange` — 空快照返回 false（UI 可跳过重绘）
  - `RunningSessionsMergeRuntimeAndHealthBonus` — Runtime 保持纯历史，Health bonus 正确
  - `LongRunningHealthCappedAtOne` — 60 分钟 bonus 饱和 1.0，Runtime 仍为 0
- `test_proxy_list_model` **6/6 PASSED**
- `ctest` 29/30 通过；`NetworkMonitorTest.LoggingOnConnectionLost` 为**环境依赖失败**（`https://www.example.com` 当前不可达，与本次无关）

## 5. 涉及文件

| 文件 | 变更 |
|------|------|
| `src/ui/ProxyListModel.h` | `setRunningDurations` 返回 `bool`；新增 `notifyRunningChanged()` 声明 |
| `src/ui/ProxyListModel.cpp` | `setRunningDurations` 整体替换语义；`getRuntime`/`GetValueByRow` 纯历史累计；`notifyRunningChanged` 仅 COL_HEALTH；`clear()` 清 `runningDurations_` |
| `src/ui/ProxyListPanel.cpp` | `onRunningDurationsLoaded` 改用 `notifyRunningChanged` |
| `tests/test_proxy_list_model.cpp` | 4 项现有测试 Runtime 期望改为纯历史；2 项新增回归测试 |
