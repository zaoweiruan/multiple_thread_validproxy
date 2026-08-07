# Bugfix: 订阅更新跳过提示日志级别由 ERROR 降为 REPORT

| 项 | 值 |
| :--- | :--- |
| **日期** | 2026-08-07 |
| **模块** | SubitemUpdaterV2（`src/SubitemUpdaterV2.cpp` / `include/SubitemUpdaterV2.h`） |
| **严重程度** | 低（日志语义调整，无功能影响） |
| **状态** | 已修复 |

## 问题描述

`SubitemUpdaterV2::updateAll()` 在「有启用订阅但全部因更新间隔（update interval）被跳过」时，以 **ERR** 级别输出：

```
[ERROR] All subscriptions skipped by update interval - nothing to update
```

该场景并非错误：所有订阅处于更新间隔内、无需更新，属于正常业务流程的提示信息。以 ERR 级别记录会造成日志告警噪音，并可能被监控/告警系统误判为故障。

## 影响范围

- 仅 `src/SubitemUpdaterV2.cpp` L319 一处日志级别调整。
- 相邻 L323 `"All subscriptions failed to update - check network connectivity"`（真正失败）保持 ERR **不变**。
- 无测试引用该消息文本（已 grep `tests/` 确认），测试无需修改。
- 历史文档 `docs/bugfix/2026-06-15-Bugfix-AutoTask-SubitemUpdater-logging-and-pipeline.md` L37 记载了该消息最初以 ERR 级别引入，属历史记录，不改写。

## 根因

原实现将「被更新间隔跳过」归类为失败分支（`successCount <= 0` 且 `attemptedCount == 0`），统一以 ERR 输出；但该分支实际是**预期的跳过行为**（函数随后 `return true` 表示正常完成），语义上应归入 REPORT 级汇总信息。

## 修复内容

`src/SubitemUpdaterV2.cpp` L317-325：

```cpp
if (successCount <= 0) {
    if (!enabledSubs.empty() && attemptedCount == 0) {
        // 原 LogLevel::ERR → LogLevel::REPORT（被更新间隔跳过，非错误）
        Logger::write("All subscriptions skipped by update interval - nothing to update", LogLevel::REPORT);
        return true;
    }
    if (!enabledSubs.empty()) {
        // 真正失败：保持 ERR 不变
        Logger::write("All subscriptions failed to update - check network connectivity", LogLevel::ERR);
    }
}
return successCount > 0;
```

### 级别可见性说明

Logger 级别枚举：`TRACE=0 < DEBUG=1 < INFO=2 < REPORT=3 < WARN=4 < ERR=5`。
生产配置 `console_level=WARN`、`file_level=ERROR` 下，REPORT(3) 低于两个阈值，该提示将不再输出到控制台与文件——与「这不是错误」的语义一致（错误不再被误报）；若需可见，可将 console_level 调至 REPORT 或更低。

## 验证

1. 构建：`cmake --build build --parallel 8`（产物 `bin\validproxy.exe` / `bin\validproxy-cli.exe`）。
2. 全量测试：`cmake --build build --target test --parallel 8`（内含 ctest，基线 21/21）。
3. 源码确认：`src/SubitemUpdaterV2.cpp` L319 为 `LogLevel::REPORT`。

## 配置文件结构表（日志相关字段）

| 字段 | 当前生产值 | 说明 |
| :--- | :--- | :--- |
| `log.enabled` | `true` | 日志总开关 |
| `log.network_failures` | `true` | 网络失败日志降级开关 |
| `log.console_level` | `WARN` | 控制台输出最低级别 |
| `log.file_level` | `ERROR` | 文件写入最低级别 |

## 后续项

无。
