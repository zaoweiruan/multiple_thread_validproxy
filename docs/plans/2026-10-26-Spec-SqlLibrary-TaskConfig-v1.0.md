---
title: "feat: SQL 语句库表 + 配置窗口 SQL 选择 + 任务 SQL 关联"
type: feat
status: implemented
date: 2026-10-26
updated: 2026-10-26
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
  - 新增数据库表 `SqlQuery`（SQL 语句库表）。
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

### 3.1 新表 `SqlQuery`

```sql
CREATE TABLE IF NOT EXISTS SqlQuery (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,          -- SQL 方案名称（用户可读）
    sql_text    TEXT NOT NULL,                 -- SQL 语句模板（含占位符 {subid} 等）
    scope       TEXT NOT NULL DEFAULT 'all',   -- 作用域：all / by_subid（用于 UI 过滤下拉选项）
    description TEXT,                          -- 描述
    is_default  INTEGER NOT NULL DEFAULT 0,    -- 是否默认方案（每 scope 最多一个）
    created_at  TEXT NOT NULL,                 -- 创建时间
    updated_at  TEXT NOT NULL                  -- 更新时间
);
CREATE INDEX IF NOT EXISTS idx_sql_scope ON SqlQuery(scope);
```

### 3.2 种子数据迁移

迁移脚本 `scripts/migrate_sql_library.sql`（新建），幂等插入：

```sql
INSERT OR IGNORE INTO SqlQuery (name, sql_text, scope, is_default, created_at, updated_at)
SELECT 'default_all', sql_query, 'all', 1, datetime('now'), datetime('now')
FROM (SELECT 1) WHERE EXISTS (SELECT 1 FROM config WHERE ...);
```

实际迁移由 `SqlQueryDAO::createTable` + `seedDefaults(db, config)` 在首次运行时执行（INSERT OR IGNORE 保证幂等）。

## 4. 配置层变更

### 4.1 AppConfig 新增字段

经核实 `ConfigReader.h`，`sql_query` / `sql_by_subid` 直接位于 `AppConfig` 结构体（非嵌套 DatabaseConfig）。新增字段同样直接添加：

```cpp
// include/ConfigReader.h — struct AppConfig
std::string sql_query;        // 保留（向后兼容）
std::string sql_by_subid;     // 保留（向后兼容）
int sql_query_id{0};          // 新增：引用 SqlQuery.id（0=使用字符串）
int sql_by_subid_id{0};       // 新增：引用 SqlQuery.id（0=使用字符串）
```

### 4.2 SQL 解析优先级

```
resolveSql(id, fallback):
  if (id > 0)  → SqlQueryDAO::getById(db, id).sql_text（空则回退）
  else if (!fallback.empty())  → fallback
  else  → "SELECT * FROM ProfileItem;"
```

### 4.3 ConfigDialog 新增 UI

- PropertyGrid 仅保留 `sql_query` / `sql_by_subid` 两行，且设为只读；配置值显示所选方案的**名称**（通过隐藏的 `sql_query_id` / `sql_by_subid_id` 实时解析）。
  - ID=0 且无原始 SQL：显示「（未选择方案，点击选择）」。
  - ID=0 但存在旧版原始 SQL：显示「（自定义 SQL，点击选择方案）」。
  - ID>0 但方案已被删除：显示「（方案 #id 不存在，点击重新选择）」。
- 弹窗触发方式改为**点击配置值区域**：`wxPropertyGrid::HitTest` 命中值列（column 1）时打开 `SqlQueryManageDialog`；已选中行的只读编辑器点击也走同一路径。双击/省略号按钮机制已取消。
- `SqlQueryManageDialog`（纵向布局：上方列表、下方编辑区）：
  - 列表列：ID / 名称 / 作用域 / 默认 / 描述
  - 按钮：新增 / 保存 / 删除 / 设为默认 / 刷新
  - 编辑区：名称 / 作用域(wxChoice: all/by_subid) / 默认勾选 / 描述 / **多行 SQL 编辑器（自动换行、等宽字体、较大高度）**
  - 底部：选择此方案 / 关闭
  - 打开时按配置项所需作用域预过滤；选择时校验作用域必须匹配，避免把通用 SQL 配到按 SubId 的配置项上。
- 选择方案后：更新隐藏 ID 与原始 SQL 文本，刷新显示名称为方案名；`saveConfig()` 持久化隐藏 ID，**不会**把显示名称误写回 `sql_query` / `sql_by_subid`。

## 5. 任务 SQL 关联

### 5.1 AutoTaskState 扩展

```cpp
struct AutoTaskState {
    // ... existing fields ...
    int sql_query_id{0};       // 任务使用的 SQL 方案 ID
    int sql_by_subid_id{0};    // 任务使用的按 SubId SQL 方案 ID
};
```

### 5.2 JSON 持久化（向后兼容）

`saveStateFile` / `loadStateFile` 使用 boost::json。新增字段序列化为 `sql_query_id` / `sql_by_subid_id`。`loadStateFile` 对缺失字段使用默认值 0（boost::json::value::if_contains 模式），旧状态文件无需迁移。

### 5.3 执行流程

1. 任务启动时，从 `AutoTaskState` 读取 SQL ID。
2. 若 ID > 0，从 `SqlQuery` 表加载 SQL 文本；否则回退到配置字符串。
3. 将解析后的 SQL 记录到任务状态文件（便于审计）。
4. 任务完成/失败时，状态文件保留 SQL ID 供追溯。

### 5.4 ProxyBatchTester 改动

- 构造函数新增可选参数 `sqlite3* db`（默认 nullptr，向后兼容）。
- 新增 `std::string resolveSql(int id, const std::string& fallback) const` 辅助方法。
- `run()` / `runWithSubId()`：调用 `resolveSql` 替代直接使用 `config_.sql_query` / `config_.sql_by_subid`。

**调用点更新**：
- `AppController.cpp`：`ProxyBatchTester tester(db_, config_, ...)` — 传入 db_。
- `main_cli.cpp`：`ProxyBatchTester tester(db, *appConfig, ...)` — 传入 db。
- `tests/test_proxy_batch_tester.cpp`：构造时可传 nullptr（回退到字符串）。

## 6. 数据库访问层

新增 `SqlQueryDAO` + `SqlQueryItem`（遵循项目 DAO 静态方法模式）：

```cpp
// include/SqlQuery.h
struct SqlQueryItem {
    int id{0};
    std::string name;
    std::string sql_text;
    std::string scope{"all"};
    std::string description;
    bool is_default{false};
    std::string created_at;
    std::string updated_at;
};

class SqlQueryDAO {
public:
    static bool createTable(sqlite3* db);                       // CREATE TABLE IF NOT EXISTS
    static bool seedDefaults(sqlite3* db, const std::string& sqlAll, const std::string& sqlBySubid); // INSERT OR IGNORE
    static std::vector<SqlQueryItem> getAll(sqlite3* db, const std::string& scope = "");
    static SqlQueryItem getById(sqlite3* db, int id);           // 不存在返回 id=0 的空项
    static int insert(sqlite3* db, const SqlQueryItem& item);   // 返回新 ID，失败返回 -1
    static bool update(sqlite3* db, const SqlQueryItem& item);
    static bool remove(sqlite3* db, int id);
    static bool setDefault(sqlite3* db, int id, const std::string& scope); // 清除同 scope 旧默认，设新默认
};
```

**错误处理**：
- `getById` 不存在 → 返回 `SqlQueryItem{}`（id=0），调用方回退。
- `insert` 重复 name → 返回 -1 + Logger::write(ERR)。
- 所有 DB 操作失败 → Logger::write(ERR) + 返回 false/-1。

## 7. 文件变更列表

| 文件 | 变更 |
|------|------|
| `include/SqlQuery.h` | 新增：SqlQueryItem + SqlQueryDAO 声明 |
| `src/SqlQuery.cpp` | 新增：DAO 实现 |
| `include/ConfigReader.h` | 修改：AppConfig 新增 sql_query_id / sql_by_subid_id |
| `src/config/sections/DatabaseConfigParser.h` | 修改：解析新字段 |
| `src/config/ConfigJsonSerializer.cpp` | 修改：序列化新字段 |
| `src/ui/ConfigDialog.h/.cpp` | 修改：新增 SQL 选择 + 库管理 UI |
| `src/ui/SqlQueryManageDialog.h/.cpp` | 新增：SQL 库管理对话框 |
| `include/AutoTaskManager.h` | 修改：AutoTaskState 新增 SQL ID 字段 |
| `src/AutoTaskManager.cpp` | 修改：load/save SQL ID（向后兼容） |
| `src/ProxyBatchTester.h/.cpp` | 修改：resolveSql 辅助 + db 参数 |
| `src/ui/AppController.cpp` | 修改：ProxyBatchTester 构造传 db_ |
| `src/main_cli.cpp` | 修改：ProxyBatchTester 构造传 db |
| `scripts/sync_db_schema.sql` | 修改：新增 SqlQuery 表 |
| `tests/test_sql_query.cpp` | 新增：DAO 单元测试 |
| `CMakeLists.txt` | 修改：注册新源文件与测试 |

## 8. 验证步骤

1. 编译：`cmake --build build --parallel 8` → 0 errors。
2. 单元测试：`ctest --test-dir build -R "SqlQuery|AutoTask" -V` → `SqlQueryDAOTest` 4/4、`AutoTaskTest` 9/9 通过。
3. 功能验证：
   - ConfigDialog 仅显示「SQL 查询 / 按 SubId 查询」两行，值为已保存方案名称；点击值区域弹出 SQL 方案库。
   - SQL 方案库为纵向布局：上方方案列表、下方编辑区；SQL 编辑框自动换行且高度充足。
   - 新增/编辑/保存/删除 SQL 方案生效。
   - 设为默认功能正确（同 scope 仅一个默认）。
   - 选择方案后隐藏 ID 与原始 SQL 更新，保存配置后持久化，配置值显示方案名称。
   - AutoTask 执行时使用指定 SQL ID。
   - 向后兼容：ID=0 时回退到配置字符串；旧状态文件正常加载。

### 8.1 实施验证记录（2026-10-26）

- 构建：`cmake --build build --parallel 8` 成功（validproxy / validproxy-cli）。
- 定向测试：`SqlQueryDAOTest` 4/4、`AutoTaskTest` 9/9，100% 通过。
- 2026-10-26 UI 调整后复验：重新构建成功；`ctest --test-dir build -R "SqlQuery|AutoTask|UI_CHAINPREVIEW" -V` 3/3 通过（`UI_CHAINPREVIEW` 16 assertions / 2 test cases，构造完整 ConfigDialog 无 wx 断言）。
- UI 自动化：`scripts/build-and-test.bat` 12 项中 11 项通过；唯一失败为 `UI_FLOATINGWIDGET` 的既有 orb 命中测试（`tests/ui/TestStandaloneFloatingWidget.cpp:529`，20 != 25），与本功能无关，单独复跑仍稳定失败，判定为既有基线问题。
- 未自动化覆盖：SQL 方案库对话框内的点击/选择交互（模态窗口 + PropertyGrid 值区域命中），现有 Win32 UI 测试框架未提供稳定定位点。代码路径已通过编译与 DAO 单测覆盖，建议交付前人工点验一次。

## 9. 风险

- **Schema 迁移**：需在 `sync_db_schema.sql` 中同步新表，避免副库缺表。使用 `CREATE TABLE IF NOT EXISTS` 保证幂等。
- **向后兼容**：保留字符串字段，旧配置仍可用；旧状态文件缺失字段默认 0。
- **线程安全**：SqlQueryDAO 仅在 UI/配置加载时调用，不涉及并发写入。

## 10. 实施顺序

1. 数据模型 + DAO + 单元测试
2. ConfigReader 字段 + 解析/序列化
3. ConfigDialog UI + SqlQueryManageDialog
4. AutoTaskManager SQL ID 关联
5. ProxyBatchTester resolveSql + 调用点更新
6. Schema 同步 + 种子数据迁移
7. 全量编译 + 测试
