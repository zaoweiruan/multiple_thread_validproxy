---
title: "fix: Fix silent sqlite3_exec error discard and unreachable sqlite3_finalize"
type: fix
status: completed
date: 2026-05-13
completed: 2026-05-13
origin: "Code audit P0 — silent error discard in SubitemUpdaterV2 + unreachable finalize in main"
---

# P0: Fix silent sqlite3_exec error discard and unreachable sqlite3_finalize

## 问题描述

### U1: 静默丢弃 sqlite3_exec 错误（SubitemUpdaterV2.cpp）

`SubitemUpdaterV2::updateProfileItems()` 中有 4 处 `sqlite3_exec` 调用使用全 `nullptr` 参数，不检查返回值也不获取错误信息，导致 SQL 执行失败时静默忽略：

- 行 178: `sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, nullptr);`
- 行 224: `sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, nullptr);`
- 行 294: `sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, nullptr);`
- 行 338: `sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, nullptr);`

对比文件中已有正确处理模式（行 1297/1353/1406/1421/1807/1828）：它们使用 `char* errMsg` 捕获错误并通过 `Logger::write(LogLevel::ERR)` 记录。

### U2: unreachable sqlite3_finalize（main.cpp）

原审计标记 `main.cpp:306` 存在 `sqlite3_finalize` 在条件分支中可能被跳过的风险。经代码审查确认该处 finalize 已正确放置在 if 块内，但需确保所有 code path 均正确释放。

## 范围边界

- U1: 仅 `src/SubitemUpdaterV2.cpp` — 4 个行位点的日志添加
- U2: 仅 `src/main.cpp` — 审计并修复可能的 finalize 遗漏
- NOT 修改: 其他文件中已经正确的 sqlite3_exec/finalize 调用
- NOT 修改: 文件内其他已有 errMsg 的错误处理代码（已正确）

## 详细变更

### U1: 4 处 silent sqlite3_exec 添加错误日志

将:
```cpp
sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, nullptr);
```

改为:
```cpp
char* execErrMsg = nullptr;
if (sqlite3_exec(db_, updateSql.c_str(), nullptr, nullptr, &execErrMsg) != SQLITE_OK) {
    Logger::write("[SubitemUpdaterV2] SQL exec failed: " + std::string(execErrMsg), LogLevel::ERR);
    sqlite3_free(execErrMsg);
}
```

### U2: main.cpp finalize 审计

- 逐行确认 `-show-sub` 函数的 finalize 调用每个 prepare_v2 分支都有对应的释放
- 如果发现遗漏，添加缺失的 finalize 调用

## 验证步骤

1. `cmake --build build --parallel 8` — 0 errors
2. `ctest -V` — 所有测试通过
3. 运行 `.\bin\validproxy.exe -show-sub` 确认功能正常

## 文件变更列表

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/SubitemUpdaterV2.cpp` | 修改（4处） | silent sqlite3_exec 添加 errMsg + Logger::write(ERR) |
| `src/main.cpp` | 修改（可能） | finalize 审计修复 |

## 风险

| 风险 | 缓解措施 |
|------|----------|
| 新增的 ERR 日志在正常路径不应触发 | 日志仅 SQL 失败时输出，不影响正常流程 |

## 执行结果

- **U1** ✅ 4 处 silent sqlite3_exec 添加了 errMsg 错误捕获 + Logger::write(ERR)，行位已因代码变更偏移（原 178/224/294/338 → 现不同行号）
- **U2** ✅ main.cpp 审计：-show-sub 和 ProfileItem 统计的 finalize 调用均在 prepare_v2 成功的 if 块内，无遗漏

### 验证结果
- ✅ 构建 0 errors
- ✅ ctest 全部通过
