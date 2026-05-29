---
title: "Adjust subscription update processing log level from INFO to REPORT"
type: feat
status: completed
date: 2026-05-13
origin: "User request: 更新订阅时，将开始每个订阅更新的日志级别调整为report"
---

# Adjust subscription update processing log level from INFO to REPORT

## 问题描述

在批量更新订阅时，`SubitemUpdaterV2::run()` 方法会逐条输出每个订阅开始处理的日志，但目前使用的是 `LogLevel::INFO` 级别。根据项目日志等级规范（`docs/plans/DEV-PROCESS.md`），`REPORT` 级别专用于统计汇总、进度指标等输出，而 `INFO` 用于常规流程、生命周期事件。

订阅更新过程中的进度指示（"第 N/M 个" + "Processing" / "Trying via proxy"）本质上属于操作进度指标，使用 `REPORT` 级别更合适，与更新摘要输出保持一致。

## 范围边界

- 修改：`src/SubitemUpdaterV2.cpp` — 两条 Logger::write 调用的日志级别
- NOT 修改：`include/Logger.h`（REPORT 已存在）、`src/Logger.cpp`、其他文件中的日志调用
- NOT 修改：`runSingle()`、`runSingleWithProxy()` 中的单条订阅更新日志（保留 INFO）

## 详细变更

### U1: `src/SubitemUpdaterV2.cpp` 第 165 行 — Direct Phase 进度日志

**当前：**
```cpp
Logger::write("INFO: [" + std::to_string(i + 1) + "/" + std::to_string(totalSubs) + "] Processing: " + sub.url, LogLevel::INFO);
```

**目标：**
```cpp
Logger::write("[" + std::to_string(i + 1) + "/" + std::to_string(totalSubs) + "] Processing: " + sub.url, LogLevel::REPORT);
```

> 同时去掉 `"INFO: "` 前缀，因为 `Logger::write` 会自动添加 `[REPORT]` 标签。

### U2: `src/SubitemUpdaterV2.cpp` 第 211 行 — Proxy Phase 进度日志

**当前：**
```cpp
Logger::write("INFO: [" + std::to_string(i + 1) + "/" + std::to_string(failedSubs.size()) + "] Trying via proxy: " + std::get<2>(sub), LogLevel::INFO);
```

**目标：**
```cpp
Logger::write("[" + std::to_string(i + 1) + "/" + std::to_string(failedSubs.size()) + "] Trying via proxy: " + std::get<2>(sub), LogLevel::REPORT);
```

> 同时去掉 `"INFO: "` 前缀。

## 验证步骤

1. 构建验证：`cmake --build build --parallel 8`
2. 运行更新订阅功能，确认控制台输出 `[REPORT] [1/N] Processing: ...` 格式
3. 确认日志文件中同样记录 `[REPORT]` 前缀
4. 确认其他 INFO 级别日志不受影响

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/SubitemUpdaterV2.cpp` | 修改（2行） | 将第 165、211 行的 `LogLevel::INFO` 改为 `LogLevel::REPORT`，同时移除消息中的 `"INFO: "` 前缀 |

## 风险

| 风险 | 缓解措施 |
|------|----------|
| 外部工具或日志解析依赖 `[INFO]` 前缀的进度日志 | 变为 `[REPORT]` 后过滤规则需同步更新；风险低，因进度日志不属 API 契约 |
| 如果 `console_level` 配置高于 REPORT（当前默认 INFO=2，REPORT=5），进度日志会消失 | 当前默认配置不会出现此问题；配置变更导致的日志缩减为用户自主行为 |
