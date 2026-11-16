# Spec — 数据库路径无效时提示生成库表并继续启动

- **日期**: 2026-11-12
- **模块**: `src/main_gui.cpp`、`src/service/DatabaseConnectionService.*`、新增 `src/service/DatabaseBootstrap.*`、`resources/schema_init.sql`、`CMakeLists.txt`
- **版本**: v1.0（设计稿，待实施；已过 3 轮 6-persona 独立评审：coherence / feasibility / adversarial / scope-guardian / security-lens / design-lens。第 3 轮已裁决「就地补齐」推迟 v2）

## 1. 背景

用户诉求：

> 新增功能：当数据库路径无效时，提示用户是否生成，批准后在 exe 相同目录生成库表并打开程序

### 1.1 现状（已勘察）

| 位置 | 行为 |
|---|---|
| `include/config/sections/DatabaseConfigParser.h:17-40` | `database.path` 为相对路径时按 **exe 目录**解析（同时接受 `"database": "x.db"` 字符串形式与 `database.path` 对象形式）；为空或缺失时 `AppConfig::database_path` 保持空串 |
| `src/main_gui.cpp:114-120` | 路径为空 → `MessageBoxA` 报错，`return 1` |
| `src/main_gui.cpp:121-129` | `DatabaseConnectionService::open()` 失败 → 报错，`return 1` |
| `src/service/DatabaseConnectionService.cpp:11` | `open()` 固定使用 `SQLITE_OPEN_READWRITE \| SQLITE_OPEN_CREATE` |
| `src/main_gui.cpp:131-143` | 打开后仅补 `FullConfigTemplateItem` / `SqlQuery` 两张表并写种子（前者属 v2rayN 兼容表，后者属项目专有表） |
| `src/main_cli.cpp:65-70` | `openDatabase()` 内联 `sqlite3_open_v2`，同样带 `CREATE` 标志，13 处调用点 |
| `src/ui/UIApp.cpp:40-91` | detached 兜底路径（当前唯一构造点是 `main_gui.cpp:162`，实际不可达）先 `exists()` 检查，缺失仅弹错 |

### 1.2 真实缺陷（本功能要解决的核心问题）

`SQLITE_OPEN_CREATE` 让「路径无效」的表现分裂成两种，且**最危险的一种是静默的**：

| 情形 | 当前结果 |
|---|---|
| 父目录存在、文件不存在 | **静默创建一个 0 字节空库**，`open()` 返回成功；程序继续跑，随后因 `SubItem`/`ProfileItem` 等表不存在而在 UI 线程抛出各种 `no such table` 错误，用户看到的是"功能全坏"而不是"库没建" |
| 父目录不存在 / 路径指向目录 / 无写权限 / 网络盘不可达 | `sqlite3_open_v2` 返回 `SQLITE_CANTOPEN` → 弹出「Failed to open database」后退出，用户没有任何补救入口 |
| 文件存在但非 SQLite（或损坏） | `open()` 成功（SQLite 惰性校验），首次查询才报 `file is not a database` |
| 文件存在但缺核心表 | 运行期才暴露 |

即：**当前程序没有任何"新建一个可用的库"的路径**。`scripts/sync_db_schema.sql` 只做增量升级（`ALTER TABLE` + 3 张专有表 + 种子），前提是 v2rayN 原生库已存在；仓库里唯一完整的空库是 `test/guiNDB_empty.db`（143 KB，供 `scripts/prepare-ui-sandbox.bat:10` 使用），未随 exe 分发。

## 2. 需求

| # | 需求 | 验收锚点 |
|---|---|---|
| R1 | 启动时先**只读探测**数据库可用性，不再让 `SQLITE_OPEN_CREATE` 静默造空库 | 启动路径改用 `OpenMode::ReadWriteExisting`；缺失文件不再被自动创建 |
| R2 | 探测结果为"缺失/空/缺核心表/遗留备份"时，弹窗询问是否生成或恢复；用户选「否」则按现状退出 | 弹窗文案含路径与原因；取消后 `return 1` |
| R3 | 用户批准后在 **exe 同目录**生成带完整库表 + 索引 + 必需种子的数据库，然后继续启动 | 生成后 `inspect() == Ok`；程序正常进入主界面 |
| R4 | 生成目标与配置路径不同时，把新路径写回 `config.json` 的 `database.path`（必要时连带同值的 `sync.target_db`），并同步内存配置 | 重启后直接命中新库，不再弹窗；后续保存配置不回退 |
| R5 | 非 SQLite / 损坏文件**不静默覆盖**：单独提示「备份为 .bak 后重建」，默认按钮为「否」 | 未批准时原文件字节不变 |
| R6 | 探测与生成逻辑可单测，不依赖 UI | 新增 `DatabaseBootstrapTest` 全部为纯 Google Test |
| R7 | CLI 保持非交互（不弹框），但**同样不得静默产出残缺库**：文件缺失/为空时自动建完整 schema，其他非 `Ok` 状态一律拒绝并给出可执行错误 | `main_cli.cpp::openDatabase()` 先 `inspect()`，仅对 MissingPath/MissingFile/MissingDirectory/EmptyFile 调 `create()`，随后以 `READWRITE`（**不带 CREATE**）打开 |
| R8 | 生成目标与配置路径相同时（配置本来就在 exe 目录），不回写 `config.json` | 生成路径 == 配置路径时 `config.json` 的 mtime 不变 |

## 3. 无效判定矩阵

`DatabaseBootstrap::inspect(path)` 只读，不创建/不修改任何文件：

| 探测结果 | 判定条件 | 建议动作 |
|---|---|---|
| `MissingPath` | 配置路径为空串 | 提示生成（目标 `<exeDir>/guiNDB.db`，经 `createTargetPath` → `defaultTargetPath` 推导） |
| `MissingDirectory` | 父目录不存在，或路径本身是目录 | 提示生成（目标按 §4.5 的 `defaultTargetPath` 规则推导；路径以分隔符结尾时文件名部分为空 → `<exeDir>/guiNDB.db`） |
| `MissingFile` | 文件不存在，且同目录下没有它遗留的 `.bak.<yyyyMMdd-HHmmss>` | 提示生成 |
| `BackupRecoverable` | 文件不存在，但同目录下存在它遗留的 `.bak.<ts>`（上次重建在 backup 之后、create 之前被中断）；有多个时取 **mtime 最新**的一个 | 提示「用备份恢复」，默认「是」；恢复即把该 `.bak.<ts>` 改名回原路径，不新建空库 |
| `EmptyFile` | 文件大小为 0 **且**同目录下不存在非空 `-wal` 附属文件 | 提示生成 |
| `WalPending` | 主库大小为 0（或很小）但同目录下存在非空 `-wal`：数据在 WAL 里，尚未 checkpoint | **不得新建**：以 `READWRITE` 打开（SQLite 自动回放 WAL），再执行 `PRAGMA wal_checkpoint(TRUNCATE)`，重新 `inspect()` 后按结果继续；回放失败 → `PromptRebuild` |
| `Inaccessible` | `sqlite3_open_v2(SQLITE_OPEN_READONLY)` 返回 `SQLITE_CANTOPEN`（无读权限 / 网络盘不可达 / 被独占），且文件头**不是**合法 SQLite 头（前 16 字节非 `SQLite format 3\0`）；**「路径本身是目录」归 `MissingDirectory`** | `Abort`：弹错退出，**禁止回写 `config.json`** |
| `WALReadOnlyBlocked` | 头部是合法 `SQLite format 3\0`，但只读打开返回 `SQLITE_CANTOPEN` —— 典型是 WAL 库放在只读目录/共享盘上（读库需能建 `-shm`，本仓库经 `applyPragmas` 一律是 WAL） | `Abort`：提示「数据库文件有效，但所在目录不可写（WAL 需要写 `-shm`）」，**不判为损坏、不重建** |
| `BusyOrLocked` | 只读 **打开成功**，但后续读取 `sqlite_master` / `PRAGMA schema_version` 返回 `SQLITE_BUSY` / `SQLITE_LOCKED`（另一实例持写锁或独占） | `Abort`：提示「数据库正被其他实例占用，请关闭后重试」，**不进入重建分支** |
| `NotADatabase` | 只读打开成功，但读取 `sqlite_master` 返回 `SQLITE_NOTADB` / `SQLITE_CORRUPT` | 提示「备份后重建」，默认「否」 |
| `MissingCoreTables` | 可打开，但 `sqlite_master` 缺 `SubItem`/`ProfileItem`/`ProfileExItem`/`FullConfigTemplateItem` 任一，**或这 4 张表缺必需列**（`ProfileItem.Region`、`ProfileExItem` 的 4 个统计列、`FullConfigTemplateItem` 的 `ParentId`/`BundleChain`/`TemplateType`/`BundleVersion`） | 提示「备份后重建」，默认「否」——**本次不做就地补齐**（不修改用户既有库的任意一列），旧库原样备份为 `.bak.<ts>` 后在新库上继续 |
| `Ok` | 以上皆不成立（只读打开成功 + `sqlite_master` 可读 + 4 张核心表齐 + 必需列齐） | 直接继续 |

> 核心表只取这 4 张（GUI 启动链路必需）。`ServerStatItem`/`RoutingItem`/`DNSItem`/`ProfileGroupItem` 属 v2rayN 兼容表，**建库时创建，但不作为"不完整"判据**，避免对存量库误报。
>
> **`Ok` 路径不执行 `PRAGMA quick_check`**：实测在 155–162 MB 的库上约 +2.3 s，会让"正常启动"明显变慢，与 §8 验收 5 冲突。`quick_check` 只在 `create()` 完成后作为新建库自检，以及 `PromptRebuild` 前的二次确认执行。

## 4. 方案

### 4.1 方案 A（推荐）：内嵌完整建库 SQL

- 新增 `resources/schema_init.sql`：11 张表 `CREATE TABLE IF NOT EXISTS` + 3 个索引 + `ComponentBundle` 16 条种子 + `FullConfigTemplateItem` 8 条模板种子（全部 `INSERT OR IGNORE`）。
- 由 CMake 在配置期读取该文件并生成 `DatabaseSchemaInit.cpp`（`R"SQLINIT(...)SQLINIT"` 原始字符串），编入 `CORE_SOURCES`。
- 优点：**单 exe 自包含**，不需要随包分发第二个文件；SQL 在 git 里可 review、可 diff；幂等，可重复执行。
- 代价：`resources/schema_init.sql` 与 `scripts/sync_db_schema.sql` 需同时维护（见 §7 风险）。

### 4.2 方案 B（备选）：分发空库模板

把 `test/guiNDB_empty.db` 复制到 `bin/` 并随包分发，批准后 `std::filesystem::copy_file` 到目标路径。

- 优点：几乎零代码；模板已被 UI 沙箱长期使用。
- 缺点：多一个 143 KB 二进制文件；模板与代码/脚本三方漂移，且该模板本身缺 `SqlQuery` 表、缺 `proxy_runtime_history.socks_port`（`ComponentBundle` 实测 16 行，非空）。

**结论：采用方案 A。** 方案 B 记录为退路。

### 4.3 建库内容（`resources/schema_init.sql`）

| 类别 | 对象 |
|---|---|
| v2rayN 兼容表（8） | `SubItem`、`ProfileItem`（含 `Region`）、`ProfileExItem`（含 4 个统计列）、`ServerStatItem`、`RoutingItem`、`DNSItem`、`FullConfigTemplateItem`（含 `ParentId`/`BundleChain`/`TemplateType`/`BundleVersion`）、`ProfileGroupItem` |
| 项目专有表（3） | `ComponentBundle`、`proxy_runtime_history`（含 `pid`/`socks_port`）、`SqlQuery` |
| 索引（3） | `idx_profile_dedup`、`idx_runtime_history_index`、`idx_sql_scope` —— 全部写进 `schema_init.sql`（`IF NOT EXISTS`）。其中 `idx_profile_dedup` 也会被 `DatabaseConnectionService::applyPragmas()` 每次 open 时重复创建（`DatabaseConnectionService.cpp:31`），`idx_runtime_history_index` 也会被 `RuntimeHistoryDAO` 建表时创建；重复声明幂等无害，**保留是为了让新建库在 `create()` 返回那一刻三种索引就齐全**，测试无需依赖后续 open |
| 种子 | `ComponentBundle` 16 条 + `FullConfigTemplateItem` 8 条（`pool_singbox_001`/`pool_xray_001`/`singbox_base_001`/`singbox_default_001`/`singbox_test_001`/`xray_base_001`/`xray_default_001`/`xray_test_001`，**必须带 `BundleChain`/`TemplateType`/`BundleVersion`**，照抄 `scripts/sync_db_schema.sql` 的 D1/D2 段与 `test/guiNDB_empty.db` 的取值）；`SqlQuery` 仍由现有 `seedDefaults()` 写入，不在 SQL 里重复 |

> **为什么模板种子必须进 SQL**：`FullConfigTemplateDAO::seedDefaultTemplates()`（`src/FullConfigTemplateDAO.cpp:139-500`）从不给 `bundleChain`/`templateType` 赋值，`save()` 原样绑定空串；而 `isTemplateInSync()`（同文件 759-760 行）把空 `BundleChain` 视为「已同步」永不修复。若沿用 C++ 种子器，新建库会带 8 条 `BundleChain` 为空的模板，组件链无法解析，与 `bin/guiNDB.db`（BundleChain 37–133 字节）行为不一致。
>
> **边界**：8 条模板种子只在 `FullConfigTemplateItem` 为空时生效（新建库即满足）。对已有 8 行但 `BundleChain` 为空的存量库，本次不改数据（不做就地补齐、不 ALTER），仍按 `Ok` 直接使用 —— 回填属独立迁移，见 §9。

执行方式：`sqlite3_exec` 单次执行整段脚本（**下发的脚本本身**只有 `CREATE ... IF NOT EXISTS` / `INSERT OR IGNORE`，没有 `ALTER`，因此不存在"重复列中断后续语句"的坑），外层包 `BEGIN IMMEDIATE ... COMMIT`。

`create()` 使用**自己新建的连接**，并在其上手设 `sqlite3_busy_timeout(db, 10000)`；不要依赖 `DatabaseConnectionService::applyPragmas()`（那里是 5000 ms，且额外建了 `idx_profile_dedup`）。两者数值不同是有意的：启动建库容忍更久的等待，运行期维持原 5 s。

**失败处置必须区分目标文件是否由本次调用创建**（否则对既有文件执行失败会误删用户库）：

```cpp
const bool existedBefore = std::filesystem::exists(path);
// ... open + BEGIN IMMEDIATE + exec ...
// 失败：
//   ROLLBACK
//   仅当 !existedBefore 时 std::filesystem::remove(path)   // 只清理本次造出来的半成品
//   既有文件：保留原文件、返回 false + error（绝不删除）
```

对**已存在**的库，失败后的状态定义如下（避免与上面的"不删文件"互相矛盾）：

- 已 `COMMIT` 的 `CREATE TABLE IF NOT EXISTS` / `CREATE INDEX IF NOT EXISTS` 保留。它们只增不减，属于可重入的中间态。
- 未提交的 INSERT（种子行）由 `ROLLBACK` 丢弃；下次重跑 `INSERT OR IGNORE` 补齐。
- `error` 中列出「已补建的对象」与「仍缺的对象」差集，便于用户/日志判断。
- **绝不会**因为中间态而删除文件或丢数据。

对已存在路径执行时，脚本必须是**纯增量**的：`CREATE TABLE IF NOT EXISTS` / `CREATE INDEX IF NOT EXISTS` / `INSERT OR IGNORE`，绝不 `DROP`、`DELETE`。

**本次不做任何 `ALTER`**：`MissingCoreTables`（含"表在但列缺"）不再就地补列，直接走 §4.5 的备份重建分支 —— 旧库原样备份、新库按 `schema_init.sql` 全新建。因此 `schema_init.sql` 内部**完全没有 `ALTER`**，`create()` 也不需要列基线或盘类型判断（那些配套随就地补齐一起推迟到 v2，见 §9）。

**连接与语句必须在所有返回路径释放**：`create()` / `inspect()` 里每一个 `sqlite3*` 都要在成功、失败、early-return 三个出口 `sqlite3_close()`；所有 `sqlite3_prepare_v2` 出来的 stmt 都要 `sqlite3_finalize()`。Windows 上句柄不关会锁住 `.db`/`-wal`/`-shm`，直接导致用例的临时目录 `remove_all()` 失败、以及同进程第二次 `create()` 打不开文件。

**PRAGMA 与事务的顺序**：`journal_mode=WAL` 在事务内是静默 no-op，必须在 `BEGIN IMMEDIATE` **之前**单独 `sqlite3_exec`。`schema_init.sql` 内不写任何 `PRAGMA`；WAL 由 `create()` 在打开连接后、开事务前设置一次。

**列顺序必须与迁移库一致**：`proxy_runtime_history.socks_port` 在既有库里是 `ALTER TABLE ADD COLUMN` 追加到**末尾**（实测 `bin/guiNDB.db` 为 `id,index_id,started_at,ended_at,exit_code,duration_ms,source,pid,socks_port`），而 `test/guiNDB_empty.db` 只有前 8 列。`schema_init.sql` 必须按"末尾追加"的顺序声明该列，否则新建库与迁移库列序不同 —— 任何 `SELECT *` 或按位置 `sqlite3_column_*` 绑定的代码会在两种库上取到错位数据。用例 18 锁定此顺序。

**DDL 权威来源（必须是可入库的 fixture）**：`bin/guiNDB.db` 与 `test/guiNDB_empty.db` 都被 `.gitignore` 忽略（实测 `git check-ignore -v` 命中 `.gitignore:26 /bin/` 与 `.gitignore:3 /*`，且 `git ls-files test` 为空），**干净检出里并不存在**。因此不能把它们当测试基线。

做法：由开发者在本地对 `bin/guiNDB.db` 执行 `sqlite3 .schema`，把生成的纯文本 DDL 提交为 **`tests/fixtures/db_schema_baseline.sql`**（schema-only，无数据，体积小、可 review、可 diff）。`resources/schema_init.sql` 与用例 7/18 都以这份 fixture 为唯一基线：

- `resources/schema_init.sql` 的表/列/列序/索引文本必须与 fixture 逐项一致；
- 用例 7/18 在内存库里执行 `schema_init.sql`，再与 fixture 解析出的结构比对，**不读任何 `.db` 二进制文件**。

8 张 v2rayN 兼容表的列定义来源同上（fixture 由 `bin/guiNDB.db` 导出，实测 `ProfileItem` 36 列、`proxy_runtime_history` 的 `socks_port` 在末尾）。

### 4.4 组件设计

新增 `include/service/DatabaseBootstrap.h` / `src/service/DatabaseBootstrap.cpp`。

该 .cpp **只依赖 sqlite3 + std::filesystem + Logger，不依赖 wxWidgets**，因此 `CORE_SOURCES` 里的两个可执行目标（`validproxy-cli`、`validproxy`）都可直接编译；`tests/` 侧只需一个独立 `add_executable(test_database_bootstrap ... src/service/DatabaseBootstrap.cpp)` + `gtest_main`，不必重复登记进 `CORE_SOURCES`。

```cpp
namespace service {

enum class DbProbe {
    Ok, MissingPath, MissingDirectory, MissingFile, EmptyFile, WalPending,
    Inaccessible, WALReadOnlyBlocked, BusyOrLocked, NotADatabase,
    MissingCoreTables, BackupRecoverable
};

struct DbInspection {
    DbProbe probe = DbProbe::Ok;
    std::string detail;      // sqlite errmsg 或缺失表/列清单（写日志/弹窗用）
    std::string backupPath;  // 仅 BackupRecoverable 非空：可恢复的 .bak.<ts>
};

class DatabaseBootstrap {
public:
    static DbInspection inspect(const std::string& path);              // 只读
    static bool create(const std::string& path, std::string& error);   // 幂等建库；失败时不删除既有文件
    static std::string defaultTargetPath(const std::string& exeDir,
                                         const std::string& configuredPath);
    // createTargetPath 恒等于 defaultTargetPath(exeDir, configuredPath)：本版不做就地补齐，
    // 任何 probe 都把目标推导到 exe 目录。保留该函数只为 v2 恢复"原路径"分支时无需改调用点。
    static std::string createTargetPath(DbProbe probe, const std::string& exeDir,
                                        const std::string& configuredPath);
    // 自动建库前的安全闸：返回 false 表示 target 已是可用库/不安全，必须中止。
    // 供 UI 层与 VALIDPROXY_DB_AUTOCREATE 共用（测试目标不链接 main_gui，故必须是纯函数）。
    static bool mayCreateAt(const std::string& exeDir, const std::string& configuredPath,
                            std::string& target, std::string& error);
    // 把 <path>.bak.<ts> 改名回 path；供 PromptRecover 与测试复用。
    static bool restoreFromBackup(const std::string& backupPath, const std::string& path,
                                  std::string& error);
    // rename 语义：<path>.bak.<yyyyMMdd-HHmmss>；跨卷时先 copy 到 <path>.bak.<ts>.partial，
    // 校验大小 + quick_check 通过后再 rename 成最终名。`.partial` 永不作为恢复候选。
    // 目标备份名已存在 → 返回 false，不覆盖。
    static bool backupFile(const std::string& path, std::string& backupPath);
};

} // namespace service
```

配套纯策略函数（可单测，UI 层只做 switch）：

```cpp
enum class DbStartupAction { Continue, PromptCreate, PromptRebuild, PromptRecover, Abort };

inline DbStartupAction dbStartupAction(DbProbe probe) {
    switch (probe) {
        case DbProbe::Ok:               return DbStartupAction::Continue;
        case DbProbe::MissingPath:
        case DbProbe::MissingDirectory:
        case DbProbe::MissingFile:
        case DbProbe::EmptyFile:         return DbStartupAction::PromptCreate;
        case DbProbe::MissingCoreTables: return DbStartupAction::PromptRebuild;  // 结构不合格 = 按损坏处理，不做就地补齐
        case DbProbe::WalPending:        return DbStartupAction::Continue;  // 先 checkpoint 再重新探测
        case DbProbe::NotADatabase:      return DbStartupAction::PromptRebuild;
        case DbProbe::BackupRecoverable: return DbStartupAction::PromptRecover;
        case DbProbe::Inaccessible:      return DbStartupAction::Abort;  // 权限/网络盘：不回写配置
        case DbProbe::WALReadOnlyBlocked:return DbStartupAction::Abort;  // 库有效但目录不可写
        case DbProbe::BusyOrLocked:      return DbStartupAction::Abort;  // 另一实例占用：绝不重建
    }
    return DbStartupAction::Abort;
}
```

`DatabaseConnectionService` 增加打开模式，**默认值保持旧行为**（CLI 13 处调用点不经过此函数，2 处调用点 `main_gui.cpp` / `UIApp.cpp` 零改动即可编译）：

```cpp
enum class OpenMode { ReadWriteCreate, ReadWriteExisting };
sqlite3* open(const std::string& path, OpenMode mode = OpenMode::ReadWriteCreate);
```

**`mode` 只切换读取/创建位，`SQLITE_OPEN_FULLMUTEX` 在两种模式下都必须保留**（现状是 `READWRITE|CREATE|FULLMUTEX`，见 `DatabaseConnectionService.cpp:12`）。丢掉 FULLMUTEX 会静默降级为 NOMUTEX，与本项目多线程共享同一连接的既有前提冲突：

```cpp
int flags = SQLITE_OPEN_FULLMUTEX;
flags |= (mode == OpenMode::ReadWriteCreate) ? (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)
                                             :  SQLITE_OPEN_READWRITE;
```

### 4.5 启动流程（`main_gui.cpp`）

```
配置加载完成
  ↓
dbPath = appConfig->database_path
  ↓
inspection = DatabaseBootstrap::inspect(dbPath)
  ↓
switch (dbStartupAction(inspection.probe))
  Continue         → 直接进入打开
                     （若 probe == WalPending：先 READWRITE 打开 + `PRAGMA wal_checkpoint(TRUNCATE)`，再重新 inspect()；结果非 Ok 则按新 probe 重新走一次本 switch）
  Abort            → 按 probe 分支弹不同文案（三者补救动作不同，不能共用一个弹窗）：
                     `Inaccessible`：「无法访问数据库路径：\n<dbPath>\n\n<原因>\n请检查文件权限、网络盘是否连通，或该路径是否被目录占用。」
                     `WALReadOnlyBlocked`：「数据库文件有效，但所在目录不可写：\n<dbPath>\n\nSQLite 的 WAL 模式需要在该目录创建 -shm/-wal 文件。请授予该目录写权限后重新启动。」
                     `BusyOrLocked`：「数据库正被其他实例占用：\n<dbPath>\n\n请关闭其他正在运行的实例后重新启动。」
                     → Logger + return 1（**不回写 config.json**）
  PromptRecover   → MessageBoxW(MB_YESNO|MB_DEFBUTTON2, 标题「恢复数据库备份」,
                     "上次数据库重建未完成：\n<dbPath>\n\n发现备份：<bakPath>\n是否用该备份恢复？")
                      否 → MB_OK「备份**未恢复**，已改名为 `<dbPath>.orphan.<ts>` 保留在原处；本次将按不可用处理」+ 把备份改名到 `.orphan.<ts>`
                           + 把 probe 视作 MissingFile 转入 PromptCreate 分支（target 由 createTargetPath 重新推导）
                      是 → 把 bakPath 改名回 dbPath；改名失败 → 弹错 return 1
                           inspect(dbPath) == Ok → MB_OK「已从备份恢复：\n<bakPath>\n备份时间：<时间戳>（取自文件名）\n注意：其中的数据可能早于你记忆中的状态。」+ 继续（跳过 create）
                           否则 → 弹错 return 1（备份本身不可用，不静默新建）
  PromptCreate     → MessageBoxW(MB_YESNO, 见下方文案)
                       否 → MB_OK 提示「请修正 config.json 的 database.path 后重新启动」+ return 1
                       是 → target = createTargetPath(inspection.probe, exeDir, dbPath)
                            若 target == 原 dbPath：
                                MissingFile/EmptyFile 走「create 新库」（该路径已确认为空或不存在）
                                MissingPath 的 dbPath 是空串，永不等于非空 target，此子项不可达
                            否则（目标在 exe 目录，尚未被探测）：
                                t = inspect(target)
                                t.probe == Ok                        → 采用已有可用库，跳过 create
                                t.probe ∈ {MissingFile, EmptyFile}    → create(target) → 失败则弹错 + return 1
                                t.probe == MissingCoreTables         → 转 PromptRebuild(target)（结构不合格同损坏处理）
                                t.probe == NotADatabase               → 转 PromptRebuild(target)
                                t.probe == BackupRecoverable         → 用 target 的 `.bak.<ts>` 恢复，或提示清理后重试 + return 1
                                t.probe ∈ {Inaccessible, WALReadOnlyBlocked, BusyOrLocked}→ 按上方 Abort 文案处理 + return 1
                                其它（target 已是目录等真冲突）        → 弹错给出 target 绝对路径与「请删除或改名该路径后重启」+ return 1
                            dbPath = target
                            target != 原配置路径 → 回写 config.json（§4.6）并提示「config.json 已更新」
                            appConfig->database_path = dbPath   // 同步内存，避免后续保存配置改回死路径
                            MB_OK 提示「已在 <target> 就绪数据库（新建库为空，无订阅/无代理）；请导入订阅」
  PromptRebuild    → MessageBoxW(MB_YESNO|MB_DEFBUTTON2, 见下方文案)
                       否 → return 1（原文件字节不变）
                       是 → 前置校验 1：**被处理的文件**（正常情况下是 dbPath，从 PromptCreate 转入时是 target）必须位于 exe 目录或 config.json 所在目录，否则弹错 return 1
                            前置校验 2：对**同一个被处理文件**跑 `PRAGMA quick_check`；仍为 `ok` → 弹错 return 1（文件可正常读取，不重建）
                            victim = 该被处理文件；target = createTargetPath(inspection.probe, exeDir, dbPath)
                            backupFile(victim, bak) 失败 → 弹错 return 1（原文件未动）
                            create(target) 失败 → 把 bak 改名回 victim（尽力恢复）+ 弹错（含 create 错误与恢复结果）+ return 1
                            create(target) 成功 → **删除 bak**（新建库已可用，旧快照不再需要；避免日后 `BackupRecoverable` 用数月前的旧库顶掉新库）
                            dbPath = target；按 §4.6 回写 + 同步内存
  ↓
db = dbService.open(dbPath, OpenMode::ReadWriteExisting)
  ↓
**删除** `main_gui.cpp:131-143` 的 C++ 模板种子调用（`seedDefaultTemplates()`）；`FullConfigTemplateItem` 已由 `schema_init.sql` 种子写入，保留调用会在空库上再写 8 条 `BundleChain` 为空的旧行。
仅保留 `SqlQueryDAO::createTableIfNotExists()` + `seedDefaults()` → wxEntry
```

**跨实例并发**（两个进程同刻启动且都判 `MissingFile`）：双方都会弹窗、都会批准。先在 `<target>` 上 `sqlite3_open_v2(..., READWRITE|CREATE)` 并立刻 `BEGIN IMMEDIATE` 者拿到写锁；后者 `busy_timeout=10000` 到期后返回 `SQLITE_BUSY`，按「另一实例正在初始化，请稍后重试」弹错退出，**不删除**文件。后者下次启动探测到 `Ok`，直接使用前者建好的库。

**弹窗文案（全部 `MessageBoxW` + 宽字面量。标题按动作区分，避免"建空库/恢复备份/销毁重建"在任务栏里看起来一样）**

| 场景 | 标题 |
|---|---|
| PromptCreate (a)/(a′) | `创建新数据库` |
| PromptRecover | `恢复数据库备份` |
| PromptRebuild | `备份后重建数据库` |
| Abort（三类） | `数据库不可用` |

**每套文案的问句本身必须含动词**（Windows 的 `MB_YESNO` 按钮固定渲染为「是/否」，只有问句能表达动作），不能统一写成「是否继续？」。

`PromptCreate` — 分 (a) / (a′) 两套文案，区别只在 `target` 是否等于 `dbPath`。

**（a）可新建分支**（`MissingPath` / `MissingDirectory` / `MissingFile` / `EmptyFile`，且 `target != dbPath`）：

```
数据库不可用：<原因>
当前配置路径：<dbPath>
将创建到：<target>

说明：
  · 将创建一个空数据库，不含任何订阅或代理
  · 确认后将改写 config.json 的 database.path
  · 原配置路径不会被删除，仍保持不可用

是否在 <target> 创建新数据库？
```

**（a′）可新建但路径相同**（同上四态，且 `target == dbPath`，即配置本来就在 exe 目录）：把中间三行换成

```
说明：
  · 将在原路径创建一个空数据库，不含任何订阅或代理
  · 不会改写 config.json，路径保持不变

是否在原路径创建新数据库？
```

**（b）已删除**：`MissingCoreTables` 不再就地补齐，改走 `PromptRebuild`（见 §4.5），因此本 Spec 只有 (a) / (a′) 两套「可新建」文案 + 一套 `PromptRebuild` 文案 + 一套 `PromptRecover` 文案。

`PromptRebuild`（`<victim>` 指真正被备份的文件：正常是 `<dbPath>`；从 PromptCreate 转入时是 `<target>`。**这两种情况文案必须写明备份的是哪个文件**，不能一律写"当前配置路径"。首行原因按 probe 区分：`NotADatabase` → 「文件不是有效的 SQLite 数据库」，`MissingCoreTables` → 「数据库缺少必需的表/列，结构不可用」）：

```
<首行原因>：<原因>
将备份的文件：<victim>
（若 victim != dbPath 追加一行：注意：这不是你的配置路径，而是程序目录下的同名文件）
将备份为：<victim>.bak.<yyyyMMdd-HHmmss>

说明：
  · 备份后会在 <target> 重建一个空数据库（原文件完整保留在备份里，可手工恢复）
  · 新数据库不含任何订阅或代理

是否备份并重建？（默认：否）
```

目标路径规则（`defaultTargetPath`）：`<exeDir>/<配置路径的文件名>`；文件名为空（配置路径为空串、以分隔符结尾，或该路径本身是一个已存在的目录）→ `<exeDir>/guiNDB.db`。"exe 相同目录"是用户要求，**不在配置指向的其他目录建目录**。

路径安全约束（`createTargetPath` / `mayCreateAt` 内部）：对 `<配置路径的文件名>` 做 `lexically_normal()`，拒绝 `.`/`..`/空名/含分隔符/Windows 保留名（`CON`/`NUL`/`AUX`/`COM1`…）/以点或空格结尾；归一化后的结果必须仍是 `<exeDir>` 的直接子项，否则回退 `<exeDir>/guiNDB.db`。**此外拒绝重解析点**：target 或 exe 目录若带 `FILE_ATTRIBUTE_REPARSE_POINT`（符号链接/联接点），一律不写、不删，弹错退出 —— 否则另一本地用户预先放一个指向任意文件的联接，建库/删除就会穿透到目标。

弹窗必须同时显示「当前配置路径」与「将创建到」两个绝对路径；配置指向目录（`MissingDirectory`）时这条差异尤其关键 —— 用户要能看出库不会再落在他指定的那个目录里。

**采用既有可用库的分支**：当 `target != dbPath` 且 `inspect(target) == Ok`，说明 exe 目录下已有一个可用库（例如上次重装/换盘留下的）。此时直接采用它、跳过 `create()`，但必须写 `REPORT` 日志说明「已存在可用库 <target>，采用之」，并把它显示在成功弹窗里 —— 不能静默换库。

**启动期反馈**：`backupFile()` + `create()` + `quick_check` 期间必须有一个**可见**的进度提示 —— 仅 `SetCursor(IDC_WAIT)` 在 162 MB 库上可能持续数秒而毫无反馈，用户会以为卡死并强杀进程，正好落在备份/建库的危险窗口里。做法：用裸 Win32 建一个极简 `WS_POPUP | WS_BORDER` 窗口（一行静态文本，随阶段更新为「校验中…」/「备份中…」/「建库中…」），`wxEntry` 前 `DestroyWindow`。**不要**用 wxWidgets 窗口 —— `wxEntry` 之前 wx 尚未初始化（`main_gui.cpp` 现有 115/126 行用的就是 Win32 `MessageBoxA`，不是 `wxMessageBox`）。

**日志**：`PromptCreate` 批准后写 `REPORT`（probe、target、是否回写配置）；`PromptRebuild` 与 `Abort` 写 `WARN`；`config.json` 回写失败写 `WARN`。日志与弹窗都使用**绝对路径**，原始 sqlite `errmsg` 只进日志、不进弹窗正文（弹窗给可读原因）。

**已知缺口（不在本次范围，见 §9）**：建库成功后的引导只有一次 `MB_OK` +「请导入订阅」，用户点掉后就没有任何提示；更合适的做法是在主窗口顶部挂一条常驻提示（"库为空，旧库仍在 &lt;oldPath&gt;，可从此处导入/切换"）。这需要新增 UI 组件，属独立改动。

**无人值守/UI 自动化**：新增环境变量 `VALIDPROXY_DB_AUTOCREATE=1`（与既有 `VALIDPROXY_ASSERT_LOG=1` 同一模式），**仅对 `PromptCreate` 生效**，命中时视为「是」并跳过弹窗。`NotADatabase` 的备份重建**不受该变量影响**，需显式设置 `VALIDPROXY_DB_AUTOREBUILD=1`；未设置时记录 `WARN` 并 `return 1`，提示运维改为交互式启动。该变量也不改变「`Abort` 分支不回写配置」的约束。

**自动化保护（必须实现，否则 UI 沙箱会被误建库）**：`VALIDPROXY_DB_AUTOCREATE=1` 生效前必须先调 `DatabaseBootstrap::mayCreateAt(exeDir, configuredPath, target, error)`；它内部完成 §4.4 的目标路径校验与 `inspect(target)`，若 `target` 已存在且 `inspect(target) == Ok`（例如沙箱把 `database.path` 指向一个已有 153 MB 测试库，只是父目录暂时不可写），**返回 false，启动流程不覆盖、不采用、直接 `return 1` 并写 `ERR`**。无人值守路径只允许"从无到有"，绝不允许"接管/替换已有库"。

**注意 `scripts/prepare-ui-sandbox.bat` 的行为**：该脚本每次都用 `copy /y` 把 `test/guiNDB_empty.db` 重置为 `test/ui-sandbox/ui-test.db`，并把 `database.path` 指向它。因此沙箱里 `ui-test.db` **总是存在**，正常路径根本不会触发 `PromptCreate`；`VALIDPROXY_DB_AUTOCREATE=1` 只会在沙箱被手工破坏（删掉 `ui-test.db`）时起作用，这正是它需要的场景。

### 4.6 config.json 回写

- 只在 `dbPath != appConfig->database_path` 时执行。
- 用 `boost::json` 读原文件 → 只改 `database.path` → 写回；**不使用 `ConfigReader::save()`**（后者从 `AppConfig` 全量序列化，会丢掉未知键）。
- **同时改写 `sync.target_db`**：若其原值与旧 `database.path` 相等，一并更新为新路径，避免同步目标与当前库分叉。
- **原子写**：写 `config.json.tmp`（同目录）→ flush/close → 把原文件复制为 `config.json.bak` → `rename(tmp, config.json)`。任一步失败保持原文件不变。
- **异常处理**：`ConfigFileStore::write()` 会抛异常，必须包在 `try/catch (const std::exception&)` 内；捕获后记 `WARN`，本次会话继续用新库。
- **写失败的用户可见面**：`wxEntry` 之前弹一次 `MB_OK|MB_ICONWARNING`，说明 `config.json` 路径、需要手工把 `database.path` 改成什么、以及本次实际使用的库路径（不能只写状态栏——那时还没有窗口）。
- **内存同步**：回写成功后必须执行 `appConfig->database_path = dbPath`，否则后续任何一次配置保存都会把 `database.path` 写回旧值。

## 5. 文件级改动清单

| 文件 | 改动 |
|---|---|
| `resources/schema_init.sql` | 新增：11 表 + 3 索引 + 16 条 `ComponentBundle` 种子 + 8 条 `FullConfigTemplateItem` 模板种子（含 `BundleChain`/`TemplateType`/`BundleVersion`） |
| `tests/fixtures/db_schema_baseline.sql` | 新增：由 `bin/guiNDB.db` 的 `sqlite3 .schema` 导出的**纯文本**结构与列序基线（该 .db 本身被 `.gitignore` 忽略，不能作为基线） |
| `resources/DatabaseSchemaInit.cpp.in` | 新增：`configure_file` 模板，输出 `R"SQLINIT(@SCHEMA_INIT_SQL@)SQLINIT"` |
| `CMakeLists.txt` | 1) `file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources)` + `file(READ ...)` + `configure_file(@ONLY)`（目录不存在时首次 configure 直接失败）；2) `set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/schema_init.sql)`；3) 新增 `set(SCHEMA_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/DatabaseSchemaInit.cpp)`，**同时**追加进 `CORE_SOURCES` 与测试目标（否则测试目标缺 SQL 符号会链接失败）；4) `add_executable(test_database_bootstrap tests/test_database_bootstrap.cpp src/service/DatabaseBootstrap.cpp ${SCHEMA_SOURCES})`，链接 `gtest_main sqlite3`，并 `target_compile_definitions(... DB_SCHEMA_BASELINE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/db_schema_baseline.sql")`（**只指向入库的纯文本 fixture，不读任何 `.db`**）；5) `add_test(NAME DatabaseBootstrapTest COMMAND test_database_bootstrap)`（本仓库 `-R` 匹配 `add_test` 的 NAME，不是目标名） |
| `include/service/DatabaseBootstrap.h` | 新增：`DbProbe`/`DbInspection`/`DatabaseBootstrap`/`DbStartupAction`/`dbStartupAction` |
| `src/service/DatabaseBootstrap.cpp` | 新增：探测、建库、备份、目标路径推导 |
| `include/service/DatabaseConnectionService.h` | 新增 `OpenMode`，`open(path, mode = ReadWriteCreate)` |
| `src/service/DatabaseConnectionService.cpp` | 按 mode 组装 `sqlite3_open_v2` 标志 |
| `src/main_gui.cpp` | 插入探测 → 弹窗（`MessageBoxW`）→ 建库 → 原子回写配置 + 同步内存 `database_path`；启动打开改用 `ReadWriteExisting` |
| `tests/test_database_bootstrap.cpp` | 新增 Google Test（见 §6） |
| `docs/DEV-PROCESS.md` | 规则 3 追加：新建库 DDL 变更须同时更新 `resources/schema_init.sql` 与 `scripts/sync_db_schema.sql`（**仅当 §9 的「单脚本合并」不被采纳时**；采纳则不新增此规则） |

## 6. 测试计划（TDD，先红后绿）

`tests/test_database_bootstrap.cpp`（`TEST_F`，全部用 `std::filesystem::temp_directory_path()` 下的唯一子目录，用例结束清理）：

| # | 用例 | 断言 |
|---|---|---|
| 1 | `InspectMissingPathReturnsMissingPath` | 空串 → `MissingPath` |
| 2 | `InspectMissingFileReturnsMissingFile` | 目录存在、文件不存在 → `MissingFile`，且**不产生文件** |
| 3 | `InspectMissingDirectoryReturnsMissingDirectory` | 父目录不存在 → `MissingDirectory` |
| 4 | `InspectEmptyFileReturnsEmptyFile` | 建 0 字节文件 → `EmptyFile` |
| 5 | `InspectNonSqliteFileReturnsNotADatabase` | 写入 `"not a database"` → `NotADatabase` |
| 6 | `InspectValidButTablelessDbReturnsMissingCoreTables` | 空 SQLite 库 → `MissingCoreTables`，`detail` 含缺失表名 |
| 7 | `CreateProducesCompleteSchema` | 在临时库上 `create()`，再用 `tests/fixtures/db_schema_baseline.sql` 解析出的结构比对：11 张表按**表名逐个**存在（不数 `sqlite_master`，会含 `sqlite_sequence`）；3 个索引齐全；每张表的列名**与列序**逐位置相等；`PRAGMA quick_check == ok`；`ComponentBundle` 16 行；`FullConfigTemplateItem` 8 行且 `BundleChain` 均非空；`inspect() == Ok`（**不读取任何 `.db` 二进制**，fixture 入库可复现） |
| 8 | `CreateIsIdempotent` | 连续两次 `create()` 均成功；行数不变（种子 `INSERT OR IGNORE` 不重复） |
| 9 | `CreateFailsWhenParentMissing` | 直接调用 `create("<不存在目录>/x.db")` → 返回 false 且 `error` 非空（**不建目录**；正常流程的 target 恒在 exe 目录，本用例锁定函数契约） |
| 10 | `CreateFailsOnReadOnlyDirectory` | 只读目录（可用 ACL 或跳过）→ 返回 false 且 `error` 非空 |
| 11 | `DefaultTargetPathUsesExeDir` | 配置为 `D:/x/y/guiNDB.db` → `<exe>/guiNDB.db`；配置为空 → `<exe>/guiNDB.db` |
| 12 | `StartupActionMapping` | 12 种 `DbProbe` → 期望的 `DbStartupAction` 全表覆盖（含 `Inaccessible`/`WALReadOnlyBlocked`/`BusyOrLocked` → `Abort`、`BackupRecoverable` → `PromptRecover`、`WalPending` → `Continue`） |
| 13 | `BackupFileRenamesWithTimestamp` | 备份后 `<path>.bak.<ts>` 存在且内容一致、**原路径不再存在**；同名备份已存在 → 返回 false 且不覆盖 |
| 14 | `CreateTargetPathAlwaysUsesExeDir` | 任何 probe（含 `MissingCoreTables`）→ 一律返回 `defaultTargetPath(exeDir, configuredPath)`；不再有 “就地补齐原路径” 分支 |
| 15 | `CreateDoesNotDeleteExistingFileOnFailure` | 对已存在的库注入失败（如预先 `BEGIN EXCLUSIVE` 占锁）→ 返回 false，**原文件字节不变** |
| 16 | `InspectBusyDatabaseReturnsBusyOrLocked` | 在 **DELETE journal 模式**的库上由另一连接 `BEGIN EXCLUSIVE` 持锁 → `inspect()` 读 `sqlite_master` 得到 `SQLITE_BUSY`/`LOCKED` → `BusyOrLocked`，而非 `NotADatabase`（注意：WAL 库允许并发读者，构造不出该场景） |
| 17 | `InspectMissingColumnReturnsMissingCoreTables` | 有 4 张表但缺 `ProfileItem.Region` → `MissingCoreTables` |
| 18 | `CreatedProxyRuntimeHistoryColumnOrderMatchesMigratedDb` | `create()` 后 `proxy_runtime_history` 的列名序列与 `tests/fixtures/db_schema_baseline.sql` 中该表的列序**逐位置相等**（`socks_port` 在末尾），锁死「新建库 ≠ 迁移库列序」的隐患 |
| 19 | `CreateTargetPathRejectsUnsafeFilename` | 配置为 `..`、`.`、`CON`、`x.db:`、`a. `、`C:/x/../../victim.db` → 均回退 `<exe>/guiNDB.db`，不产生 exe 之外的路径 |
| 20 | `AutoCreateRefusesToAdoptExistingDatabase` | 直接调用 `DatabaseBootstrap::mayCreateAt()`：target 已是 `Ok` 库 → 返回 false、`error` 非空、不写文件 |
| 21 | `OrphanBackupIsOfferedForRecovery` | 配置路径不存在、同目录存在其 `.bak.<ts>` → `BackupRecoverable`＋`backupPath` 非空（不判 `MissingFile`）；`restoreFromBackup()` 后 `inspect() == Ok` |
| 22 | `InspectValidWalDbOnReadOnlyDirReturnsWalReadOnlyBlocked` | 合法头 + 只读目录 → `WALReadOnlyBlocked`（不是 `Inaccessible`、不提示"文件损坏"） |
| 23 | `InspectRandomFileWithSqliteHeaderIsNotInaccessible` | 头合法但内容随机的文件 → `NotADatabase`，不因 `CANTOPEN` 误判为权限问题 |
| 24 | `MissingCoreTablesRoutesToRebuild` | `dbStartupAction(MissingCoreTables) == PromptRebuild`；且 `create()` 全程不发出任何 `ALTER TABLE` |
| 25 | `CreateTargetRejectsReparsePoint` | exe 目录或 target 带 `FILE_ATTRIBUTE_REPARSE_POINT` → `mayCreateAt()` 返回 false、不写不删 |
| 26 | `WalPendingCheckpointsInsteadOfCreating` | 主库 0 字节 + 含数据的 `-wal` → 探测 `WalPending`，checkpoint 后 `inspect() == Ok` 且**不新建**、原数据仍在 |
| 27 | `BackupPartialFileIsNeverRecoverable` | 跨卷备份中断留下 `.bak.<ts>.partial` → `inspect()` 不将其作为 `BackupRecoverable` 候选 |
| 28 | `SuccessfulRebuildDeletesBackup` | `create()` 成功后 `.bak.<ts>` 不再存在，后续启动不会用旧快照恢复 |

回归：
- `cmake --build build --parallel 8` → 0 error。
- `ctest --test-dir build -R DatabaseBootstrapTest -V` → 全绿。
- `ctest --test-dir build -R "FloatingWidgetStateTest|SqlQueryDAOTest|ConfigReader"` → 无新增失败。
- 手工/UI：把 `bin/config.json` 的 `database.path` 指向 `D:/nope/guiNDB.db` → 启动弹窗 → 批准 → `D:/nope/guiNDB.db` 的父目录不存在（`MissingDirectory`），因此目标落到 `bin/guiNDB.db`、程序进入主界面、`config.json` 被改写；再启动一次不弹窗。

## 7. 风险与非目标

**风险**

| 风险 | 处置 |
|---|---|
| `resources/schema_init.sql` 与 `scripts/sync_db_schema.sql` 漂移 | §6 用例 7 锁定"新建库结构完整"；`docs/DEV-PROCESS.md` 规则 3 明确双文件同步；两者职责分离（全新建 vs 存量升级）写入文件头注释。**是否合并为单脚本 → 见 §9 Open Questions** |
| 种子来源分散：`ComponentBundle` + `FullConfigTemplateItem` 在 SQL，`SqlQuery` 在 C++ | 本 Spec 明确划分：SQL 放 `ComponentBundle`（无 C++ 种子器）与 `FullConfigTemplateItem`（C++ 种子器不写 `BundleChain`）；`SqlQuery` 沿用 `seedDefaults()`；后续若给 `ComponentBundleDAO` 加 `seedDefaults()` 再迁走 |
| 回写 `config.json` 覆盖用户手工格式 | 只改 `database.path`（及必要时 `sync.target_db`）两个键，其余键值原样保留（`boost::json` 读改写，不走 `AppConfig` 全量序列化） |
| `config.json` 写到一半时崩溃导致配置损坏 | 临时文件 + `.bak` + `rename` 原子替换；失败保持原文件 |
| exe 目录不可写（安装在 `Program Files`） | `create()` 失败 → 明确弹错并给出路径、原因与所需权限，不静默降级到临时目录 |
| 误判合法库为"不完整" | 只用 4 张核心表 + 必需列判定；`Ok` 时零副作用、不跑 `quick_check` |
| 误把被占用/无权限的库判成"损坏"并重建 | 拆出 `BusyOrLocked`/`Inaccessible` → `Abort`，只有 `SQLITE_NOTADB`/`SQLITE_CORRUPT` 才进重建分支 |
| 对既有文件执行失败误删用户库 | `create()` 记录目标是否本次新建：新建才删半成品；既有文件只 `ROLLBACK`、从不删除（用例 15 锁定） |
| 多实例并发首次启动同时建库 | `BEGIN IMMEDIATE` + `busy_timeout=10000` + `CREATE ... IF NOT EXISTS` + `INSERT OR IGNORE`；抢锁失败方弹「另一实例正在初始化」并退出，不删文件；下次启动探测到 `Ok` 直接用 |
| 进程在 `backupFile()` 之后、`create()` 之前被杀 → 配置路径暂时无库、只剩 `.bak.<ts>` | 下次启动把同目录 `.bak.<ts>` 作为恢复候选**优先提示**（「发现上次重建留下的备份，是否用它恢复？」），默认「是」；选「否」才走新建流程 |
| 配置文件名畸形（`..`/`.`/保留名/尾随点空格/超长路径/UNC/ADS）导致建库逃出 exe 目录 | `createTargetPath`/`mayCreateAt` 归一化 + 白名单校验：拒绝上述形态并回退 `<exeDir>/guiNDB.db`；用例 19 覆盖 |
| 预置符号链接/联接点让建库或删除穿透到别的文件 | target 或 exe 目录带 `FILE_ATTRIBUTE_REPARSE_POINT` 时一律不写不删、弹错退出；用例 25 覆盖 |
| ~~网络盘就地补齐并发~~ | **已消除**：本次不做就地补齐；结构不合格的库走备份重建（旧库原样进 `.bak`），任何情况下都不修改既有库结构 |
| 用户选「否」拒绝恢复后，备份被后来的空库"掩盖"，再也不会被提示 | 拒绝时把 `.bak.<ts>` 改名为 `.orphan.<ts>` 并明确告知"未恢复"，避免误以为已恢复 |
| 重建成功后旧 `.bak` 长期堆积，日后被 `BackupRecoverable` 用数月前快照顶掉新库 | `create()` 成功后立即删除该 `.bak`；跨卷备份先写 `.partial` 并校验，`.partial` 永不作为恢复候选（用例 27/28） |
| 主库 0 字节但数据还在非空 `-wal` 里，被误判 `EmptyFile` 并新建空库（已提交事务被丢弃） | 新增 `WalPending` 探测态：先 checkpoint 回放 WAL，再重新探测；绝不新建 |
| `bin/guiNDB.db`/`test/guiNDB_empty.db` 被 `.gitignore` 忽略，干净检出无法运行用例 7/18 | 改用入库的纯文本基线 `tests/fixtures/db_schema_baseline.sql` |
| 历史库 8 条模板 `BundleChain` 为空，组件链无法解析 | 本次**不修数据**：新建库已正确写入；存量库列入 §9 独立迁移。发布说明需标注该已知限制，不得声称"所有库已修复" |

**非目标**

- 不改 `scripts/sync_db_schema.sql` 的增量升级职责（是否改为"单脚本 + 条件执行"的合并方案见 §9 Open Questions）。
- 不给 CLI 增加交互式提示（如需，另开 `--init-db` 参数）。
- 不创建缺失的 `config.json` 本身（配置缺失仍按现状报错退出）；只对已存在的 `config.json` 回写 `database.path`。
- 不实现"数据库在线迁移/版本号"体系（表结构版本化是独立议题）。
- 不改 `AppController` 的切库（`switchDatabase`）流程。
- **不做就地补齐**：不修改任何既有库的表/列结构（`MissingCoreTables` 走备份重建）。推迟到 v2，见 §9。

## 8. 验收标准

1. `DatabaseBootstrapTest` 28 例全绿；构建 0 error。
2. 配置指向不存在路径时启动：出现询问弹窗（标题 `数据库初始化`）；选「否」退出码 1 且无文件产生；选「是」生成 `<exeDir>/<配置文件名>`（默认 `guiNDB.db`）且 `inspect() == Ok`，程序进入主界面。**若该路径遗留了 `.bak.<ts>`，走验收 9 的 `PromptRecover`，不适用本条。**
3. 生成后 `config.json` 的 `database.path` 指向新库、内存 `AppConfig::database_path` 同步为新值，二次启动不弹窗；生成后手工触发一次配置保存，`database.path` 不回退。
4. 指向非 SQLite 文件时：弹窗默认「否」；不批准时文件字节不变。
5. 配置路径 == 生成路径（配置本来就在 exe 目录）时：不写 `config.json`，其 mtime 不变。
6. 正常库（`bin/guiNDB.db`）启动：零新增弹窗、零新增日志 WARN、启动耗时无可感知增加（不跑 `quick_check`），行为与改动前一致。
7. 库被另一实例占用（`SQLITE_BUSY`）或路径无权限/网络盘不可达时：弹错退出，文案**按 probe 区分**（占用 → 请关闭其他实例；无权限/网络盘 → 请检查权限与连通性），**不进入重建分支、不回写 `config.json`**。
8. 历史库（8 条模板 `BundleChain` 为空）启动：不因 `Ok` 判定而被永久跳过 —— 该情形按 §9 的独立迁移处理，本次交付需在 §9 标注为已知遗留，且发布说明不得声称已修复。
9. 重建中途崩溃（`.bak.<ts>` 遗留、配置路径无库）：下次启动走 `PromptRecover`，默认「是」恢复；选「否」才允许新建空库。
10. `ctest --test-dir build` 无新增失败（既有环境噪声需给出同签名证据）。

## 9. Open Questions

- **决策**：是否放弃 `resources/schema_init.sql`，改为直接扩展 `scripts/sync_db_schema.sql`（补 8 个 `CREATE TABLE IF NOT EXISTS`）并内嵌该单文件？两种方案都能建库；合并后只有一个 DDL 源，不再需要 §7 的"双文件同步"规则和 `docs/DEV-PROCESS.md` 规则 3 的新增条目，代价是执行期需要容忍 `ALTER TABLE` 的 duplicate-column / already-exists 错误，且"全新建库"与"存量升级"两种语义挤在同一文件里。（scope-guardian）
- **背景**：AGENTS.md §2.2 的目录边界未列出 `resources/`；本方案会新增该顶级目录。若采纳上面的合并方案，该问题一并消失。（feasibility、scope-guardian）
- **决策**：历史库（已有 8 行 `FullConfigTemplateItem`、`BundleChain` 为空）是否在本次一并回填？新建库已由 `schema_init.sql` 写入正确 `BundleChain`，但存量库因 `isTemplateInSync()` 把空串视为"已同步"而永远不会被修复。回填需要一次性迁移逻辑（按模板 Id 匹配写入规范链），并可能覆盖用户自定义过的模板。（adversarial）
- **决策**：`config.json` 是符号链接/重解析点时，原子写（tmp + rename）会替换链接本身而非目标文件。是否需要显式检测并拒绝回写（只 WARN）？本次默认**不处理**，与改动前行为一致。（security-lens）
- **待定**：`utils::getExecutableDir()` 用 `GetModuleFileNameA` + `MAX_PATH` 定长缓冲，超长路径会被截断。这是既有缺陷，本功能沿用；若后续要支持长路径安装目录，需单独改为 `GetModuleFileNameW` + 动态缓冲。（feasibility）
- **决策**：建库成功后的引导是否改为主窗口常驻提示条（而非一次性 `MB_OK`）？需要新增 UI 组件并对接 `MainFrame`/`AppController`，属独立改动。（design-lens）
- **待定**：现有 "Configuration Error"/"Database Error" 两条英文弹窗与新加的中文 `数据库初始化` 系列混用；是否统一为中文标题并合并文案，需产品决定。（design-lens）
- **待定**：`tests/fixtures/db_schema_baseline.sql` 位于 `tests/`，与 AGENTS.md §2.2「测试数据归 `test/`」有张力。实测 `test/` 整体被 `.gitignore:3 /*` 忽略（`git ls-files test` 为空），而 `tests/` 已被 `!/tests/**` 放行（126 个跟踪文件）。因此 fixture **只能**放 `tests/`，否则无法入库、干净检出无法运行用例 7/18。要么接受这一例外并在 DEV-PROCESS 注明，要么改用 `tests/` 之外的既有跟踪目录。（scope-guardian）
- **待定**：日志落盘位置/ACL/轮转策略未定义；弹窗与日志都会写绝对路径（含用户名）。若要支持"一键复制诊断信息"，需单独设计。（security-lens）
- **已决（范围，2026-11-13）**：**就地补齐推迟到 v2**。理由：它是全篇唯一会修改用户既有数据文件、且不做前置备份的路径，配套需要列基线推导、网络盘限制、重解析点检查三套机制，成本远超"路径无效"这一原始诉求。处置：`MissingCoreTables` 保留为探测态（检测便宜且有用），但动作改为走 `PromptRebuild`（旧库原样备份为 `.bak.<ts>`，新库按 `schema_init.sql` 全新建）；`create()` 全程不发出 `ALTER TABLE`；`createTargetPath` 不再有"原路径"分支。v2 若恢复就地补齐，需同时带回：列基线（`:memory:` 执行 schema_init 后取 `table_info`）、`DRIVE_FIXED` 地域限制、对应测试。（scope-guardian；owner 裁决）
