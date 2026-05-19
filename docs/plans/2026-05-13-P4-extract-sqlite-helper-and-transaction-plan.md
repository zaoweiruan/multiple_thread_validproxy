---
title: "refactor: Extract sqlite::exec helper and sqlite::Transaction RAII guard"
type: refactor
status: cancelled
date: 2026-05-13
origin: "Code audit P4 — consolidate sqlite3_exec error handling and fix COMMIT→ROLLBACK bug in transaction error paths"
---

# P4: Extract sqlite::exec helper and sqlite::Transaction RAII guard

## 问题描述

### U1: `sqlite3_exec` 错误处理不统一（跨文件）

当前存在三种处理方式：

| 模式 | 位置 | 数量 |
|------|------|------|
| 静默忽略（全 nullptr） | `main.cpp:735` PRAGMA | 1 处 |
| 手动 `char* errMsg` + Logger + free | SubitemUpdaterV2::deduplicate/Phase0/1/cleanup | ~9 处 |
| `execSql()` 私有方法 | SubitemUpdaterV2 类内 | 1 处定义，4 处调用 |

没有通用函数可用 —— `execSql` 是 `SubitemUpdaterV2` 的私有方法，其他类无法复用。`DatabaseHelper::execute()` 存在但使用 `std::cerr` 而不是项目的 `Logger`，且零引用。

### U2: 事务管理无 RAII 守卫

`updateProfileItems` 和 `deduplicate` 中事务是手动管理的，有多个提前返回路径。

**Bug**: `updateProfileItems` 中 prepare 失败路径使用 `COMMIT` 而不是 `ROLLBACK`（行 954/962/971）：

```cpp
if (sqlite3_prepare_v2(db_, dedupSql, -1, &checkStmt, nullptr) != SQLITE_OK) {
    Logger::write("ERROR: ...", LogLevel::ERR);
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);  // ❌ 应为 ROLLBACK
    return false;
}
```

prepare 失败时事务应回滚而非提交。虽然该路径极罕见（仅 SQL 编译失败时触发）且不影响数据完整性（COMMIT 空事务无实际效果），但语义错误，应修正。

## 范围边界

- 修改: `include/` 下新增 `SqliteHelper.h`
- 修改: `include/SubitemUpdaterV2.h` — 移除 `execSql` 私有声明
- 修改: `src/SubitemUpdaterV2.cpp` — 替换 execSql 调用 + 用 RAII 事务替换手动事务管理
- 修改: `src/main.cpp` — 替换 1 处静默 `sqlite3_exec`(PRAGMA)
- NOT 修改: 已有的手动 `char* errMsg` 调用（deduplicate/Phase0/1/cleanup）— 已正确处理错误，替换收益不足以抵消风险
- NOT 修改: `DatabaseHelper.h`（留作死代码清理的后续计划）
- NOT 修改: Logger 或其他基础设施

## 详细变更

### U1: 创建 `include/SqliteHelper.h`

新增文件，包含 2 个组件：

```cpp
#pragma once
#ifndef SQLITE_HELPER_H
#define SQLITE_HELPER_H

#include <sqlite3.h>
#include <string>
#include "Logger.h"

namespace sqlite {

// ═══════════════════════════════════════════
// exec() — 带错误日志的 sqlite3_exec
// ═══════════════════════════════════════════
inline bool exec(sqlite3* db, const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("SQL exec error: " + std::string(errMsg), LogLevel::ERR);
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════
// execSilent() — 不关心返回值的调用（如 PRAGMA）
// ═══════════════════════════════════════════
inline void execSilent(sqlite3* db, const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        Logger::write("SQL exec (ignored): " + std::string(errMsg), LogLevel::WARN);
        sqlite3_free(errMsg);
    }
}

// ═══════════════════════════════════════════
// Transaction — RAII 事务守卫
// ═══════════════════════════════════════════
class Transaction {
    sqlite3* db_;
    bool committed_ = false;
public:
    explicit Transaction(sqlite3* db) : db_(db) {
        if (!exec(db, "BEGIN TRANSACTION")) {
            throw std::runtime_error("Failed to begin transaction");
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    ~Transaction() noexcept {
        if (!committed_) {
            execSilent(db_, "ROLLBACK");
        }
    }

    void commit() {
        if (exec(db_, "COMMIT")) {
            committed_ = true;
        }
    }
};

} // namespace sqlite
#endif
```

### U2: 修改 `include/SubitemUpdaterV2.h`

- 移除 `bool execSql(const std::string& sql, const std::string& errorContext);` 私有方法声明

### U3: 修改 `src/SubitemUpdaterV2.cpp`

#### U3a: 移除 `execSql` 实现，替换 4 处调用

移除整个 `execSql` 方法体（约行 340-350）。4 处调用点改为：

```cpp
// Before
execSql(updateSql, "[SubitemUpdaterV2] SQL exec failed");

// After
sqlite::exec(db_, updateSql);
```

#### U3b: 用 `sqlite::Transaction` 替换 `updateProfileItems` 中手动事务

```cpp
// Before (行 928 + 954/962/971 + 1061)
sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
// ... prepare 失败路径用 COMMIT 代替 ROLLBACK ...
sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);

// After
sqlite::Transaction tx(db_);
// ... prepare 失败时直接 return — tx 析构自动 ROLLBACK ...
tx.commit();
```

#### U3c: 用 `sqlite::Transaction` 替换 `deduplicate` 中手动事务

```cpp
// Before (行 1817-1843)
char* errMsg = nullptr;
if (sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
    Logger::write("ERROR: ...", LogLevel::ERR);
    sqlite3_free(errMsg);
    return false;
}
// ... phases ...
if (sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
    Logger::write("ERROR: ...", LogLevel::ERR);
    sqlite3_free(errMsg);
    sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
    return false;
}

// After
sqlite::Transaction tx(db_);
// ... phases ...
tx.commit();
```

### U4: 修改 `src/main.cpp`

行 735：
```cpp
// Before
sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);

// After
sqlite::execSilent(db, "PRAGMA journal_mode=WAL");
```

## 分步执行

| 步骤 | 操作 | 文件 | 预计变更行 |
|------|------|------|-----------|
| 1 | 创建 `SqliteHelper.h` | 新增 | +65 行 |
| 2 | 移除 `execSql` 声明 | `SubitemUpdaterV2.h` | -1 行 |
| 3 | 移除 `execSql` 实现 + 替换调用 | `SubitemUpdaterV2.cpp` | -8 / +4 行 |
| 4 | `updateProfileItems` 事务替换 | `SubitemUpdaterV2.cpp` | ~10 行改动 |
| 5 | `deduplicate` 事务替换 | `SubitemUpdaterV2.cpp` | ~10 行改动 |
| 6 | main.cpp PRAGMA 替换 | `main.cpp` | 1 行 |

## 验证步骤

1. `cmake --build build --parallel 8` — 0 errors
2. `ctest -V` — 所有测试通过
3. 运行 `.\bin\validproxy.exe -h` — 正常
4. 运行 `.\bin\validproxy.exe -D` — 去重功能正常
5. 运行 `.\bin\validproxy.exe -show-sub` — 正常

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `include/SqliteHelper.h` | **新增** | `sqlite::exec`, `sqlite::execSilent`, `sqlite::Transaction` |
| `include/SubitemUpdaterV2.h` | 修改 | 移除 `execSql` 私有方法声明 |
| `src/SubitemUpdaterV2.cpp` | 修改 | 移除 execSql 实现，替换 4 处调用，2 处事务 RAII 化 |
| `src/main.cpp` | 修改 | 替换 1 处静默 sqlite3_exec |
| `docs/plans/project-plans-tracker.md` | 修改 | 更新进度 |

## 风险

| 风险 | 缓解措施 |
|------|----------|
| `Transaction` 析构中 ROLLBACK 可能抛异常 | 使用 `noexcept` + `execSilent`（不抛） |
| `std::runtime_error` 在 BEGIN 失败时传播 | 现调用点无 try-catch；BEGIN 失败极罕见（仅 I/O 错误） |
| `execSilent` 将错误降级为 WARN 而非 ERR | 符合语义——调用方表明不关心返回值的调用不应产生 ERR |
