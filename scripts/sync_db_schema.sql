-- sync_db_schema.sql
-- 把 v2rayN 原生库结构同步为本项目所需结构（增量、幂等）。
--
-- 基准库（唯一事实来源；表结构变更后须据其重新核对本脚本）:
--   E:\v2rayN-windows-64\guiConfigs\guiNDB_Original.db
-- 目标库:
--   bin\worker\guiNDB.db
--
-- ★ 执行方式：必须经 run_sync_db_schema.ps1（自动忽略「列/表已存在」等错误）。
--   裸跑本 .sql 时 ALTER TABLE ADD COLUMN 遇列已存在会直接报错中断。
--
-- 差异总览（2026-10-14 全量比对，基准库 8 表 → 项目库 10 表）:
--   A. 同名表新增列（9 个）
--      ProfileItem            + Region
--      ProfileExItem          + consecutive_failures / start_count
--                                       + total_runtime_ms / crash_count
--      FullConfigTemplateItem + ParentId / BundleChain
--                                       + TemplateType / BundleVersion
--   B. 新增索引（1 个）
--      ProfileItem + idx_profile_dedup
--   C. 新增专有表（2 张）
--      ComponentBundle          组件包（组件链功能核心）
--      proxy_runtime_history   独立代理运行会话历史
--
--   不纳入本脚本：视图 no_tls_view
--      系遗留调试产物（硬编码单个 SubId、表名小写、全仓零引用），
--      不属于应同步的标准结构。
--
-- 规范见 docs/DEV-PROCESS.md「核心规则 · 规则 3：库表 Schema 变更后，
-- 必须同步 scripts/sync_db_schema.sql」

-- ============================================================
-- A1. ProfileItem: 增加 Region 列
-- ============================================================
ALTER TABLE "ProfileItem" ADD COLUMN Region varchar;

-- ============================================================
-- A2. ProfileExItem: 增加连续失败计数与运行统计列
-- ============================================================
ALTER TABLE "ProfileExItem" ADD COLUMN consecutive_failures INTEGER DEFAULT 0;
ALTER TABLE "ProfileExItem" ADD COLUMN start_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE "ProfileExItem" ADD COLUMN total_runtime_ms INTEGER NOT NULL DEFAULT 0;
ALTER TABLE "ProfileExItem" ADD COLUMN crash_count INTEGER NOT NULL DEFAULT 0;

-- ============================================================
-- A3. FullConfigTemplateItem: 增加组件链相关列
--     BundleChain / BundleVersion 供「组件链 → 物化 Config」闭环；
--     ParentId 为旧模板继承机制字段（现已全库置空，保留列以兼容既有代码路径）。
-- ============================================================
ALTER TABLE "FullConfigTemplateItem" ADD COLUMN ParentId varchar;
ALTER TABLE "FullConfigTemplateItem" ADD COLUMN BundleChain varchar;
ALTER TABLE "FullConfigTemplateItem" ADD COLUMN TemplateType varchar;
ALTER TABLE "FullConfigTemplateItem" ADD COLUMN BundleVersion varchar;

-- ============================================================
-- B. ProfileItem: 去重索引（表达式索引，sqlite 3.9+ 支持）
-- ============================================================
CREATE INDEX IF NOT EXISTS idx_profile_dedup
    ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network));

-- ============================================================
-- C1. ComponentBundle: 组件包表（组件链功能核心）
--     列义: Id=组件包ID(主键) / CoreType=2 Xray、24 SingBox
--           Version=版本号 / Components=JSON [{"type":<ComponentType>,"params":{...}}]
--     （注：本段 DDL 须保持单行注释在上方——run_sync_db_schema.ps1 按行解析并会丢弃行内注释）
-- ============================================================
CREATE TABLE IF NOT EXISTS "ComponentBundle" (Id varchar PRIMARY KEY, Name varchar NOT NULL, Description varchar, CoreType INTEGER NOT NULL, Version INTEGER NOT NULL DEFAULT 1, Enabled INTEGER NOT NULL DEFAULT 1, Components varchar NOT NULL, CreatedAt TEXT NOT NULL, UpdatedAt TEXT NOT NULL);

-- ============================================================
-- C2. proxy_runtime_history: 独立代理运行会话历史
--     列义: index_id=代理 IndexId / started_at、ended_at=起止时间
--           duration_ms=运行时长 / source=来源，默认 standalone
-- ============================================================
CREATE TABLE IF NOT EXISTS proxy_runtime_history (id INTEGER PRIMARY KEY AUTOINCREMENT, index_id TEXT NOT NULL, started_at TEXT NOT NULL, ended_at TEXT NULL, exit_code INTEGER, duration_ms INTEGER, source TEXT DEFAULT 'standalone', pid INTEGER);

CREATE INDEX IF NOT EXISTS idx_runtime_history_index
    ON proxy_runtime_history(index_id, started_at);

-- ============================================================
-- D. 验证
-- ============================================================
.mode column
.headers on

SELECT '--- ProfileItem 新增 Region ---' AS info;
SELECT name, type, [notnull] AS nn, dflt_value AS dflt
FROM pragma_table_info('ProfileItem') WHERE name = 'Region';

SELECT '--- ProfileExItem 新增 4 列 ---' AS info;
SELECT name, type, [notnull] AS nn, dflt_value AS dflt
FROM pragma_table_info('ProfileExItem')
WHERE name IN ('consecutive_failures','start_count','total_runtime_ms','crash_count')
ORDER BY cid;

SELECT '--- FullConfigTemplateItem 新增 4 列 ---' AS info;
SELECT name, type, [notnull] AS nn, dflt_value AS dflt
FROM pragma_table_info('FullConfigTemplateItem')
WHERE name IN ('ParentId','BundleChain','TemplateType','BundleVersion')
ORDER BY cid;

SELECT '--- 专有表 ---' AS info;
SELECT name FROM sqlite_master
WHERE type = 'table' AND name IN ('ComponentBundle','proxy_runtime_history');

SELECT '--- 新增索引 ---' AS info;
SELECT name, tbl_name FROM sqlite_master
WHERE type = 'index' AND name IN ('idx_profile_dedup','idx_runtime_history_index');