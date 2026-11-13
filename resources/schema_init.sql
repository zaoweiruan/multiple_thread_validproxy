-- resources/schema_init.sql
--
-- Full, idempotent bootstrap schema for a NEW database.
-- Embedded into the executable by CMake (configure_file) and executed
-- inside a single BEGIN IMMEDIATE ... COMMIT by service::DatabaseBootstrap::create().
--
-- Rules for editing:
--   * Only CREATE ... IF NOT EXISTS / INSERT OR IGNORE. Never DROP/DELETE/ALTER.
--   * Never put PRAGMA here (journal_mode must be set outside the transaction).
--   * Must stay structurally identical to tests/fixtures/db_schema_baseline.sql.
--   * Incremental upgrades for EXISTING databases live in scripts/sync_db_schema.sql.

-- ==== v2rayN-compatible tables (8) ====

CREATE TABLE IF NOT EXISTS "SubItem" (
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

CREATE TABLE IF NOT EXISTS "ProfileItem" (
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

CREATE TABLE IF NOT EXISTS "ProfileExItem" (
"IndexId" varchar primary key not null ,
"Delay" integer ,
"Speed" float ,
"Sort" integer ,
"Message" varchar , consecutive_failures INTEGER DEFAULT 0, start_count INTEGER NOT NULL DEFAULT 0, total_runtime_ms INTEGER NOT NULL DEFAULT 0, crash_count INTEGER NOT NULL DEFAULT 0);

CREATE TABLE IF NOT EXISTS "ServerStatItem" (
"IndexId" varchar primary key not null ,
"TotalUp" integer ,
"TotalDown" integer ,
"TodayUp" integer ,
"TodayDown" integer ,
"DateNow" integer );

CREATE TABLE IF NOT EXISTS "RoutingItem" (
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

CREATE TABLE IF NOT EXISTS "DNSItem" (
"Id" varchar primary key not null ,
"Remarks" varchar ,
"Enabled" integer ,
"CoreType" integer ,
"UseSystemHosts" integer ,
"NormalDNS" varchar ,
"TunDNS" varchar ,
"DomainStrategy4Freedom" varchar ,
"DomainDNSAddress" varchar );

CREATE TABLE IF NOT EXISTS "FullConfigTemplateItem" (
"Id" varchar primary key not null ,
"Remarks" varchar ,
"Enabled" integer ,
"CoreType" integer ,
"Config" varchar ,
"TunConfig" varchar ,
"AddProxyOnly" integer ,
"ProxyDetour" varchar , ParentId varchar, BundleChain varchar, TemplateType varchar, BundleVersion varchar);

CREATE TABLE IF NOT EXISTS "ProfileGroupItem" (
"IndexId" varchar primary key not null ,
"ChildItems" varchar ,
"SubChildItems" varchar ,
"Filter" varchar ,
"MultipleLoad" integer );

-- ==== project-proprietary tables (3) ====

CREATE TABLE IF NOT EXISTS "ComponentBundle" (Id varchar PRIMARY KEY, Name varchar NOT NULL, Description varchar, CoreType INTEGER NOT NULL, Version INTEGER NOT NULL DEFAULT 1, Enabled INTEGER NOT NULL DEFAULT 1, Components varchar NOT NULL, CreatedAt TEXT NOT NULL, UpdatedAt TEXT NOT NULL);

CREATE TABLE IF NOT EXISTS proxy_runtime_history (id           INTEGER PRIMARY KEY AUTOINCREMENT, index_id     TEXT NOT NULL, started_at   TEXT NOT NULL, ended_at     TEXT NULL, exit_code    INTEGER, duration_ms  INTEGER, source       TEXT DEFAULT 'standalone', pid INTEGER, socks_port INTEGER);

CREATE TABLE IF NOT EXISTS SqlQuery (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL UNIQUE,
    sql_text    TEXT NOT NULL,
    scope       TEXT NOT NULL DEFAULT 'all',
    description TEXT,
    is_default  INTEGER NOT NULL DEFAULT 0,
    created_at  TEXT NOT NULL,
    updated_at  TEXT NOT NULL
);

-- ==== indexes (3) ====

CREATE INDEX IF NOT EXISTS idx_profile_dedup ON ProfileItem(LOWER(Address), Port, ConfigType, LOWER(Id), LOWER(Network));

CREATE INDEX IF NOT EXISTS idx_runtime_history_index ON proxy_runtime_history(index_id, started_at);

CREATE INDEX IF NOT EXISTS idx_sql_scope ON SqlQuery(scope);

-- ==== seed: ComponentBundle (16) ====

-- ComponentBundle: 16 rows
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('singbox_DNS', 'singbox_DNS', 'Minimal bundle (singbox_DNS)', 24, 1, 1, '{"id":"singbox_DNS","name":"singbox_DNS","description":"Minimal bundle (singbox_DNS)","coreType":24,"version":1,"enabled":true,"components":[{"type":3,"params":{"final":"remote_dns","independent_cache":true,"rules":[],"servers":[{"server":"223.5.5.5","type":"udp","tag":"local_dns"},{"server":"cloudflare-dns.com","domain_resolver":"local_dns","path":"/dns-query","type":"https","tag":"remote_dns","detour":"proxy"}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('singbox_INBOUND', 'singbox_INBOUND', 'Minimal bundle (singbox_INBOUND)', 24, 1, 1, '{"id":"singbox_INBOUND","name":"singbox_INBOUND","description":"Minimal bundle (singbox_INBOUND)","coreType":24,"version":1,"enabled":true,"components":[{"type":6,"params":{"listen":"127.0.0.1","listen_port":"${socksPort}","tag":"socks","type":"socks","users":[{"user":"","password":""}]},"id":"socks"}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('singbox_LOG', 'singbox_LOG', 'Minimal bundle (singbox_LOG)', 24, 1, 1, '{"id":"singbox_LOG","name":"singbox_LOG","description":"Minimal bundle (singbox_LOG)","coreType":24,"version":1,"enabled":true,"components":[{"type":1,"params":{"level":"warn","timestamp":true}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('singbox_ROUTING_pool', 'singbox_ROUTING_pool', 'Minimal bundle (singbox_ROUTING)', 24, 1, 1, '{"id":"singbox_ROUTING_pool","name":"singbox_ROUTING_pool","description":"Minimal bundle (singbox_ROUTING)","coreType":24,"version":1,"enabled":true,"components":[{"type":4,"params":{"default_domain_resolver":{"server":"local_dns"},"final":"proxy","rule_set":[],"rules":[{"action":"sniff"},{"protocol":["dns"],"action":"hijack-dns"},{"outbound":"proxy","clash_mode":"Global"},{"network":["udp"],"port":[443],"action":"reject"}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('singbox_ROUTING_standalone', 'singbox_ROUTING_standalone', 'Minimal bundle (singbox_ROUTING)', 24, 1, 1, '{"id":"singbox_ROUTING_standalone","name":"singbox_ROUTING_standalone","description":"Minimal bundle (singbox_ROUTING)","coreType":24,"version":1,"enabled":true,"components":[{"type":4,"params":{"default_domain_resolver":{"server":"direct_dns"},"final":"proxy","rule_set":[],"rules":[{"action":"sniff"},{"protocol":["dns"],"action":"hijack-dns"},{"outbound":"direct","clash_mode":"Direct"},{"outbound":"proxy","clash_mode":"Global"},{"network":["udp"],"port":[443],"action":"reject"},{"outbound":"proxy","rule_set":["geosite-google"]},{"outbound":"direct","ip_is_private":true},{"outbound":"direct","rule_set":["geosite-private"]},{"outbound":"direct","rule_set":["geoip-cn"]},{"outbound":"direct","rule_set":["geosite-cn"]}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('singbox_ROUTING_test', 'singbox_ROUTING_test', 'Minimal bundle (singbox_ROUTING)', 24, 1, 1, '{"id":"singbox_ROUTING_test","name":"singbox_ROUTING_test","description":"Minimal bundle (singbox_ROUTING)","coreType":24,"version":1,"enabled":true,"components":[{"type":4,"params":{"default_domain_resolver":{"server":"local_dns"},"final":"proxy","rule_set":[],"rules":[{"action":"sniff"},{"protocol":["dns"],"action":"hijack-dns"},{"outbound":"proxy","clash_mode":"Global"},{"network":["udp"],"port":[443],"action":"reject"}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_API', 'xray_API', 'Minimal bundle (xray_API)', 2, 1, 1, '{"id":"xray_API","name":"xray_API","description":"Minimal bundle (xray_API)","coreType":2,"version":1,"enabled":true,"components":[{"type":2,"params":{"listen":"127.0.0.1:${apiPort}","services":["HandlerService","LoggerService","StatsService","RoutingService"],"tag":"api"}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_DNS', 'xray_DNS', 'Minimal bundle (xray_DNS)', 2, 1, 1, '{"id":"xray_DNS","name":"xray_DNS","description":"Minimal bundle (xray_DNS)","coreType":2,"version":1,"enabled":true,"components":[{"type":3,"params":{"servers":["https://cloudflare-dns.com/dns-query"],"tag":"dns"}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_DNS_completion', 'xray_DNS_completion', 'completion DNS bundle (xray_DNS)', 2, 1, 1, '{"id":"xray_DNS_completion","name":"xray_DNS_completion","description":"completion DNS bundle (xray_DNS)","coreType":2,"version":1,"enabled":true,"components":[{"type":3,"params":{"hosts":{"dns.google":["8.8.8.8","8.8.4.4","2001:4860:4860::8888","2001:4860:4860::8844"],"dns.alidns.com":["223.5.5.5","223.6.6.6","2400:3200::1","2400:3200:baba::1"],"one.one.one.one":["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],"1dot1dot1dot1.cloudflare-dns.com":["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],"cloudflare-dns.com":["104.16.249.249","104.16.248.249","2606:4700::6810:f8f9","2606:4700::6810:f9f9"],"dns.cloudflare.com":["104.16.132.229","104.16.133.229","2606:4700::6810:84e5","2606:4700::6810:85e5"],"dot.pub":["1.12.12.12","120.53.53.53"],"doh.pub":["1.12.12.12","120.53.53.53"],"dns.quad9.net":["9.9.9.9","149.112.112.112","2620:fe::fe","2620:fe::9"],"dns.yandex.net":["77.88.8.8","77.88.8.1","2a02:6b8::feed:0ff","2a02:6b8:0:1::feed:0ff"],"dns.sb":["185.222.222.222","2a09::"],"dns.umbrella.com":["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],"dns.sse.cisco.com":["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],"engage.cloudflareclient.com":["162.159.192.1"]},"servers":[{"address":"https://dns.alidns.com/dns-query","domains":["domain:alidns.com","domain:doh.pub","domain:dot.pub","domain:360.cn","domain:onedns.net","www.wto.org"],"skipFallback":true},{"address":"https://cloudflare-dns.com/dns-query","domains":["geosite:google"],"skipFallback":true},{"address":"https://dns.alidns.com/dns-query","domains":["geosite:private","geosite:cn"],"skipFallback":true},{"address":"223.5.5.5","domains":["full:dns.alidns.com","full:cloudflare-dns.com"],"skipFallback":true},"https://cloudflare-dns.com/dns-query"]}}]}', '2026-10-10 02:10:49', '2026-10-10 02:10:49');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_INBOUND', 'xray_INBOUND', 'Minimal bundle (xray_INBOUND)', 2, 1, 1, '{"id":"xray_INBOUND","name":"xray_INBOUND","description":"Minimal bundle (xray_INBOUND)","coreType":2,"version":1,"enabled":true,"components":[{"type":6,"params":{"listen":"127.0.0.1","port":"${socksPort}","protocol":"mixed","settings":{"auth":"noauth","udp":true,"allowTransparent":false},"sniffing":{"enabled":true,"destOverride":["http","tls","quic"],"routeOnly":false},"tag":"socks"},"id":"socks"}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_LOG', 'xray_LOG', 'Minimal bundle (xray_LOG)', 2, 1, 1, '{"id":"xray_LOG","name":"xray_LOG","description":"Minimal bundle (xray_LOG)","coreType":2,"version":1,"enabled":true,"components":[{"type":1,"params":{"loglevel":"warning"}}]}', '2026-10-13 07:04:16', '2026-10-13 07:04:16');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_POLICY', 'xray_POLICY', 'Minimal bundle (xray_POLICY)', 2, 1, 1, '{"id":"xray_POLICY","name":"xray_POLICY","description":"Minimal bundle (xray_POLICY)","coreType":2,"version":1,"enabled":true,"components":[{"type":5,"params":{"levels":{"0":{"statsUserUplink":true,"statsUserDownlink":true}},"system":{"statsInboundUplink":true,"statsInboundDownlink":true,"statsOutboundUplink":true,"statsOutboundDownlink":true}}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_ROUTING_pool', 'xray_ROUTING_pool', 'Minimal bundle (xray_ROUTING)', 2, 1, 1, '{"id":"xray_ROUTING_pool","name":"xray_ROUTING_pool","description":"Minimal bundle (xray_ROUTING)","coreType":2,"version":1,"enabled":true,"components":[{"type":4,"params":{"balancers":[{"tag":"px-","strategy":{"type":"leastPing","fallbackProxy":"api"},"observation":{"probeUrl":"https://www.google.com/generate_204","probeInterval":"1m"}}],"domainStrategy":"AsIs","rules":[{"type":"field","inboundTag":["api"],"outboundTag":"api"},{"type":"field","port":"443","network":"udp","outboundTag":"block"},{"type":"field","outboundTag":"direct","ip":["geoip:private"]},{"type":"field","outboundTag":"direct","domain":["geosite:private"]},{"type":"field","outboundTag":"direct","ip":["geoip:cn"]},{"type":"field","outboundTag":"direct","domain":["geosite:cn"]}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_ROUTING_standalone', 'xray_ROUTING_standalone', 'Minimal bundle (xray_ROUTING)', 2, 1, 1, '{"id":"xray_ROUTING_standalone","name":"xray_ROUTING_standalone","description":"Minimal bundle (xray_ROUTING)","coreType":2,"version":1,"enabled":true,"components":[{"type":4,"params":{"domainStrategy":"AsIs","rules":[{"type":"field","port":"443","network":"udp","outboundTag":"block"},{"type":"field","outboundTag":"proxy","domain":["geosite:google"]},{"type":"field","outboundTag":"direct","ip":["geoip:private"]},{"type":"field","outboundTag":"direct","domain":["geosite:private"]},{"type":"field","outboundTag":"direct","ip":["geoip:cn"]},{"type":"field","outboundTag":"direct","domain":["geosite:cn"]}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_ROUTING_test', 'xray_ROUTING_test', 'Minimal bundle (xray_ROUTING)', 2, 1, 1, '{"id":"xray_ROUTING_test","name":"xray_ROUTING_test","description":"Minimal bundle (xray_ROUTING)","coreType":2,"version":1,"enabled":true,"components":[{"type":4,"params":{"domainStrategy":"AsIs","rules":[{"type":"field","inboundTag":["api"],"outboundTag":"api"}]}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');
INSERT OR IGNORE INTO "ComponentBundle" ("Id", "Name", "Description", "CoreType", "Version", "Enabled", "Components", "CreatedAt", "UpdatedAt") VALUES ('xray_STATS', 'xray_STATS', 'Minimal bundle (xray_STATS)', 2, 1, 1, '{"id":"xray_STATS","name":"xray_STATS","description":"Minimal bundle (xray_STATS)","coreType":2,"version":1,"enabled":true,"components":[{"type":8,"params":{}}]}', '2026-10-10 00:40:31', '2026-10-10 00:40:31');

-- FullConfigTemplateItem: 8 rows
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('pool_singbox_001', 'Default SingBox pool config template', 1, 24, '{"log":{"level":"warn","timestamp":true},"dns":{"final":"remote_dns","independent_cache":true,"rules":[],"servers":[{"server":"223.5.5.5","type":"udp","tag":"local_dns"},{"server":"cloudflare-dns.com","domain_resolver":"local_dns","path":"/dns-query","type":"https","tag":"remote_dns","detour":"proxy"}]},"inbounds":[{"listen":"127.0.0.1","listen_port":"${socksPort}","tag":"socks","type":"socks","users":[{"user":"","password":""}]}],"route":{"rules":[{"action":"sniff"},{"protocol":["dns"],"action":"hijack-dns"},{"outbound":"proxy","clash_mode":"Global"},{"network":["udp"],"port":[443],"action":"reject"}],"default_domain_resolver":{"server":"local_dns"},"final":"proxy","rule_set":[]}}', '', 0, '', '', '{"bundleIds":["singbox_LOG","singbox_DNS","singbox_ROUTING_pool","singbox_INBOUND"],"params":{}}', 'bundleChain', '[{"bundleId":"singbox_LOG","version":1},{"bundleId":"singbox_DNS","version":1},{"bundleId":"singbox_ROUTING_pool","version":1},{"bundleId":"singbox_INBOUND","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('pool_xray_001', 'Default Xray pool config template', 1, 2, '{"log":{"loglevel":"warning"},"api":{"listen":"127.0.0.1:${apiPort}","services":["HandlerService","LoggerService","StatsService","RoutingService"],"tag":"api"},"routing":{"rules":[{"type":"field","inboundTag":["api"],"outboundTag":"api"},{"type":"field","port":"443","network":"udp","outboundTag":"block"},{"type":"field","outboundTag":"direct","ip":["geoip:private"]},{"type":"field","outboundTag":"direct","domain":["geosite:private"]},{"type":"field","outboundTag":"direct","ip":["geoip:cn"]},{"type":"field","outboundTag":"direct","domain":["geosite:cn"]}],"balancers":[{"tag":"px-","strategy":{"type":"leastPing","fallbackProxy":"api"},"observation":{"probeUrl":"https://www.google.com/generate_204","probeInterval":"1m"}}],"domainStrategy":"AsIs"},"policy":{"levels":{"0":{"statsUserUplink":true,"statsUserDownlink":true}},"system":{"statsInboundUplink":true,"statsInboundDownlink":true,"statsOutboundUplink":true,"statsOutboundDownlink":true}},"inbounds":[{"listen":"127.0.0.1","port":"${socksPort}","protocol":"mixed","settings":{"auth":"noauth","udp":true,"allowTransparent":false},"sniffing":{"enabled":true,"destOverride":["http","tls","quic"],"routeOnly":false},"tag":"socks"}],"stats":{},"dns":{"hosts":{"dns.google":["8.8.8.8","8.8.4.4","2001:4860:4860::8888","2001:4860:4860::8844"],"dns.alidns.com":["223.5.5.5","223.6.6.6","2400:3200::1","2400:3200:baba::1"],"one.one.one.one":["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],"1dot1dot1dot1.cloudflare-dns.com":["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],"cloudflare-dns.com":["104.16.249.249","104.16.248.249","2606:4700::6810:f8f9","2606:4700::6810:f9f9"],"dns.cloudflare.com":["104.16.132.229","104.16.133.229","2606:4700::6810:84e5","2606:4700::6810:85e5"],"dot.pub":["1.12.12.12","120.53.53.53"],"doh.pub":["1.12.12.12","120.53.53.53"],"dns.quad9.net":["9.9.9.9","149.112.112.112","2620:fe::fe","2620:fe::9"],"dns.yandex.net":["77.88.8.8","77.88.8.1","2a02:6b8::feed:0ff","2a02:6b8:0:1::feed:0ff"],"dns.sb":["185.222.222.222","2a09::"],"dns.umbrella.com":["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],"dns.sse.cisco.com":["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],"engage.cloudflareclient.com":["162.159.192.1"]},"servers":[{"address":"https://dns.alidns.com/dns-query","domains":["domain:alidns.com","domain:doh.pub","domain:dot.pub","domain:360.cn","domain:onedns.net","www.wto.org"],"skipFallback":true},{"address":"https://cloudflare-dns.com/dns-query","domains":["geosite:google"],"skipFallback":true},{"address":"https://dns.alidns.com/dns-query","domains":["geosite:private","geosite:cn"],"skipFallback":true},{"address":"223.5.5.5","domains":["full:dns.alidns.com","full:cloudflare-dns.com"],"skipFallback":true},"https://cloudflare-dns.com/dns-query"]}}', '', 0, '', '', '{"bundleIds":["xray_LOG","xray_API","xray_ROUTING_pool","xray_POLICY","xray_INBOUND","xray_STATS","xray_DNS_completion"],"params":{}}', 'bundleChain', '[{"bundleId":"xray_LOG","version":1},{"bundleId":"xray_API","version":1},{"bundleId":"xray_ROUTING_pool","version":1},{"bundleId":"xray_POLICY","version":1},{"bundleId":"xray_INBOUND","version":1},{"bundleId":"xray_STATS","version":1},{"bundleId":"xray_DNS_completion","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('singbox_base_001', 'SingBox base template (shared log + dns)', 1, 24, '{"log":{"level":"warn","timestamp":true},"dns":{"final":"remote_dns","independent_cache":true,"rules":[],"servers":[{"server":"223.5.5.5","type":"udp","tag":"local_dns"},{"server":"cloudflare-dns.com","domain_resolver":"local_dns","path":"/dns-query","type":"https","tag":"remote_dns","detour":"proxy"}]}}', '', 0, '', '', '{"bundleIds":["singbox_LOG","singbox_DNS"]}', 'bundleChain', '[{"bundleId":"singbox_LOG","version":1},{"bundleId":"singbox_DNS","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('singbox_default_001', 'Default SingBox config template', 1, 24, '{"log":{"level":"warn","timestamp":true},"inbounds":[{"listen":"127.0.0.1","listen_port":"${socksPort}","tag":"socks","type":"socks","users":[{"user":"","password":""}]}],"dns":{"final":"remote_dns","independent_cache":true,"rules":[],"servers":[{"server":"223.5.5.5","type":"udp","tag":"local_dns"},{"server":"cloudflare-dns.com","domain_resolver":"local_dns","path":"/dns-query","type":"https","tag":"remote_dns","detour":"proxy"}]},"route":{"rules":[{"action":"sniff"},{"protocol":["dns"],"action":"hijack-dns"},{"outbound":"direct","clash_mode":"Direct"},{"outbound":"proxy","clash_mode":"Global"},{"network":["udp"],"port":[443],"action":"reject"},{"outbound":"proxy","rule_set":["geosite-google"]},{"outbound":"direct","ip_is_private":true},{"outbound":"direct","rule_set":["geosite-private"]},{"outbound":"direct","rule_set":["geoip-cn"]},{"outbound":"direct","rule_set":["geosite-cn"]}],"default_domain_resolver":{"server":"direct_dns"},"final":"proxy","rule_set":[]}}', '', 0, '', '', '{"bundleIds":["singbox_ROUTING_standalone","singbox_INBOUND","singbox_DNS","singbox_LOG"],"params":{}}', 'bundleChain', '[{"bundleId":"singbox_ROUTING_standalone","version":1},{"bundleId":"singbox_INBOUND","version":1},{"bundleId":"singbox_DNS","version":1},{"bundleId":"singbox_LOG","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('singbox_test_001', 'Default SingBox test config template (for batch testing)', 1, 24, '{"log":{"level":"warn","timestamp":true},"inbounds":[{"listen":"127.0.0.1","listen_port":"${socksPort}","tag":"socks","type":"socks","users":[{"user":"","password":""}]}],"dns":{"final":"remote_dns","independent_cache":true,"rules":[],"servers":[{"server":"223.5.5.5","type":"udp","tag":"local_dns"},{"server":"cloudflare-dns.com","domain_resolver":"local_dns","path":"/dns-query","type":"https","tag":"remote_dns","detour":"proxy"}]},"route":{"rules":[{"action":"sniff"},{"protocol":["dns"],"action":"hijack-dns"},{"outbound":"proxy","clash_mode":"Global"},{"network":["udp"],"port":[443],"action":"reject"}],"default_domain_resolver":{"server":"local_dns"},"final":"proxy","rule_set":[]}}', '', 0, '', '', '{"bundleIds":["singbox_ROUTING_test","singbox_INBOUND","singbox_DNS","singbox_LOG"],"params":{}}', 'bundleChain', '[{"bundleId":"singbox_ROUTING_test","version":1},{"bundleId":"singbox_INBOUND","version":1},{"bundleId":"singbox_DNS","version":1},{"bundleId":"singbox_LOG","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('xray_base_001', 'Xray base template (shared log + dns)', 1, 2, '{"log":{"loglevel":"warning"},"dns":{"servers":["https://cloudflare-dns.com/dns-query"],"tag":"dns"}}', '', 0, '', '', '{"bundleIds":["xray_LOG","xray_DNS"]}', 'bundleChain', '[{"bundleId":"xray_LOG","version":1},{"bundleId":"xray_DNS","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('xray_default_001', 'Default Xray config template', 1, 2, '{"routing":{"rules":[{"type":"field","port":"443","network":"udp","outboundTag":"block"},{"type":"field","outboundTag":"proxy","domain":["geosite:google"]},{"type":"field","outboundTag":"direct","ip":["geoip:private"]},{"type":"field","outboundTag":"direct","domain":["geosite:private"]},{"type":"field","outboundTag":"direct","ip":["geoip:cn"]},{"type":"field","outboundTag":"direct","domain":["geosite:cn"]}],"domainStrategy":"AsIs"},"inbounds":[{"listen":"127.0.0.1","port":"${socksPort}","protocol":"mixed","settings":{"auth":"noauth","udp":true,"allowTransparent":false},"sniffing":{"enabled":true,"destOverride":["http","tls","quic"],"routeOnly":false},"tag":"socks"}],"log":{"loglevel":"warning"},"dns":{"hosts":{"dns.google":["8.8.8.8","8.8.4.4","2001:4860:4860::8888","2001:4860:4860::8844"],"dns.alidns.com":["223.5.5.5","223.6.6.6","2400:3200::1","2400:3200:baba::1"],"one.one.one.one":["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],"1dot1dot1dot1.cloudflare-dns.com":["1.1.1.1","1.0.0.1","2606:4700:4700::1111","2606:4700:4700::1001"],"cloudflare-dns.com":["104.16.249.249","104.16.248.249","2606:4700::6810:f8f9","2606:4700::6810:f9f9"],"dns.cloudflare.com":["104.16.132.229","104.16.133.229","2606:4700::6810:84e5","2606:4700::6810:85e5"],"dot.pub":["1.12.12.12","120.53.53.53"],"doh.pub":["1.12.12.12","120.53.53.53"],"dns.quad9.net":["9.9.9.9","149.112.112.112","2620:fe::fe","2620:fe::9"],"dns.yandex.net":["77.88.8.8","77.88.8.1","2a02:6b8::feed:0ff","2a02:6b8:0:1::feed:0ff"],"dns.sb":["185.222.222.222","2a09::"],"dns.umbrella.com":["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],"dns.sse.cisco.com":["208.67.220.220","208.67.222.222","2620:119:35::35","2620:119:53::53"],"engage.cloudflareclient.com":["162.159.192.1"]},"servers":[{"address":"https://dns.alidns.com/dns-query","domains":["domain:alidns.com","domain:doh.pub","domain:dot.pub","domain:360.cn","domain:onedns.net","www.wto.org"],"skipFallback":true},{"address":"https://cloudflare-dns.com/dns-query","domains":["geosite:google"],"skipFallback":true},{"address":"https://dns.alidns.com/dns-query","domains":["geosite:private","geosite:cn"],"skipFallback":true},{"address":"223.5.5.5","domains":["full:dns.alidns.com","full:cloudflare-dns.com"],"skipFallback":true},"https://cloudflare-dns.com/dns-query"]}}', '', 0, '', '', '{"bundleIds":["xray_ROUTING_standalone","xray_INBOUND","xray_LOG","xray_DNS_completion"],"params":{}}', 'bundleChain', '[{"bundleId":"xray_ROUTING_standalone","version":1},{"bundleId":"xray_INBOUND","version":1},{"bundleId":"xray_LOG","version":1},{"bundleId":"xray_DNS_completion","version":1}]');
INSERT OR IGNORE INTO "FullConfigTemplateItem" ("Id", "Remarks", "Enabled", "CoreType", "Config", "TunConfig", "AddProxyOnly", "ProxyDetour", "ParentId", "BundleChain", "TemplateType", "BundleVersion") VALUES ('xray_test_001', 'Default Xray test config template (for batch testing)', 1, 2, '{"api":{"listen":"127.0.0.1:${apiPort}","services":["HandlerService","LoggerService","StatsService","RoutingService"],"tag":"api"},"routing":{"rules":[{"type":"field","inboundTag":["api"],"outboundTag":"api"}],"domainStrategy":"AsIs"},"policy":{"levels":{"0":{"statsUserUplink":true,"statsUserDownlink":true}},"system":{"statsInboundUplink":true,"statsInboundDownlink":true,"statsOutboundUplink":true,"statsOutboundDownlink":true}},"inbounds":[{"listen":"127.0.0.1","port":"${socksPort}","protocol":"mixed","settings":{"auth":"noauth","udp":true,"allowTransparent":false},"sniffing":{"enabled":true,"destOverride":["http","tls","quic"],"routeOnly":false},"tag":"socks"}],"stats":{},"log":{"loglevel":"warning"},"dns":{"servers":["https://cloudflare-dns.com/dns-query"],"tag":"dns"}}', '', 0, '', '', '{"bundleIds":["xray_API","xray_ROUTING_test","xray_POLICY","xray_INBOUND","xray_STATS","xray_LOG","xray_DNS"],"params":{}}', 'bundleChain', '[{"bundleId":"xray_API","version":1},{"bundleId":"xray_ROUTING_test","version":1},{"bundleId":"xray_POLICY","version":1},{"bundleId":"xray_INBOUND","version":1},{"bundleId":"xray_STATS","version":1},{"bundleId":"xray_LOG","version":1},{"bundleId":"xray_DNS","version":1}]');
