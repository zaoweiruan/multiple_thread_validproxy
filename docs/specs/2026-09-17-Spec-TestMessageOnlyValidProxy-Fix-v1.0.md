---
doc_type: Spec
date: 2026-09-17
module: ProfileExItemDAO
version: v1.0
status: completed
scope: src/ProfileExItemDAO.cpp
risk: low
---

# Spec — 修复测试/批量测试时仅对有效代理更新 message 测试时间侧

## 1. 背景

用户报告：测试或批量测试时，应**只对有效代理**更新 `ProfileExItem.message` 的测试时间侧。

`ProfileExItem.message` 双时间戳语义（2026-08-11 Spec-ProfileExMessage）：`<测试成功时间>+<启动时间>`，测试时间恒在前，`+` 分隔。仅当代理**测试成功且延迟有效**时，才应刷新测试时间侧；失败或延迟无效的代理不应刷新。

## 2. 根因

`ProfileExItemDAO::updateTestResult` L274 与 `updateTestResultBatch` L404 均用 `if (success)` 判断是否更新 message 测试时间侧：

```cpp
if (success) {
    message = formatTestMessage(existingMessage, currentTimeString());
} else {
    message = existingMessage;
}
```

`success=true` 但 `latencyMs<=0`（如超时返回 0、异常返回 -1）的代理，仍会被写入测试时间，造成「延迟无效但 message 显示刚测试成功」的误导。

「有效代理」的既有定义见 `utils::isTestResultValid(bool success, long latencyMs)`（`src/Utils.cpp:584-586`）：

```cpp
return success && latencyMs > 0;
```

该方法已用于 `delayStr` 计算（`updateTestResult` L290、`updateTestResultBatch` L417），但**未用于 message 判断**，导致两处判定口径不一致。

## 3. 目标 / 非目标

### 目标

1. `updateTestResult` 与 `updateTestResultBatch` 中，仅当 `utils::isTestResultValid(success, latencyMs)` 为 true（即 `success && latencyMs > 0`）时刷新 message 测试时间侧。
2. 与 `delayStr` 的 `isTestResultValid` 判定口径一致。

### 非目标

- 不修改 `utils::isTestResultValid` 定义。
- 不修改 `formatTestMessage` 逻辑（仍保留启动时间侧）。
- 不修改 `consecutive_failures`、`start_count`、`total_runtime_ms`、`crash_count` 等其它字段更新逻辑。
- 不修改 `updateStartupTime`（仅刷新启动时间侧，不受影响）。

## 4. 实现方案

将 `if (success)` 改为 `if (utils::isTestResultValid(success, latencyMs))`，两处：

### 4.1 `updateTestResult` L274

```cpp
if (utils::isTestResultValid(success, latencyMs)) {
    message = formatTestMessage(existingMessage, currentTimeString());
} else {
    // 保留原 message（含失败、延迟无效、未通过验证三种情况）；
    // 失败时重置评价历史列。
    message = existingMessage;
    if (!success) {
        histStartCount = 0;
        histRuntimeMs = 0;
        histCrashCount = 0;
        if (!curlMsg.empty()) {
            Logger::write("[ProfileExItem] test failed for " + indexid + ": " + curlMsg, LogLevel::DEBUG);
        }
    }
}
```

### 4.2 `updateTestResultBatch` L404

```cpp
if (utils::isTestResultValid(success, latencyMs)) {
    message = formatTestMessage(existingMessage, currentTimeString());
} else {
    message = existingMessage;
    if (!success) {
        histStartCount = 0;
        histRuntimeMs = 0;
        histCrashCount = 0;
        if (!curlMsg.empty()) {
            Logger::write("[ProfileExItem] test failed for " + indexid + ": " + curlMsg, LogLevel::DEBUG);
        }
    }
}
```

## 5. 边界与兼容性

- **success=true 且 latencyMs>0**：刷新测试时间侧（与旧行为一致）。
- **success=true 且 latencyMs<=0**：保留原 message（旧行为刷新，新行为不刷新）。`delayStr` 仍为 `-1`（旧行为已如此），评价历史列**保留原值**（旧行为重置，新行为保留——因为代理并非失败，只是延迟无效，不应清零历史）。
- **success=false**：保留原 message + 重置评价历史列（与旧行为一致）。
- **新代理（无 existingMessage）**：首次成功测试时 `existingMessage=""`，`formatTestMessage("", now)` 写入 `<测试时间>+`（与旧行为一致）。
- **DB 切换**：`db_` 指针由调用方传入，与旧行为一致。
- **C++17 无 `auto`**：所有变量显式类型。

## 6. 验证计划

1. `cmake --build build --parallel 8` 增量编译通过（零 error）。
2. `ctest -R test_profile_ex_item -V`（若存在相关单测）回归通过。
3. 手动验证：批量测试一批代理，确认 `success=true 但 latencyMs<=0` 的代理 message 未被刷新，`success=true 且 latencyMs>0` 的代理 message 被刷新。

## 7. 变更清单

| 文件 | 变更 |
| :--- | :--- |
| `src/ProfileExItemDAO.cpp` | `updateTestResult` L274 与 `updateTestResultBatch` L404 的 `if (success)` 改为 `if (utils::isTestResultValid(success, latencyMs))`；`!success` 分支嵌套在 else 内，仅失败时重置评价历史列 |
| `docs/INDEX.md` §8.2 | 新增 2026-09-17 本 spec 条目 |
| `docs/plans/project-plans-tracker.md` | 近期文档引用表新增 2026-09-17 条目 |
| `docs/specs/2026-09-17-Spec-TestMessageOnlyValidProxy-Fix-v1.0.md` | 本 spec（新建） |
