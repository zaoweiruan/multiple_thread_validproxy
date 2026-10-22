# 库表 Schema 同步：`bin\guiNDB.db` 对齐 `bin\worker\guiNDB.db` + `sync_db_schema.sql` 补齐

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:systematic-debugging to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 以项目库 `bin\worker\guiNDB.db` 为基准，把同构副库 `bin\guiNDB.db` 的表结构同步到完全一致，并按 `docs/DEV-PROCESS.md` 规则 3 同步更新 `scripts/sync_db_schema.sql`（补齐此前遗漏的 `socks_port` 列），使「脚本产出 = 项目库结构 = 副库结构」三者收敛。

**Tech Stack:** SQLite 3.43.1 / PowerShell 7 / `scripts/run_sync_db_schema.ps1`（逐条执行 + 自动跳过「列已存在」）

**依据文档:** `docs/DEV-PROCESS.md` 规则 3（库表 Schema 变更后必须同步 `scripts/sync_db_schema.sql`）；`docs/specs/2026-10-21-Spec-StandaloneAdoption-InstanceBoundPortFallback-v1.1.md`（v1.4 起 `proxy_runtime_history` 增 `socks_port` 列）

**约束:** 全栈禁止使用 `auto` 进行类型推导（AGENTS.md §1）；测试源码只允许放在 `tests/`；本次为纯库结构 + SQL 脚本维护，**不改动任何 C++ 源码**。

---

## 0. 现场证据

### 0.1 两个库的角色（`bin\config.json`）

| 库 | 角色 | 大小 | 最后写入 |
| :--- | :--- | :--- | :--- |
| `bin\worker\guiNDB.db` | **项目库** = `database.path`；`sync.target_db` | 184 004 608 B | 2026-10-22 11:02 |
| `bin\guiNDB.db` | **同构副库** = `sync.source_db`（v2rayN 侧） | 160 399 360 B | 2026-10-15 17:15 |

两者均含项目专有表（`ComponentBundle` 16 行 / `FullConfigTemplateItem` 8 行 / `proxy_runtime_history`），即副库**已被本项目的 schema 脚本处理过**，属同族库。

### 0.2 结构比对实测（`sqlite_master` + `pragma_table_info` 逐表逐列）

对象集合一致（10 表 + 2 索引；副库无 `no_tls_view` 视图）。逐列比对后**仅 2 处差异**：

| # | 差异 | 项目库（基准） | 副库 `bin\guiNDB.db` | 性质 |
| :--- | :--- | :--- | :--- | :--- |
| D1 | `proxy_runtime_history` 缺列 | `socks_port INTEGER` | **无该列** | **实质缺失** |
| D2 | `FullConfigTemplateItem.ParentId` 声明文本 | `varchar`（无默认） | `varchar(50) DEFAULT ''` | 仅文本，语义等价 |

其余 8 张表（`ProfileItem` 36 列 / `ProfileExItem` 9 / `SubItem` 15 / `DNSItem` 9 / `RoutingItem` 13 / `ProfileGroupItem` 5 / `ServerStatItem` 6 / `ComponentBundle` 9）**逐列完全一致**；两个索引定义亦逐字一致。

### 0.3 D1 的来源与危害

- `socks_port` 是 v1.4 起「纳管独立代理进程端口回落」引入的**会话级运行参数列**，由运行时 `ProxyRuntimeHistoryDAO::migrateTable()` 以幂等 `ALTER TABLE … ADD COLUMN socks_port INTEGER` 补齐（`src/ProxyRuntimeHistory.cpp`）。
- **`scripts/sync_db_schema.sql` 当时未同步补入**（其 C2 段 CREATE TABLE 与「差异总览」均无 `socks_port`）⇒ **违反 `docs/DEV-PROCESS.md` 规则 3**。后果：任何**不经运行时 DAO**（如由脚本新建、或由上游原生库派生的副库）得到的库都缺该列；`lookupSocksPort()` 要求 `socks_port IS NOT NULL`（`src/ProxyRuntimeHistory.cpp`）⇒ 缺列时端口回落链的「运行时历史层」直接失效。
- 副库 `bin\guiNDB.db` 正是此类情形 ⇒ 与项目库结构漂移。

### 0.4 D2 的等价性论证（决定不重建表的依据）

- SQLite 类型亲和：`varchar` 与 `varchar(50)` 同属 **TEXT** 亲和 ⇒ 存储与比较行为一致。
- 两者均**可空**（`notnull=0`）。
- `FullConfigTemplateDAO` 的 INSERT 显式列出 `ParentId`（`src/FullConfigTemplateDAO.cpp:700`）⇒ **DEFAULT 从不生效**。
- 实测两库 8/8 行的 `ParentId` 取值均为**空串**（`[]`），无 NULL、无差异。
- ⇒ D2 无任何功能影响；重建表属于「对上游原生库做无收益的破坏性操作」，仅作备案（见 §2 D3）。

---

## 1. 根因（RCA）

- **RCA-1（脚本漏项）**：`socks_port` 由 C++ DAO 的 `migrateTable()` 在运行时补齐，属「代码已改、脚本未改」的典型漏项 —— `docs/DEV-PROCESS.md` 规则 3 未被触发执行。
- **RCA-2（副库未随迁）**：副库 `bin\guiNDB.db` 在 v1.4 之前就已建好，其 `proxy_runtime_history` 表早于 `socks_port` 列的引入；此后项目库由 DAO 自动补列、副库无人补 ⇒ 两库逐步漂移。
- **RCA-3（声明来源不同）**：`ParentId` 在项目库由脚本的 `ALTER … ADD COLUMN ParentId varchar` 产出（`src/FullConfigTemplateDAO.cpp:57` 同款），在副库则是 v2rayN 原生自带（`varchar(50) DEFAULT ''`）⇒ 同一列两种声明文本。

---

## 2. 修复方案

### D1：副库结构对齐项目库（`bin\guiNDB.db` ← `bin\worker\guiNDB.db`）

在**单个事务**内对副库执行：

1. `ALTER TABLE proxy_runtime_history ADD COLUMN socks_port INTEGER;`（补 D1；SQLite 元数据操作，不重写表体）；
2. 重建 `FullConfigTemplateItem` 以对齐 `ParentId` 声明（补 D2）：
   - 以**项目库的原始 DDL 文本**建新表 `FullConfigTemplateItem_new`；
   - `INSERT … SELECT` **显式携带 `rowid`** 逐行搬运（保持 rowid 1..8 不变）；
   - `DROP TABLE` 旧表 → `ALTER TABLE … RENAME TO` 回原名。

前置校验（已实测）：`FullConfigTemplateItem` 无任何外键引用（`PRAGMA foreign_key_list` 全表为空）、仅 1 个自动索引（`sqlite_autoindex_FullConfigTemplateItem_1`，由 `Id` 主键自动生成，重建后自动复现）、8 行数据。

### D2：`scripts/sync_db_schema.sql` 同步更新（规则 3 落实）

| 位置 | 改动 |
| :--- | :--- |
| 头部「差异总览」 | 日期 2026-10-14 → 2026-10-22；新增 **C3** 条目（`proxy_runtime_history + socks_port`）；新增「已知良性偏差」备案 2 条 |
| 头部「目标库 / 同构副库」 | 登记 `bin\guiNDB.db` 为同构副库并注明已对齐内容 |
| C2 段 | `CREATE TABLE` 补 `socks_port INTEGER` |
| **C2b 段（新增）** | `ALTER TABLE proxy_runtime_history ADD COLUMN socks_port INTEGER;` —— 新建库走 CREATE（含列）⇒ 该 ALTER 报 duplicate column，由 `run_sync_db_schema.ps1` 自动跳过 ⇒ **幂等安全**；存量库走 ALTER ⇒ 补齐 |
| D 验证段 | 新增 `proxy_runtime_history.socks_port` 的 `pragma_table_info` 校验 |

### D3：良性偏差备案（不改动，仅记录）

1. `FullConfigTemplateItem.ParentId`：脚本的 ALTER **仅在列缺失时生效** ⇒ 上游原生库若已带该列（`varchar(50) DEFAULT ''`），脚本**不会**改写其声明 ⇒ 脚本产出与项目库在该列的声明文本存在差异（仅文本，语义等价，见 §0.4）。副库已由 D1 重建为 `varchar`，与项目库一致。
2. `ComponentBundle` 的 DDL 文本：本脚本为 `PRIMARY KEY, Name …`（逗号后带空格），项目库内该表由 C++ DAO 的 `createTableIfNotExists()` 先行创建（逗号后无空格）⇒ **仅空白差异**，列定义完全一致；脚本用 `CREATE TABLE IF NOT EXISTS`，不会覆盖既有表 ⇒ 不改。

### D4：本次**不做**（范围外）

- 不同步两个库的**数据**（`ProfileItem` / `ProfileExItem` / `proxy_runtime_history` 行内容）：数据同步由既有的 `sync` 功能负责，本次只做结构。
- 不重建 `ComponentBundle`（对齐空白差异）：无功能收益，且属对项目库的破坏性操作。
- 不改 `src/FullConfigTemplateDAO.cpp` / `src/ProxyRuntimeHistory.cpp`：运行时迁移逻辑已正确（幂等 ALTER），本次只是让脚本与其对齐。

---

## 3. 改动清单

- [x] Task 0：备份副库 `bin\guiNDB.db` → `bin\guiNDB.db.bak-<时间戳>`
- [x] Task 1：副库结构对齐（单事务：补 `socks_port` + 重建 `FullConfigTemplateItem`）
- [x] Task 2：`scripts/sync_db_schema.sql` 更新（头部差异总览 / C2 / C2b / 验证段）
- [x] Task 3：脚本幂等性与「脚本产出 == 项目库」验证（临时副本上执行 `run_sync_db_schema.ps1`）
- [x] Task 4：三方比对（项目库 vs 副库 vs 脚本产出）
- [x] Task 5：文档登记（`docs/INDEX.md` §9.1 + `docs/plans/project-plans-tracker.md`）

---

## 4. 验证

### 4.1 结构比对

- 逐表逐列（`sqlite_master` JOIN `pragma_table_info`）比对 **10 张表**：**全部一致**（`COLUMN_DIFF_COUNT=0`）。
- `sqlite_master` DDL 文本比对：仅剩 2 项已知差异 —— `no_tls_view` 视图（脚本明确排除的遗留调试产物）与 `ComponentBundle` 的空白差异（D3-2）。

### 4.2 数据完整性（副库）

| 项 | 值 |
| :--- | :--- |
| `FullConfigTemplateItem` 行数 / rowid | 8 / 1..8 **保持不变**（`pool_singbox_001, pool_xray_001, singbox_base_001, singbox_default_001, singbox_test_001, xray_base_001, xray_default_001, xray_test_001`） |
| `ComponentBundle` 行数 | 16（未动） |
| `proxy_runtime_history` 行数 | 36（未动，`ALTER` 不重写表体） |
| `proxy_runtime_history` 列数 | 9（原 8 + `socks_port`） |

### 4.3 脚本幂等性与产出校验（在副库**改动前**的备份副本上执行）

| 轮次 | 解析语句数 | 成功 | 跳过 | 失败 | 说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 第 1 轮（库缺 `socks_port`） | 40 | 31 | 9 | **0** | 新增的 C2b ALTER **执行成功** |
| 第 2 轮（库已有 `socks_port`） | 40 | 30 | 10 | **0** | C2b ALTER 转为 **duplicate column → 自动跳过** ⇒ 幂等 |

「跳过 9 项」= A1–A3 的 9 条 `ADD COLUMN`（`ProfileItem.Region` 1 + `ProfileExItem` 4 + `FullConfigTemplateItem` 4）均已存在。

### 4.4 三方比对（项目库 vs 副库 vs 脚本产出）

10 张表逐列比对：**项目库 == 副库 完全一致**；「脚本产出 == 副库」仅差 `ParentId` 声明文本 1 项（D3-1，已备案，非本次遗留）。

### 4.5 构建与测试

本次**未改动任何 C++ 源码**，构建与测试基线不变（构建 0 error；定向 ctest 4/4；全量 55/56，唯一失败 `XrayManagerStartupTest` 既有基线）。

---

## 5. 风险与边界

- **R1**：副库重建 `FullConfigTemplateItem` 会改变其物理存储与 rowid 分配策略 —— 已通过显式携带 `rowid` 搬运消除；已确认无外键引用、无触发器、无额外索引。
- **R2**：副库若被上游 v2rayN 重建，`socks_port` 与 `ParentId` 声明会再次漂移 ⇒ 需重新执行本脚本（幂等，可反复执行）。
- **R3**：`run_sync_db_schema.ps1` 的 D1/D2 段含 `DELETE FROM` + `INSERT OR REPLACE`（种子数据重灌）⇒ 对**已被物化改写**的模板库执行会覆盖模板 `Config`。本次只在**临时副本**上验证脚本，未对生产库执行整脚本。
- **R4**：脚本裸跑（不经 `run_sync_db_schema.ps1`）遇「列已存在」会中断 —— 已在脚本头部与 C2b 注释中标注。
- **R5**：`run_sync_db_schema.ps1` 的默认 `-Db` 参数仍指向失效的 v2rayN 路径（`Join-Path` + 绝对路径），必须显式传 `-Db`。

---

## 6. 文档登记

- `docs/INDEX.md` §9.1 新增编号 **104**（本文件）。
- `docs/plans/project-plans-tracker.md`「近期文档引用」表尾追加 1 行（5 列数据行，末列为状态）。

---

## 附录：实施记录（v1.0，已落地）

### A.1 代码/脚本改动表

| # | 文件 | 改动 |
| :--- | :--- | :--- |
| 1 | `bin\guiNDB.db` | 单事务：`ALTER TABLE proxy_runtime_history ADD COLUMN socks_port INTEGER` + 重建 `FullConfigTemplateItem`（对齐 `ParentId` 声明，保留 rowid） |
| 2 | `bin\guiNDB.db.bak-20261022-110735` | 改动前备份（153 MB） |
| 3 | `scripts/sync_db_schema.sql` | 头部差异总览（新增 C3 + 良性偏差备案）、目标库/副库说明、C2 `CREATE TABLE` 补 `socks_port`、新增 **C2b** `ALTER`、D 验证段新增 `socks_port` 校验 |

### A.2 验证结果

- 结构：10 表逐列比对 `COLUMN_DIFF_COUNT=0`；数据 8/16/36 行与 rowid 全部保持。
- 脚本：40 条语句，第 1 轮 31 成功 / 9 跳过 / **0 失败**（C2b ALTER 生效），第 2 轮 30 成功 / 10 跳过 / **0 失败**（C2b 幂等跳过）。
- 三方比对：项目库 == 副库（逐列一致）；脚本产出仅 `ParentId` 声明文本 1 项备案差异。
- C++ 源码未改动 ⇒ 构建与 ctest 基线不变。

### A.3 剩余残留

- `FullConfigTemplateItem.ParentId` 的声明文本在「上游原生库自带该列」时不会被脚本改写（D3-1，仅备案）。
- `ComponentBundle` 的 DDL 空白差异（D3-2，仅备案）。
- `scripts/run_sync_db_schema.ps1` 的默认 `-Db` 参数失效路径（R5），本次未修。
