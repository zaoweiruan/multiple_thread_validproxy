---
title: "Adjust profile progress/inserted log level from INFO to REPORT"
type: feat
status: completed
date: 2026-05-13
origin: "User request: 将[INFO] INFO: Inserted 0 new profiles, skipped duplicates、[INFO] Progress: 1200/1260 profiles processed 日志级别调整为report"
---

# Adjust profile progress/inserted log level from INFO to REPORT

## 问题描述

`SubitemUpdaterV2::updateProfileItems()` 方法中有两条进度/统计日志消息目前使用 `LogLevel::INFO`：

1. **Progress 日志** — 每处理 100 条 profile 输出一次处理进度
2. **Inserted 日志** — 处理完成后输出新增 vs 去重跳过的统计

根据项目日志等级规范（`docs/plans/DEV-PROCESS.md`），`REPORT` 级别专用于统计汇总、进度指标等输出。这两条消息属于批量操作中的进度指示和统计汇总，应使用 `LogLevel::REPORT`。

## 范围边界

- 修改：`src/SubitemUpdaterV2.cpp` — 两条 Logger::write 调用的日志级别
- NOT 修改：其他文件、其他日志调用

## 详细变更

### U1: `src/SubitemUpdaterV2.cpp` 第 969 行 — Progress 日志

**当前：**
```cpp
Logger::write("Progress: " + std::to_string(count) + "/" + std::to_string(profiles.size()) + " profiles processed", LogLevel::INFO);
```

**目标：**
```cpp
Logger::write("Progress: " + std::to_string(count) + "/" + std::to_string(profiles.size()) + " profiles processed", LogLevel::REPORT);
```

> 消息无 `"INFO: "` 前缀，仅改日志级别。

### U2: `src/SubitemUpdaterV2.cpp` 第 1052 行 — Inserted 统计日志

**当前：**
```cpp
Logger::write("INFO: Inserted " + std::to_string(inserted) + " new profiles, skipped duplicates", LogLevel::INFO);
```

**目标：**
```cpp
Logger::write("Inserted " + std::to_string(inserted) + " new profiles, skipped duplicates", LogLevel::REPORT);
```

> 同时去掉 `"INFO: "` 前缀，因为 `Logger::write` 会自动添加 `[REPORT]` 标签。

## 验证步骤

1. 构建验证：`cmake --build build --parallel 8`
2. 运行 `ctest -V` 确认无回归

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/SubitemUpdaterV2.cpp` | 修改（2行） | 第 969 行 `LogLevel::INFO` → `LogLevel::REPORT`；第 1052 行 `LogLevel::INFO` → `LogLevel::REPORT` + 移除 `"INFO: "` 前缀 |

## 风险

| 风险 | 缓解措施 |
|------|----------|
| 外部工具或日志解析依赖这些日志格式 | 仅级别和前缀变化，消息内容不变，风险低 |
