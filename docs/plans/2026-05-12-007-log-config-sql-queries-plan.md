---
title: "feat(ProxyBatchTester): log config-parsed SQL queries to log"
type: feat
status: completed
date: 2026-05-12
origin: "Sisyphus - Plan 007: Log Config-Parsed SQL Queries to Log"
---
# Plan 007: Log Config-Parsed SQL Queries to Log

- **Status**: completed
- **Type**: feat
- **Author**: Sisyphus
- **Created**: 2026-05-12

## 1. Problem

项目支持在 `config.json` 的 `database.sql` 和 `database.sql_by_subid` 字段中配置自定义 SQL 查询语句。这些从配置中解析的 SQL 语句当前**不会**被记录到日志中，导致调试时无法确认程序实际执行的 SQL 内容（包括占位符替换后的结果）。

> **范围限定**：本计划**只**处理从配置文件中解析的 SQL 语句（`sql_query`/`sql_by_subid`），不涉及代码中硬编码的 SQL 语句（如 `SubitemUpdaterV2.cpp` 中的 INSERT 语句）。

## 2. 相关代码链路

```
config.json
  └─ database.sql          ──→ ConfigReader.cpp:73-74  → config.sql_query
  └─ database.sql_by_subid  ──→ ConfigReader.cpp:76-77  → config.sql_by_subid
       │
       ▼  (占位符替换)
ConfigReader.cpp:240-241
  replacePlaceholder(sql_query, "{blacklist_threshold}", ...)
  replacePlaceholder(sql_by_subid, "{blacklist_threshold}", ...)
       │
       ▼  (使用)
ProxyBatchTester.cpp:29-37
  if (!subId.empty() && !config_.sql_by_subid.empty())
      sql = config_.sql_by_subid + replace "{subid}"
  else
      sql = config_.sql_query
       │
       ▼  (执行)
  configGen.loadProfiles(sql)  →  DAO.getAll(sqlQuery)
```

## 3. 变更位置和工作量

### 3.1 ConfigReader.cpp — 解析完成占位符替换后，记录最终 SQL

- 位置：`ConfigReader.cpp` 第 240-241 行之后
- 操作：添加两行 `Logger::write`，`LogLevel::DEBUG`
- 日志内容：
  - `sql_query` 替换完 `{blacklist_threshold}` 后的完整 SQL
  - `sql_by_subid` 替换完 `{blacklist_threshold}` 后的完整 SQL
- 预估代码：~4 行

### 3.2 ProxyBatchTester.cpp — 实际使用 SQL 前，记录最终 SQL

- 位置：`ProxyBatchTester.cpp` 第 37 行之后（`sql` 变量已确定）
- 操作：添加一行 `Logger::write`，`LogLevel::DEBUG`
- 日志内容：最终会传给 `loadProfiles()` 的 SQL 字符串（含 `{subid}` 替换后的值）
- 预估代码：~2 行

## 4. 日志级别说明

使用 `LogLevel::DEBUG` 的原因：
- `file_level` 默认 `DEBUG`，SQL 会写入日志文件
- `console_level` 默认 `INFO`，SQL **不会**刷到控制台
- 仅当用户手动配置 `log_console_level: "debug"` 时才会在控制台显示 SQL

## 5. 变更清单

| 文件 | 行 | 操作 | 日志消息示例 |
|------|----|------|-------------|
| `src/ConfigReader.cpp` | ~242 | 追加 Logger::write | `[ConfigReader] SQL query: SELECT * FROM ...` |
| `src/ConfigReader.cpp` | ~243 | 追加 Logger::write | `[ConfigReader] SQL by subid: SELECT * FROM ...` |
| `src/ProxyBatchTester.cpp` | ~38 | 追加 Logger::write | `[ProxyBatchTester] Executing SQL: SELECT * FROM ...` |

## 6. 验证标准

- [ ] 构建零 errors
- [ ] `ctest` 全部通过
- [ ] 日志文件中能看到 SQL 语句记录（`DEBUG` 级别）
- [ ] 控制台默认不显示 SQL（`INFO` 阈值过滤）
- [ ] `sql_query` 和 `sql_by_subid` 均被记录
- [ ] `{blacklist_threshold}` 和 `{subid}` 占位符替换后的值在日志中可见

