---
date: 2026-07-28
title: "Bugfix: Database Index Missing on New Database — Centralize applyPragmas"
type: bugfix
module: DatabaseConnectionService / main_gui / UIApp / main_cli
version: 1.0
status: completed
---

# Bugfix: Database Index Missing on New Database — Centralize applyPragmas

## 问题描述

新建数据库时，`idx_profile_dedup` 复合索引不会被创建。该索引定义于 `DatabaseConnectionService::applyPragmas()`（`src/service/DatabaseConnectionService.cpp:31`），但三个数据库打开代码路径未调用 `applyPragmas()`：

```sql
CREATE INDEX IF NOT EXISTS idx_profile_dedup
ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network))
```

### 影响路径

| # | 文件 | 问题 |
|---|------|------|
| 1 | `src/main_gui.cpp:87` | 原始 `sqlite3_open_v2()` 直接调用，无任何 pragma 应用 |
| 2 | `src/ui/UIApp.cpp:76` | detached launch 路径，原始 `sqlite3_open_v2()` 无 pragma |
| 3 | `src/main_cli.cpp:62-72` | `openDatabase()` 辅助函数仅应用 6 条基础 pragma，缺少索引创建 |

### 已正确调用的路径

- `AppController` — 接收 db 指针，`applyPragmas()` 由 `DatabaseConnectionService::open()` 完成
- `UIApp`（非 detached 路径）— 接收外部 db 指针，已由上游完成
- `SubitemUpdaterV2` — 接收 db 指针，已调用 `applyPragmas()`

## 修复方案

将三个问题路径统一接入 `service::DatabaseConnectionService`：

### 1. `src/main_gui.cpp`

- 添加 `#include "service/DatabaseConnectionService.h"`
- 原 `sqlite3_open_v2()` 替换为 `service::DatabaseConnectionService::open()`
- 错误检查从 `rc != SQLITE_OK` 改为 `!db`（nullptr 检查）

### 2. `src/ui/UIApp.cpp`

- 添加 `#include "service/DatabaseConnectionService.h"`
- detached launch 路径的 `sqlite3_open_v2()` 替换为 `service::DatabaseConnectionService::open()`
- 错误检查从 `rc != SQLITE_OK` 改为 `!db_`，移除手动 `sqlite3_close(db_)`

### 3. `src/main_cli.cpp`

- 添加 `#include "service/DatabaseConnectionService.h"`
- `openDatabase()` 辅助函数中 7 条手动 pragma 替换为单行 `service::DatabaseConnectionService::applyPragmas(db)`
- 验证 `applyPragmas()` 包含所有相同 6 条基础 pragma + `CREATE INDEX IF NOT EXISTS idx_profile_dedup`

## 验证

- **编译**: 3 个修改文件均编译通过
- **链接**: `validproxy.exe`（GUI）和 `validproxy-cli.exe`（CLI）链接成功
- **测试**: 18/18 测试套件全部通过（0 failures）

## 相关文件

- `src/service/DatabaseConnectionService.cpp` — `applyPragmas()` 定义
- `include/service/DatabaseConnectionService.h` — 接口头文件
- `src/main_gui.cpp` — GUI 入口
- `src/ui/UIApp.cpp` — wxApp 初始化
- `src/main_cli.cpp` — CLI 入口
