-- sync_db_schema.sql
-- 维护v2rayn原生 guiNDB.db 表结构
--
-- 差异:
--   1. ProfileItem   缺少 Region 列
--   2. ProfileExItem 缺少 consecutive_failures 列
--
-- 建议通过 run_sync_db_schema.ps1 执行（自动处理列已存在等错误）:
--   run_sync_db_schema.ps1
--
-- 或直接执行（需确认列不存在，否则会报错）:
--   sqlite3 "XXX\guiNDB.db" < sync_db_schema.sql

-- ============================================================
-- 1. ProfileExItem: 增加 consecutive_failures 列
-- ============================================================
ALTER TABLE "ProfileExItem" ADD COLUMN consecutive_failures INTEGER DEFAULT 0;

-- ============================================================
-- 2. ProfileItem: 增加 Region 列
-- ============================================================
ALTER TABLE "ProfileItem" ADD COLUMN Region varchar;

-- ============================================================
-- 3. 验证
-- ============================================================
.mode column
.headers on
SELECT '--- ProfileExItem 列 ---' AS info;
SELECT cid, name, type, [notnull] AS nn, dflt_value AS dflt
FROM pragma_table_info('ProfileExItem');

SELECT '--- ProfileItem 列 ---' AS info;
SELECT cid, name, type, [notnull] AS nn, dflt_value AS dflt
FROM pragma_table_info('ProfileItem');
