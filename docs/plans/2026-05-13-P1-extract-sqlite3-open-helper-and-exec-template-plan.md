---
title: "refactor: Extract sqlite3_open helper and sqlite3_exec+errMsg template"
type: refactor
status: completed
date: 2026-05-13
completed: 2026-05-13
origin: "Code audit P1 — eliminate 8 sqlite3_open duplicates in main.cpp + 4 sqlite3_exec+errMsg duplicates in SubitemUpdaterV2.cpp"
---

# P1: Extract sqlite3_open helper and sqlite3_exec+errMsg template

## 问题描述

### U1: sqlite3_open 重复（main.cpp）

`src/main.cpp` 中有 8 处完全相同的 `sqlite3_open` 调用模式：

```cpp
if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
    Logger::write("Failed to open database: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
    return 1;
}
```

行位: 228, 283, 378, 445, 575, 622, 659, 735

### U2: sqlite3_exec + errMsg 重复（SubitemUpdaterV2.cpp）

`src/SubitemUpdaterV2.cpp` 中有 4 处相同的 `sqlite3_exec` + `errMsg` 错误处理模式：

```cpp
char* errMsg = nullptr;
if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
    Logger::write("...: " + std::string(errMsg), LogLevel::ERR);
    sqlite3_free(errMsg);
}
```

行位: 1297, 1353, 1406, 1421（以及 1369 的 errMsg2 变体）

## 执行结果

- **U1** ✅ openDatabase 辅助函数已提取（`main.cpp:58-64`），8 处重复已消除
- **U2** ❌ 经评估，sqlite3_exec+errMsg 各处的错误消息内容不同，提取模板会损失日志精确性，决定保持现状

## 范围边界

- U1: 仅修改 `src/main.cpp` — 添加辅助函数，替换 8 处内联代码
- U2: 已评估决定不执行（各 errMsg 错误消息内容不同，提取模板会损失日志精确性）
- NOT 修改: 其他文件、其他数据库操作

## 详细变更

### U1: 提取 openDatabase 辅助函数 (main.cpp)

在 `src/main.cpp` 顶部附近添加：

```cpp
static bool openDatabase(const AppConfig* config, sqlite3*& db, const std::string& context) {
    if (sqlite3_open(config->database_path.c_str(), &db) != SQLITE_OK) {
        Logger::write(context + " - Failed to open database: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
        return false;
    }
    return true;
}
```

替换 8 处:
```cpp
// before:
if (sqlite3_open(appConfig->database_path.c_str(), &db) != SQLITE_OK) {
    Logger::write("Failed to open database: " + std::string(sqlite3_errmsg(db)), LogLevel::ERR);
    return 1;
}

// after:
if (!openDatabase(appConfig, db, "[main] show-sub")) {
    return 1;
}
```

每个调用处使用不同的 context 字符串标识调用位置。

### U2: 提取 execSql 辅助函数

在 `include/SubitemUpdaterV2.h` 中添加私有方法声明，在 `src/SubitemUpdaterV2.cpp` 中实现。

可选方案: Lambda 函数在 `updateProfileItems()` 方法内复用，或提取为私有方法。

## 验证步骤

1. `cmake --build build --parallel 8` — 0 errors
2. `ctest -V` — 所有测试通过
3. `.\bin\validproxy.exe -h` — 功能正常

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/main.cpp` | 修改 | 添加 openDatabase 辅助函数 + 替换 8 处调用 |

## 验证结果
- ✅ 构建 0 errors
- ✅ ctest 全部通过

## 风险

| 风险 | 缓解措施 |
|------|----------|
| 辅助函数可能影响内联优化 | 编译器会内联静态/短小函数，性能无差异 |
| context 字符串维护成本 | 字符串在调用处内联，无额外维护负担 |
