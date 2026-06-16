---
title: "AutoTask — 自动任务管道设计规格"
type: spec
status: draft
version: 1.0
date: 2026-06-15
---

# AutoTask — 自动任务管道规格 v1.0

## 1. 概述

将更新订阅、默认测试、同步、去重等功能聚合成一个可配置的自动任务管道，支持中断续跑。

## 2. 组件

### AutoTaskManager (`include/AutoTaskManager.h`, `src/AutoTaskManager.cpp`)

| 组件 | 职责 |
|------|------|
| AutoTaskManager | 核心调度器，串行执行步骤列表，维护状态文件 |
| AutoTaskStepInfo | 单个步骤的元数据（类型、状态、错误信息） |
| AutoTaskState | 断点状态（当前步骤索引、各步骤状态、时间戳） |

### 步骤类型

| 步骤 | 操作 | 对应 CLI 模式 |
|------|------|--------------|
| UPDATE_ALL | SubitemUpdaterV2::run() | -UA |
| TEST_ALL | ProxyBatchTester::run() | -TA |
| DEDUP | SubitemUpdaterV2::deduplicate() | -D |
| SYNC | SubitemUpdaterV2::syncDatabases() | -S |
| EXPORT | 导出有效代理为分享链接 | -TU |

### 配置 (`config.json` -> AppConfig)

```json
{
  "auto_task": {
    "enabled": false,
    "steps": ["update", "test", "dedup", "sync"],
    "sync_source": "",
    "sync_target": "",
    "resume_on_restart": true,
    "notify_on_complete": true,
    "state_file": "worker/autotask_state.json"
  }
}
```

### 断点状态文件

```json
{
  "version": 1,
  "task_id": "uuid",
  "created_at": "2026-06-15T09:00:00",
  "current_step_index": 2,
  "steps": [
    {"name": "update", "status": "completed", "started_at": "...", "completed_at": "..."},
    {"name": "test", "status": "completed", ...},
    {"name": "dedup", "status": "running", "started_at": "...", "completed_at": ""},
    {"name": "sync", "status": "pending", ...}
  ],
  "completed": false,
  "cancelled": false
}
```

## 3. 中断与续跑

1. 每个步骤执行前标记为 RUNNING 并写入状态文件
2. 步骤完成后标记 COMPLETED，向前推进 current_step_index
3. 收到取消信号 → 当前步骤走完取消流程 → 状态标记 CANCELLED，写入文件
4. 重启后检测状态文件 → 找到 CANCELLED 状态 → 询问用户是否续跑
5. 续跑时跳过 COMPLETED 步骤，从 current_step_index 开始

## 4. CLI 集成

```
-AT, --auto-task     执行自动任务管道（从 config 读取 steps 配置）
-ATR, --auto-task-resume  续跑上次中断的自动任务
```

## 5. UI 集成（后续版本）

## 6. 文件变更清单

| 文件 | 操作 |
|------|------|
| `include/AutoTaskManager.h` | 新增 |
| `src/AutoTaskManager.cpp` | 新增 |
| `include/ConfigReader.h` | 修改 (AppConfig) |
| `src/ConfigReader.cpp` | 修改 (parse/save auto_task) |
| `src/main_cli.cpp` | 修改 (-AT / -ATR) |
| `CMakeLists.txt` | 修改 (add sources + test) |
| `tests/test_autotask.cpp` | 新增 |
