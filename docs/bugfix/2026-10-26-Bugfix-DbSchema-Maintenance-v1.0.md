# 库表维护：`bin\guiNDB.db` 对齐 `SqlQuery` 结构 + `sync_db_schema.sql` / 运行脚本加固

**Goal:** 把同构副库 `bin\guiNDB.db` 补到与项目库一致的新结构（新增 `SqlQuery` 表与 `idx_sql_scope` 索引），并修复数据库维护脚本的破坏性种子、错误默认路径、非 WAL 安全备份、缺少校验等问题，使「脚本产出 = 项目库结构 = 副库结构」持续可验证。

**Tech Stack:** SQLite 3.43.1 / PowerShell 7 / `sqlite3.exe`（`C:\Program Files (x86)\SQLite Expert\Professional 5\sqlite3.exe`）/ `scripts/sync_db_schema.sql` / `scripts/run_sync_db_schema.ps1`

**依据文档:** `docs/DEV-PROCESS.md` 规则 3（库表 Schema 变更后必须同步维护脚本）；`docs/plans/2026-10-26-Spec-SqlLibrary-TaskConfig-v1.0.md`（`SqlQuery` 表由 SQL 方案库功能引入）

**约束:** 本次为纯库结构 + 脚本维护，**不改动任何 C++ 源码**；全栈禁止使用 `auto`；备份后再变更生产侧数据库。

---

## 0. 现场证据

### 0.1 两个库的角色

| 库 | 角色 | 变更前状态 |
| :--- | :--- | :--- |
| `bin\worker\guiNDB.db` | **项目库** = `database.path`；`sync.target_db` | 已有 `SqlQuery`（3 行：`default_all` / `default_by_subid` / `快速sql`） |
| `bin\guiNDB.db` | **同构副库** = `sync.source_db`（v2rayN 侧） | **10 张应用表，无 `SqlQuery`** |

`bin\config.json` 当前配置：`sql_query_id=1`、`sql_by_subid_id=2`、`sync.source_db=bin\guiNDB.db`、`sync.target_db=bin\worker\guiNDB.db`、`sync_skip_subids=true`。

`SubitemUpdaterV2::syncDatabases` 只同步 `ProfileItem` / `ProfileExItem` / `SubItem` **数据**，不同步 schema，因此副库结构必须由维护脚本单独对齐。

### 0.2 变更前结构比对

- 副库 10 张应用表：`ComponentBundle`、`DNSItem`、`FullConfigTemplateItem`、`ProfileExItem`、`ProfileGroupItem`、`ProfileItem`、`RoutingItem`、`ServerStatItem`、`SubItem`、`proxy_runtime_history`。
- 项目库 11 张应用表：上述 10 张 + `SqlQuery`。
- 副库其余列、索引已在此前维护中对齐（`Region`、`ProfileExItem` 4 个运行统计列、`FullConfigTemplateItem` 4 个组件链列、`socks_port`、`idx_profile_dedup`、`idx_runtime_history_index`）。
- 唯一实质差异：副库缺 `SqlQuery` 表与 `idx_sql_scope` 索引。

### 0.3 变更前维护脚本的问题

| # | 问题 | 影响 |
| :--- | :--- | :--- |
| P1 | `SqlQuery` DDL 被追加在验证段**之后** | 结构上不属于 DDL 区，后续维护易漏读/误删 |
| P2 | D1/D2 种子数据使用 `DELETE FROM` + `INSERT OR REPLACE` | 每次执行都会**删除并覆盖**用户自定义的组件包 / 配置模板 |
| P3 | `run_sync_db_schema.ps1` 默认 `-Db` 由 `Join-Path` 拼接绝对路径 | 默认路径非法，必须手工传参（历史 R5） |
| P4 | 备份使用 `Copy-Item` | 数据库处于 WAL 模式时可能得到不一致副本 |
| P5 | 无「只校验」模式；校验仅覆盖 3 项 | 无法在不改库的情况下验收，也发现不了 `SqlQuery` 缺失 |

---

## 1. 修复方案

### 1.1 `scripts/sync_db_schema.sql`

1. 头部差异总览更新为「基准库 8 表 → 项目库 11 表」，新增 C4 `SqlQuery` 说明。
2. `SqlQuery` 建表与 `idx_sql_scope` 从文件末尾移入 **C4 DDL 区**（`idx_runtime_history_index` 之后、种子段之前）。
3. 删除 `DELETE FROM "ComponentBundle";` 与 `DELETE FROM "FullConfigTemplateItem";`。
4. 24 条种子语句由 `INSERT OR REPLACE` 改为 `INSERT OR IGNORE`：只补齐缺失的内置行，**保留用户已修改/新增的行**。
5. 验证段补充 `SqlQuery` 列、`SqlQuery` 索引与 `idx_sql_scope` 校验。

### 1.2 `scripts/run_sync_db_schema.ps1`

1. 默认目标改为 `bin\worker\guiNDB.db`（基于 `$PSScriptRoot` 解析），修复 P3。
2. 新增 `-SqliteExe`、`-VerifyOnly`、`-SkipBackup` 参数；`sqlite3.exe` 自动按「显式路径 → PATH → SQLite Expert 常见安装路径」解析。
3. 备份优先使用 `sqlite3 .backup`（WAL 安全），失败才回退 `Copy-Item` 并告警。
4. 语句切分改为引号/方括号/注释感知的状态机，正确处理种子 JSON 中的 `;` 与 `''`、`""` 转义；仍保持逐条 `.read` 执行，避免 PowerShell 吞掉 JSON 双引号。
5. 校验扩展为：11 张标准表、`ProfileItem.Region`、`ProfileExItem` 4 列、`FullConfigTemplateItem` 4 列、`socks_port`、`SqlQuery` 8 列、3 个关键索引、种子表行数下限、`PRAGMA integrity_check`。

### 1.3 副库对齐执行

先在同构副本上验证，再对 `bin\guiNDB.db` 执行：

```powershell
# 1) 副本演练
.\scripts\run_sync_db_schema.ps1 -Db 'C:\Users\dsm\AppData\Local\Temp\opencode\guiNDB_schema_test2.db'

# 2) 正式对齐（自动备份）
.\scripts\run_sync_db_schema.ps1 -Db .\bin\guiNDB.db

# 3) 只校验
.\scripts\run_sync_db_schema.ps1 -Db .\bin\guiNDB.db -VerifyOnly
```

正式执行备份：`bin\guiNDB.db.bak-20261026-121250`。

---

## 2. 验证结果

| 项目 | 结果 |
| :--- | :--- |
| 解析语句数 | **40** |
| 副本首轮执行 | 成功 **30** / 跳过 **10** / 失败 **0** |
| 副本二轮执行（幂等） | 成功 **30** / 跳过 **10** / 失败 **0** |
| 正式副库执行 | 成功 **30** / 跳过 **10** / 失败 **0** |
| `-VerifyOnly` | 全部 PASS |
| 标准表数量 | **11** |
| `SqlQuery` 列数 | **8** |
| 关键索引 | **3**（`idx_profile_dedup` / `idx_runtime_history_index` / `idx_sql_scope`） |
| `ComponentBundle` 行数 | **16**（未被种子覆盖） |
| `FullConfigTemplateItem` 行数 | **8**（未被种子覆盖） |
| `integrity_check` | **ok** |

10 条「跳过」均为既有列/表/索引的 `duplicate column` / `already exists`，由脚本自动识别，属预期幂等行为。

---

## 3. 影响与后续注意

1. **副库 `SqlQuery` 为空是预期**：默认方案由程序启动时 `SqlQueryDAO::seedDefaults()` 依据 `config.database.sql` / `sql_by_subid` 写入；副库只补结构，不复制运行库方案。
2. **种子改为非破坏性后**，维护脚本不再保证内置组件包/模板一定是最新内容；如需升级内置种子，应先比对并单独处理，不能依赖 `REPLACE` 静默覆盖。
3. 测试库 `test\guiNDB.db` / `test\guiNDB_empty.db` 本次未对齐；如测试需要 `SqlQuery`，可另用 `-Db` 指向测试库执行。
4. 运行时 `bin\worker\guiNDB.db` 未做结构变更（本就是基准），本次只运行了 `-VerifyOnly`。

---

## 4. 附录：关键命令证据

```text
目标数据库: E:\eclipse_workspace\multiple_thread_validproxy\bin\guiNDB.db
sqlite3:    C:\Program Files (x86)\SQLite Expert\Professional 5\sqlite3.exe
[OK] 已备份至: ...\bin\guiNDB.db.bak-20261026-121250
[执行] 解析出 40 条语句
[汇总] 成功 30 / 跳过 10 / 失败 0
[PASS] 标准表数量 = 11
[PASS] SqlQuery 列数 = 8
[PASS] 关键索引数量 = 3
[PASS] ComponentBundle 行数 = 16
[PASS] FullConfigTemplateItem 行数 = 8
[PASS] integrity_check = ok
[完成] 全部成功
```
