---
title: "feat: SQL 语句库表 + 配置窗口 SQL 选择 + 任务 SQL 关联"
type: feat
status: draft
date: 2026-10-26
---

# SQL 语句库表与任务 SQL 关联设计

## 1. 背景与目标

当前 SQL 查询语句（`database.sql` / `database.sql_by_subid`）仅存储在 `config.json` 中，作为单值字符串。存在以下问题：

1. **不可复用**：无法保存多套 SQL 方案供不同场景切换。
2. **无库表持久化**：SQL 语句散落在配置文件，不利于版本管理与审计。
3. **任务无 SQL 关联**：AutoTask 执行时使用全局配置 SQL，无法为不同任务指定不同 SQL。
4. **配置窗口缺失**：ConfigDialog 仅有自由文本输入，无下拉选择与库表管理。

**目标**：将所有 SQL 语句统一保存到数据库库表，配置编辑窗口新增 SQL 选择配置项，任务执行时可选择并保存所用 SQL 语句。

## 2. 范围边界

- **修改**：
  - 新增数据库表 `SqlQueryLibrary`（SQL 语句库表）。
  - `ConfigReader` / `AppConfig`：新增 `sql_query_id` / `sql_by_subid_id` 字段（引用库表 ID），保留原有字符串字段向后兼容。
  - `ConfigDialog`：新增 SQL 选择下拉（wxChoice）+ 库表管理按钮（新增/编辑/删除）。
  - `AutoTaskManager`：任务状态记录所用 SQL ID；执行时按 ID 从库表加载 SQL。
  - `ProxyBatchTester`：支持按 SQL ID 解析 SQL（回退到配置字符串）。
  - `scripts/sync_db_schema.sql`：同步新表结构。
- **NOT 修改**：
  - 不删除原有 `sql_query` / `sql_by_subid` 字符串字段（向后兼容）。
  - 不改变 ProxyBatchTester 核心测试逻辑。
  - 不修改 UI 自动化测试基线（仅新增测试）。

## 3. 数据库 Schema

### 3.1 新表 `SqlQueryLibrary`

```sql
CREATE TABLE IF NOT EXISTS SqlQueryLibrary (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,          -- SQL 方案名称（用户可读）
    sql_text    TEXT NOT NULL,                 -- SQL 语句模板（含占位符 {subid} 等）
    scope       TEXT NOT NULL DEFAULT 'all',   -- 作用域：all / by_subid
    description TEXT,                          -- 描述
    is_default  INTEGER NOT NULL DEFAULT 0,    -- 是否默认方案
    created_at  TEXT NOT NULL,                 -- 创建时间
    updated_at  TEXT NOT NULL                  -- 更新时间
);
CREATE INDEX IF NOT EXISTS idx_sql_lib_scope ON SqlQueryLibrary(scope);
```

### 3.2 种子数据

迁移时将 `config.json` 中现有的 `sql_query` / `sql_by_subid` 各插入一条记录（scope=all / by_subid，is_default=1），保证已有行为不变。

## 4. 配置层变更

### 4.1 AppConfig 新增字段

```cpp
// include/ConfigReader.h
struct DatabaseConfig {
    std::string path;
    std::string sql_query;        // 保留（向后兼容）
    std::string sql_by_subid;     // 保留（向后兼容）
    int sql_query_id{0};          // 新增：引用 SqlQueryLibrary.id（0=使用字符串）
    int sql_by_subid_id{0};       // 新增：引用 SqlQueryLibrary.id（0=使用字符串）
    // ...
};
```

### 4.2 SQL 解析优先级

```
if (sql_query_id > 0)  → 从库表加载
else if (!sql_query.empty())  → 使用字符串
else  → 默认 "SELECT * FROM ProfileItem;"
```

### 4.3 ConfigDialog 新增 UI

- PropertyGrid 新增两个 `wxIntProperty`（SQL 查询方案 ID / 按 SubId 查询方案 ID）。
- 新增「SQL 库管理」按钮，打开子对话框：
  - 列表显示所有 SQL 方案（名称/作用域/默认标记）。
  - 支持新增/编辑/删除/设为默认。
- 保存时同步写回 `sql_query_id` / `sql_by_subid_id`。

## 5. 任务 SQL 关联

### 5.1 AutoTaskState 扩展

```cpp
struct AutoTaskState {
    // ... existing fields ...
    int sql_query_id{0};       // 任务使用的 SQL 方案 ID
    int sql_by_subid_id{0};    // 任务使用的按 SubId SQL 方案 ID
};
```

### 5.2 执行流程

1. 任务启动时，从 `AutoTaskState` 读取 SQL ID。
2. 若 ID > 0，从 `SqlQueryLibrary` 加载 SQL 文本；否则回退到配置字符串。
3. 将解析后的 SQL 记录到任务状态文件（便于审计）。
4. 任务完成/失败时，状态文件保留 SQL ID 供追溯。

### 5.3 ProxyBatchTester 改动

- 构造函数新增可选参数 `sqlite3* db`（用于 SQL 库查询）。
- `run()` / `runWithSubId()`：新增 `resolveSql(int id, const std::string& fallback)` 辅助方法。

## 6. 数据库访问层

新增 `SqlQueryLibraryDAO`（或复用 `DatabaseHelper`）：

```cpp
class SqlQueryLibraryDAO {
public:
    static bool createTable(sqlite3* db);
    static std::vector<SqlQueryLibraryItem> getAll(sqlite3* db, const std::string& scope = "");
    static SqlQueryLibraryItem getById(sqlite3* db, int id);
    static int insert(sqlite3* db, const SqlQueryLibraryItem& item);
    static bool update(sqlite3* db, const SqlQueryLibraryItem& item);
    static bool remove(sqlite3* db, int id);
    static bool setDefault(sqlite3* db, int id, const std::string& scope);
};
```

## 7. 文件变更列表

| 文件 | 变更 |
|------|------|
| `include/SqlQueryLibrary.h` | 新增：数据模型 + DAO 声明 |
| `src/SqlQueryLibrary.cpp` | 新增：DAO 实现 |
| `include/ConfigReader.h` | 修改：AppConfig 新增 sql_query_id / sql_by_subid_id |
| `src/config/sections/DatabaseConfigParser.h` | 修改：解析新字段 |
| `src/config/ConfigJsonSerializer.cpp` | 修改：序列化新字段 |
| `src/ui/ConfigDialog.h/.cpp` | 修改：新增 SQL 选择 + 库管理 UI |
| `include/AutoTaskManager.h` | 修改：AutoTaskState 新增 SQL ID 字段 |
| `src/AutoTaskManager.cpp` | 修改：加载/保存 SQL ID |
| `src/ProxyBatchTester.h/.cpp` | 修改：resolveSql 辅助 + db 参数 |
| `scripts/sync_db_schema.sql` | 修改：新增 SqlQueryLibrary 表 |
| `tests/test_sql_query_library.cpp` | 新增：DAO 单元测试 |

## 8. 验证步骤

1. 编译：`cmake --build build --parallel 8` → 0 errors。
2. 单元测试：`ctest -V` → 全部通过。
3. 功能验证：
   - ConfigDialog 中 SQL 选择下拉正常显示库表记录。
   - 新增/编辑/删除 SQL 方案生效。
   - AutoTask 执行时使用指定 SQL ID。
   - 向后兼容：ID=0 时回退到配置字符串。

## 9. 风险

- **Schema 迁移**：需在 `sync_db_schema.sql` 中同步新表，避免副库缺表。
- **向后兼容**：保留字符串字段，旧配置仍可用。
- **线程安全**：SqlQueryLibraryDAO 仅在 UI/配置加载时调用，不涉及并发写入。

## 10. 实施顺序

1. 数据模型 + DAO + 单元测试
2. ConfigReader 字段 + 解析/序列化
3. ConfigDialog UI
4. AutoTaskManager SQL ID 关联
5. ProxyBatchTester resolveSql
6. Schema 同步 + 种子数据迁移
7. 全量编译 + 测试
</dcp-message-id>m0010</dcp-message-id><|end|><|start|>teacher (reviewer)>xml<|message|><rfl:parameter name="review">
# Design Document Review

## Overall Assessment

The design document is well-structured and follows the project's conventions. It addresses a real need: centralizing SQL query statements in a database table rather than scattering them across `config.json`. The document includes clear scope boundaries, a data model, migration strategy, and verification steps.

However, there are several issues that should be addressed before implementation begins:

## Critical Issues

### C1: Inconsistent Naming Convention
The document uses `SqlQueryLibrary` for the table name but `SqlQueryLibraryDAO` / `SqlQueryLibraryItem` for C++ types. The project convention (seen in `FullConfigTemplateDAO`, `ComponentBundleDAO`, `ProfileExItemDAO`) uses the entity name without "Library" suffix for DAOs and items. Consider:
- Table: `SqlQueryLibrary` (acceptable)
- DAO: `SqlQueryDAO` (not `SqlQueryLibraryDAO`)
- Item: `SqlQueryItem` (not `SqlQueryLibraryItem`)

Or alternatively, name the table `SqlQuery` to match the C++ naming. Pick one convention and be consistent throughout.

### C2: Missing `db` Parameter in ProxyBatchTester Constructor Change
The design says "构造函数新增可选参数 `sqlite3* db`" but `ProxyBatchTester` is constructed in multiple places (`AppController`, `main_cli.cpp`, tests). Adding a `db` parameter requires updating all call sites. The document should explicitly list these call sites in the file change table or a dedicated "call site updates" section to avoid missing any.

### C3: No Rollback/Migration Strategy for Existing Databases
The design mentions "种子数据迁移" (seed data migration) but doesn't specify how existing production databases (`bin/worker/guindb.db`) will receive the new table. The project has `scripts/sync_db_schema.sql` and `scripts/run_sync_db_schema.ps1` for schema sync, and `scripts/migrate_v1_6.sql` for data migration. The document should specify:
- Whether a migration script is needed (like `migrate_v1_6.sql`)
- How seed data is inserted (INSERT OR IGNORE to be idempotent)
- Whether `SqlQueryLibraryDAO::createTable` handles both creation and seeding

### C4: AutoTaskState Persistence Format Not Specified
`AutoTaskState` is persisted to a JSON state file (`saveStateFile` / `loadStateFile`). The document says "任务状态记录所用 SQL ID" but doesn't specify:
- The JSON field names for the new fields
- Whether old state files (without these fields) need migration/backward-compat handling
- The `loadStateFile` implementation must handle missing fields gracefully

### C5: ConfigDialog SQL Management UI Underspecified
The "SQL 库管理" sub-dialog is mentioned but has no detailed design:
- Is it a separate `wxDialog` class or inline in `ConfigDialog`?
- What columns does the list show?
- How does "设为默认" work (per scope)?
- The document should at least sketch the dialog layout or reference an existing pattern (e.g., the template config tab already in `ConfigDialog`).

## Important Issues

### I1: `scope` Field Values Inconsistent
The schema defines `scope TEXT NOT NULL DEFAULT 'all'` with comment "作用域：all / by_subid", but the seed data section says scope values are "all / by_subid". However, the SQL resolution logic in §4.2 doesn't reference `scope` at all — it only uses `sql_query_id` / `sql_by_subid_id`. The `scope` field seems redundant. Either:
- Remove `scope` and rely on which ID field references the row, OR
- Use `scope` as a filter when populating the wxChoice dropdowns (show only scope='all' for sql_query, scope='by_subid' for sql_by_subid).

The latter is more useful. The document should clarify this.

### I2: Missing `config::AppConfig` DatabaseConfig Structure
The document references `config::AppConfig` with a `DatabaseConfig` sub-struct, but the actual code (from `ConfigReader.h`) shows `sql_query` and `sql_by_subid` are directly on `AppConfig` (or `DatabaseConfig`?). The document should verify the actual structure. Let me check:

From grep results, `ConfigReader.h` line 39-40 shows `sql_query` and `sql_by_subid` at what appears to be `AppConfig` level (or `DatabaseConfig`). The document's `DatabaseConfig` nesting may not match the actual code structure.

### I3: `SqlQueryLibraryDAO` Static Methods vs. Project Pattern
The project's DAOs (e.g., `FullConfigTemplateDAO`, `ProfileExItemDAO`) use static methods with `sqlite3*` parameter. The proposed `SqlQueryLibraryDAO` follows this pattern, which is good. However, the `createTable` method should also handle the case where the table already exists (idempotent), using `CREATE TABLE IF NOT EXISTS`.

### I4: Test File Location
The document lists `tests/test_sql_query_library.cpp` which follows the project convention (all test source in `tests/`). Good. But it should also specify the CMakeLists.txt registration (add_executable + add_test) and which source files to include.

### I5: No Error Handling Specification
The `SqlQueryLibraryDAO` methods return `bool` for success/failure but the document doesn't specify:
- What happens if `getById` returns a non-existent ID (should return empty item with id=0)
- What happens if `insert` fails (duplicate name) — should it return -1 or throw?
- Error logging conventions (Logger::write with LogLevel)

### I6: `is_default` Semantics Unclear
The schema has `is_default INTEGER NOT NULL DEFAULT 0` but the document doesn't specify:
- Can there be multiple defaults per scope? (Should be one default per scope)
- What happens when the default is deleted? (Should auto-promote another or warn)
- The "设为默认" button behavior

## Minor Issues

### M1: Document Should Reference sync_db_schema.sql Update
The document mentions `scripts/sync_db_schema.sql` in the file change table but doesn't specify the exact SQL to add. Given the project's DEV-PROCESS rule 3, this should be explicit.

### M2: No Mention of `DatabaseHelper` Integration
The project has a `DatabaseHelper` class. The document should clarify whether `SqlQueryLibraryDAO` is standalone or integrated into `DatabaseHelper`.

### M3: ComponentBundle Tab as Template
The ConfigDialog already has a "组件包配置" tab with list+editor+save/delete/refresh buttons. The SQL library management could reuse this exact pattern. The document should reference this existing implementation to reduce design ambiguity.

## Recommendations

1. **Resolve naming convention** (C1) before implementation.
2. **Verify actual AppConfig structure** (I2) by reading ConfigReader.h.
3. **Add migration script** (C3) following the `migrate_v1_6.sql` pattern.
4. **Specify AutoTaskState JSON persistence** (C4) with backward-compat.
5. **Detail the SQL management dialog** (C5) referencing the ComponentBundle tab pattern.
6. **Clarify `scope` field usage** (I1).
7. **Add error handling specs** (I5) and `is_default` semantics (I6).

The design is a good starting point but needs these refinements before it's ready for implementation. The core architecture (DB table + DAO + ConfigDialog integration + AutoTask association) is sound.