-- tests/fixtures/db_schema_baseline.sql
--
-- SCHEMA-ONLY baseline for DatabaseBootstrapTest.
-- Generated from a fully migrated bin/guiNDB.db via sqlite_master,
-- so that a clean checkout (where bin/guiNDB.db and test/guiNDB_empty.db
-- are both git-ignored) can still assert the created schema.
-- DO NOT hand-edit; regenerate when the canonical schema changes.
--
-- Format kept parseable by the test: one "TABLE <name>" / "INDEX <name>"
-- marker line per object, then the DDL, then "<name>.<column> <type> <notnull> <pk>".

-- >>>>> TABLE SubItem
CREATE TABLE "SubItem" (
"Id" varchar primary key not null ,
"Remarks" varchar ,
"Url" varchar ,
"MoreUrl" varchar ,
"Enabled" integer ,
"UserAgent" varchar ,
"Sort" integer ,
"Filter" varchar ,
"AutoUpdateInterval" integer ,
"UpdateTime" integer ,
"ConvertTarget" varchar ,
"PrevProfile" varchar ,
"NextProfile" varchar ,
"PreSocksPort" integer ,
"Memo" varchar );
-- COL Id varchar 1 1
-- COL Remarks varchar 0 0
-- COL Url varchar 0 0
-- COL MoreUrl varchar 0 0
-- COL Enabled INTEGER 0 0
-- COL UserAgent varchar 0 0
-- COL Sort INTEGER 0 0
-- COL Filter varchar 0 0
-- COL AutoUpdateInterval INTEGER 0 0
-- COL UpdateTime INTEGER 0 0
-- COL ConvertTarget varchar 0 0
-- COL PrevProfile varchar 0 0
-- COL NextProfile varchar 0 0
-- COL PreSocksPort INTEGER 0 0
-- COL Memo varchar 0 0

-- >>>>> TABLE ProfileItem
CREATE TABLE "ProfileItem" (
"IndexId" varchar primary key not null ,
"ConfigType" integer ,
"ConfigVersion" integer ,
"Address" varchar ,
"Port" integer ,
"Ports" varchar ,
"Id" varchar ,
"AlterId" integer ,
"Security" varchar ,
"Network" varchar ,
"Remarks" varchar ,
"HeaderType" varchar ,
"RequestHost" varchar ,
"Path" varchar ,
"StreamSecurity" varchar ,
"AllowInsecure" varchar ,
"Subid" varchar ,
"IsSub" integer ,
"Flow" varchar ,
"Sni" varchar ,
"Alpn" varchar ,
"CoreType" integer ,
"PreSocksPort" integer ,
"Fingerprint" varchar ,
"DisplayLog" integer ,
"PublicKey" varchar ,
"ShortId" varchar ,
"SpiderX" varchar ,
"Mldsa65Verify" varchar ,
"Extra" varchar ,
"MuxEnabled" integer ,
"Cert" varchar ,
"CertSha" varchar ,
"EchConfigList" varchar ,
"EchForceQuery" varchar , Region VARCHAR);
-- COL IndexId varchar 1 1
-- COL ConfigType INTEGER 0 0
-- COL ConfigVersion INTEGER 0 0
-- COL Address varchar 0 0
-- COL Port INTEGER 0 0
-- COL Ports varchar 0 0
-- COL Id varchar 0 0
-- COL AlterId INTEGER 0 0
-- COL Security varchar 0 0
-- COL Network varchar 0 0
-- COL Remarks varchar 0 0
-- COL HeaderType varchar 0 0
-- COL RequestHost varchar 0 0
-- COL Path varchar 0 0
-- COL StreamSecurity varchar 0 0
-- COL AllowInsecure varchar 0 0
-- COL Subid varchar 0 0
-- COL IsSub INTEGER 0 0
-- COL Flow varchar 0 0
-- COL Sni varchar 0 0
-- COL Alpn varchar 0 0
-- COL CoreType INTEGER 0 0
-- COL PreSocksPort INTEGER 0 0
-- COL Fingerprint varchar 0 0
-- COL DisplayLog INTEGER 0 0
-- COL PublicKey varchar 0 0
-- COL ShortId varchar 0 0
-- COL SpiderX varchar 0 0
-- COL Mldsa65Verify varchar 0 0
-- COL Extra varchar 0 0
-- COL MuxEnabled INTEGER 0 0
-- COL Cert varchar 0 0
-- COL CertSha varchar 0 0
-- COL EchConfigList varchar 0 0
-- COL EchForceQuery varchar 0 0
-- COL Region VARCHAR 0 0

-- >>>>> TABLE ProfileExItem
CREATE TABLE "ProfileExItem" (
"IndexId" varchar primary key not null ,
"Delay" integer ,
"Speed" float ,
"Sort" integer ,
"Message" varchar , consecutive_failures INTEGER DEFAULT 0, start_count INTEGER NOT NULL DEFAULT 0, total_runtime_ms INTEGER NOT NULL DEFAULT 0, crash_count INTEGER NOT NULL DEFAULT 0);
-- COL IndexId varchar 1 1
-- COL Delay INTEGER 0 0
-- COL Speed float 0 0
-- COL Sort INTEGER 0 0
-- COL Message varchar 0 0
-- COL consecutive_failures INTEGER 0 0
-- COL start_count INTEGER 1 0
-- COL total_runtime_ms INTEGER 1 0
-- COL crash_count INTEGER 1 0

-- >>>>> TABLE ServerStatItem
CREATE TABLE "ServerStatItem" (
"IndexId" varchar primary key not null ,
"TotalUp" integer ,
"TotalDown" integer ,
"TodayUp" integer ,
"TodayDown" integer ,
"DateNow" integer );
-- COL IndexId varchar 1 1
-- COL TotalUp INTEGER 0 0
-- COL TotalDown INTEGER 0 0
-- COL TodayUp INTEGER 0 0
-- COL TodayDown INTEGER 0 0
-- COL DateNow INTEGER 0 0

-- >>>>> TABLE RoutingItem
CREATE TABLE "RoutingItem" (
"Id" varchar primary key not null ,
"Remarks" varchar ,
"Url" varchar ,
"RuleSet" varchar ,
"RuleNum" integer ,
"Enabled" integer ,
"Locked" integer ,
"CustomIcon" varchar ,
"CustomRulesetPath4Singbox" varchar ,
"DomainStrategy" varchar ,
"DomainStrategy4Singbox" varchar ,
"Sort" integer ,
"IsActive" integer );
-- COL Id varchar 1 1
-- COL Remarks varchar 0 0
-- COL Url varchar 0 0
-- COL RuleSet varchar 0 0
-- COL RuleNum INTEGER 0 0
-- COL Enabled INTEGER 0 0
-- COL Locked INTEGER 0 0
-- COL CustomIcon varchar 0 0
-- COL CustomRulesetPath4Singbox varchar 0 0
-- COL DomainStrategy varchar 0 0
-- COL DomainStrategy4Singbox varchar 0 0
-- COL Sort INTEGER 0 0
-- COL IsActive INTEGER 0 0

-- >>>>> TABLE DNSItem
CREATE TABLE "DNSItem" (
"Id" varchar primary key not null ,
"Remarks" varchar ,
"Enabled" integer ,
"CoreType" integer ,
"UseSystemHosts" integer ,
"NormalDNS" varchar ,
"TunDNS" varchar ,
"DomainStrategy4Freedom" varchar ,
"DomainDNSAddress" varchar );
-- COL Id varchar 1 1
-- COL Remarks varchar 0 0
-- COL Enabled INTEGER 0 0
-- COL CoreType INTEGER 0 0
-- COL UseSystemHosts INTEGER 0 0
-- COL NormalDNS varchar 0 0
-- COL TunDNS varchar 0 0
-- COL DomainStrategy4Freedom varchar 0 0
-- COL DomainDNSAddress varchar 0 0

-- >>>>> TABLE FullConfigTemplateItem
CREATE TABLE "FullConfigTemplateItem" (
"Id" varchar primary key not null ,
"Remarks" varchar ,
"Enabled" integer ,
"CoreType" integer ,
"Config" varchar ,
"TunConfig" varchar ,
"AddProxyOnly" integer ,
"ProxyDetour" varchar , ParentId varchar, BundleChain varchar, TemplateType varchar, BundleVersion varchar);
-- COL Id varchar 1 1
-- COL Remarks varchar 0 0
-- COL Enabled INTEGER 0 0
-- COL CoreType INTEGER 0 0
-- COL Config varchar 0 0
-- COL TunConfig varchar 0 0
-- COL AddProxyOnly INTEGER 0 0
-- COL ProxyDetour varchar 0 0
-- COL ParentId varchar 0 0
-- COL BundleChain varchar 0 0
-- COL TemplateType varchar 0 0
-- COL BundleVersion varchar 0 0

-- >>>>> TABLE ProfileGroupItem
CREATE TABLE "ProfileGroupItem" (
"IndexId" varchar primary key not null ,
"ChildItems" varchar ,
"SubChildItems" varchar ,
"Filter" varchar ,
"MultipleLoad" integer );
-- COL IndexId varchar 1 1
-- COL ChildItems varchar 0 0
-- COL SubChildItems varchar 0 0
-- COL Filter varchar 0 0
-- COL MultipleLoad INTEGER 0 0

-- >>>>> TABLE ComponentBundle
CREATE TABLE "ComponentBundle" (Id varchar PRIMARY KEY, Name varchar NOT NULL, Description varchar, CoreType INTEGER NOT NULL, Version INTEGER NOT NULL DEFAULT 1, Enabled INTEGER NOT NULL DEFAULT 1, Components varchar NOT NULL, CreatedAt TEXT NOT NULL, UpdatedAt TEXT NOT NULL);
-- COL Id varchar 0 1
-- COL Name varchar 1 0
-- COL Description varchar 0 0
-- COL CoreType INTEGER 1 0
-- COL Version INTEGER 1 0
-- COL Enabled INTEGER 1 0
-- COL Components varchar 1 0
-- COL CreatedAt TEXT 1 0
-- COL UpdatedAt TEXT 1 0

-- >>>>> TABLE proxy_runtime_history
CREATE TABLE proxy_runtime_history (id           INTEGER PRIMARY KEY AUTOINCREMENT, index_id     TEXT NOT NULL, started_at   TEXT NOT NULL, ended_at     TEXT NULL, exit_code    INTEGER, duration_ms  INTEGER, source       TEXT DEFAULT 'standalone', pid INTEGER, socks_port INTEGER);
-- COL id INTEGER 0 1
-- COL index_id TEXT 1 0
-- COL started_at TEXT 1 0
-- COL ended_at TEXT 0 0
-- COL exit_code INTEGER 0 0
-- COL duration_ms INTEGER 0 0
-- COL source TEXT 0 0
-- COL pid INTEGER 0 0
-- COL socks_port INTEGER 0 0

-- >>>>> TABLE SqlQuery
CREATE TABLE SqlQuery (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    sql_text    TEXT NOT NULL,
    scope       TEXT NOT NULL DEFAULT 'all',
    description TEXT,
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);
-- COL id INTEGER 0 1
-- COL name TEXT 1 0
-- COL sql_text TEXT 1 0
-- COL scope TEXT 1 0
-- COL description TEXT 0 0
-- COL is_default INTEGER 1 0
-- COL created_at TEXT 1 0
-- COL updated_at TEXT 1 0

-- >>>>> INDEX idx_profile_dedup
CREATE INDEX idx_profile_dedup ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network));

-- >>>>> INDEX idx_runtime_history_index
CREATE INDEX idx_runtime_history_index ON proxy_runtime_history(index_id, started_at);

-- >>>>> INDEX idx_sql_scope
CREATE INDEX idx_sql_scope ON SqlQuery(scope);
