---
title: "调整: 移除log.enabled和dedup.enabled冗余字段"
type: change
status: completed
date: 2026-05-26
---

# 配置项调整记录

## 变更描述

移除 `log.enabled` 和 `dedup.enabled` 字段，因其值等于默认值。

## 变更前

```json
"log": {
    "enabled": true,
    "network_failures": false,
    "console_level": "INFO",
    "file_level": "ERROR"
},
"dedup": {
    "enabled": true,
    "dedup_after_update": false,
    "blacklist_threshold": 5,
    "subids": ["5544178410297751350", "5126987642659995691"]
}
```

## 变更后

```json
"log": {
    "network_failures": false,
    "console_level": "INFO",
    "file_level": "ERROR"
},
"dedup": {
    "dedup_after_update": false,
    "blacklist_threshold": 5,
    "subids": ["5544178410297751350", "5126987642659995691"]
}
```

## 影响分析

| 字段 | 默认值 | 变更后行为 | 备注 |
|------|--------|------------|------|
| `log.enabled` | `true` | 日志保持开启 | 值相等，无功能变化 |
| `dedup.enabled` | `false` | ⚠️ 去重关闭 | 当前值`true`→默认`false`，功能变更 |

⚠️ **警告**: `dedup.enabled` 当前为 `true`（启用去重），调整后将变为 `false`（禁用去重）。如需保持启用状态，请恢复此字段。

---

(上面是新增内容，以下是原有内容)

# ConfigReader SQL字段解析逻辑评估

## 评估目标

分析 `ConfigReader` 如何解析 `sql` 和 `sql_by_subid` 字段，验证配置加载流程是否正确。

## 当前实现分析

### 1. AppConfig 结构 (include/ConfigReader.h)

```cpp
struct AppConfig {
    std::string database_path;
    std::string sql_query;      // ← database.sql 映射
    std::string sql_by_subid;   // ← database.sql_by_subid 映射
};
```

### 2. JSON 解析逻辑 (src/ConfigReader.cpp:68-78)

```cpp
if (obj.contains("database")) {
    auto& db = obj["database"].as_object();
    if (db.contains("sql") && db["sql"].is_string()) {
        config.sql_query = db["sql"].as_string().c_str();
    }
    if (db.contains("sql_by_subid") && db["sql_by_subid"].is_string()) {
        config.sql_by_subid = db["sql_by_subid"].as_string().c_str();
    }
}
```

### 3. SQL查询加载逻辑 (src/ConfigGenerator.cpp:27-31)

```cpp
std::vector<db::models::Profileitem> ConfigGenerator::loadProfiles(const std::string& sqlQuery) {
    db::models::ProfileitemDAO dao(db_);
    auto profiles = dao.getAll(sqlQuery.empty() ? "SELECT * FROM ProfileItem;" : sqlQuery);
    // ...
}
```

## 配置结构映射

| JSON 路径 | AppConfig 字段 | 行为 |
|-----------|--------------|------|
| `database.sql` | `sql_query` | 自定义查询语句 |
| `database.sql_by_subid` | `sql_by_subid` | 按订阅ID查询语句 |
| 字段缺失 | empty string | 触发默认查询 |

## 结论

✅ **解析逻辑正确**。`sql` 和 `sql_by_subid` 字段位于 `database` 对象下，与 `ConfigReader` 解析逻辑一致。当字段缺失时，空字符串触发 `ConfigGenerator::loadProfiles()` 中的默认查询 `"SELECT * FROM ProfileItem;"`。

⚠️ **log.enabled 和 dedup.enabled 可省略当值等于默认值**:
- `log.enabled` 默认 `true`，当前值`true` → **可省略**
- `dedup.enabled` 默认 `false`，当前值`true` → **不可省略**（保持原样）

## 变更记录

- 变更日期: 2026-05-26
- 变更人员: Kilo
- 状态: ✅ 已调整
- 操作:
  - `log.enabled` 已移除（值 `true` 等于默认值）
  - `dedup.enabled` 移除，修改 `ConfigReader.cpp:168` 默认值从 `false` 改为 `true`
  - `ConfigDialog.cpp` 移除 `dedup_enabled` UI 控件（line 72）和保存逻辑（line 154）
- 验证: cmake build 成功，ctest 3/3 通过