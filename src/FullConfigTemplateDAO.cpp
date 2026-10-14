#include "FullConfigTemplateDAO.h"
#include "Logger.h"
#include <sqlite3.h>
#include <map>
#include <boost/json.hpp>

namespace db {

FullConfigTemplateDAO::FullConfigTemplateDAO(sqlite3* db) : db_(db) {
    // Table creation will be handled by createTableIfNotExists()
}

bool FullConfigTemplateDAO::tableExists() const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='FullConfigTemplateItem'";
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("[FullConfigTemplateDAO] Failed to prepare table exists check: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool FullConfigTemplateDAO::createTableIfNotExists() const {
    if (tableExists()) {
        // Table exists - check if ParentId column is missing and add it
        sqlite3_stmt* colStmt = nullptr;
        const char* colSql = "PRAGMA table_info(FullConfigTemplateItem)";
        int rc = sqlite3_prepare_v2(db_, colSql, -1, &colStmt, nullptr);
        if (rc == SQLITE_OK) {
            bool hasParentId = false;
            bool hasBundleChain = false;
            bool hasTemplateType = false;
            bool hasBundleVersion = false;
            while (sqlite3_step(colStmt) == SQLITE_ROW) {
                const char* name = reinterpret_cast<const char*>(sqlite3_column_text(colStmt, 1));
                if (name) {
                    std::string colName(name);
                    if (colName == "ParentId") {
                        hasParentId = true;
                    } else if (colName == "BundleChain") {
                        hasBundleChain = true;
                    } else if (colName == "TemplateType") {
                        hasTemplateType = true;
                    } else if (colName == "BundleVersion") {
                        hasBundleVersion = true;
                    }
                }
            }
            sqlite3_finalize(colStmt);
            
            if (!hasParentId) {
                const char* alterSql = "ALTER TABLE FullConfigTemplateItem ADD COLUMN ParentId varchar";
                char* errMsg = nullptr;
                rc = sqlite3_exec(db_, alterSql, nullptr, nullptr, &errMsg);
                if (rc != SQLITE_OK) {
                    std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
                    if (errMsg) sqlite3_free(errMsg);
                    Logger::write("[FullConfigTemplateDAO] Failed to add ParentId column: " + msg, LogLevel::WARN);
                } else {
                    Logger::write("[FullConfigTemplateDAO] Added ParentId column to existing table", LogLevel::INFO);
                }
            }
            if (!hasBundleChain) {
                const char* alterSql = "ALTER TABLE FullConfigTemplateItem ADD COLUMN BundleChain varchar";
                char* errMsg = nullptr;
                rc = sqlite3_exec(db_, alterSql, nullptr, nullptr, &errMsg);
                if (rc != SQLITE_OK) {
                    std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
                    if (errMsg) sqlite3_free(errMsg);
                    Logger::write("[FullConfigTemplateDAO] Failed to add BundleChain column: " + msg, LogLevel::WARN);
                } else {
                    Logger::write("[FullConfigTemplateDAO] Added BundleChain column to existing table", LogLevel::INFO);
                }
            }
            if (!hasTemplateType) {
                const char* alterSql = "ALTER TABLE FullConfigTemplateItem ADD COLUMN TemplateType varchar";
                char* errMsg = nullptr;
                rc = sqlite3_exec(db_, alterSql, nullptr, nullptr, &errMsg);
                if (rc != SQLITE_OK) {
                    std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
                    if (errMsg) sqlite3_free(errMsg);
                    Logger::write("[FullConfigTemplateDAO] Failed to add TemplateType column: " + msg, LogLevel::WARN);
                } else {
                    Logger::write("[FullConfigTemplateDAO] Added TemplateType column to existing table", LogLevel::INFO);
                }
            }
            if (!hasBundleVersion) {
                const char* alterSql = "ALTER TABLE FullConfigTemplateItem ADD COLUMN BundleVersion varchar";
                char* errMsg = nullptr;
                rc = sqlite3_exec(db_, alterSql, nullptr, nullptr, &errMsg);
                if (rc != SQLITE_OK) {
                    std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
                    if (errMsg) sqlite3_free(errMsg);
                    Logger::write("[FullConfigTemplateDAO] Failed to add BundleVersion column: " + msg, LogLevel::WARN);
                } else {
                    Logger::write("[FullConfigTemplateDAO] Added BundleVersion column to existing table", LogLevel::INFO);
                }
            }
        } else {
            sqlite3_finalize(colStmt);
        }
        return true;
    }
    
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS FullConfigTemplateItem ("
        "Id varchar PRIMARY KEY,"
        "ParentId varchar,"
        "Remarks varchar,"
        "Enabled INTEGER DEFAULT 0,"
        "CoreType INTEGER DEFAULT 0,"
        "Config varchar,"
        "TunConfig varchar,"
        "AddProxyOnly INTEGER DEFAULT 0,"
        "ProxyDetour varchar,"
        "BundleChain varchar,"
        "TemplateType varchar,"
        "BundleVersion varchar"
        ");";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
        if (errMsg) sqlite3_free(errMsg);
        Logger::write("[FullConfigTemplateDAO] Failed to create table: " + msg, LogLevel::ERR);
        return false;
    }
    
    Logger::write("[FullConfigTemplateDAO] Created FullConfigTemplateItem table", LogLevel::INFO);
    return true;
}

int FullConfigTemplateDAO::seedDefaultTemplates() const {
    // Check if table already has data
    std::vector<FullConfigTemplateItem> existing = getAll();
    if (!existing.empty()) {
        Logger::write("[FullConfigTemplateDAO] Table already has templates, skipping seed", LogLevel::INFO);
        return 0;
    }
    
    int inserted = 0;
    
    // === Base templates (Level 0) - shared by all children ===
    
    // Xray base template (CoreType=2) - log + dns only
    {
        FullConfigTemplateItem item;
        item.id = "xray_base_001";
        item.parentId = "";
        item.remarks = "Xray base template (shared log + dns)";
        item.enabled = 1;
        item.coreType = 2;
        item.config = R"JSON({
  "log": {"loglevel": "warning"},
  "dns": {
    "tag": "dns",
    "servers": ["https://cloudflare-dns.com/dns-query"]
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted Xray base template", LogLevel::INFO);
        }
    }
    
    // SingBox base template (CoreType=24) - log + dns only
    {
        FullConfigTemplateItem item;
        item.id = "singbox_base_001";
        item.parentId = "";
        item.remarks = "SingBox base template (shared log + dns)";
        item.enabled = 1;
        item.coreType = 24;
        item.config = R"JSON({
  "log": {"level": "warn", "timestamp": true},
  "dns": {
    "servers": [
      {"server": "223.5.5.5", "type": "udp", "tag": "local_dns"},
      {"server": "cloudflare-dns.com", "domain_resolver": "local_dns", "path": "/dns-query", "type": "https", "tag": "remote_dns", "detour": "proxy"}
    ],
    "rules": [],
    "final": "remote_dns",
    "independent_cache": true
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted SingBox base template", LogLevel::INFO);
        }
    }
    
    // === Child templates (Level 1-2) - inherit from base ===
    
    // Default Xray config template (CoreType=2) - inherits xray_base_001
    // ConfigBuilder generates outbounds (with dynamic proxy)
    {
        FullConfigTemplateItem item;
        item.id = "xray_default_001";
        item.parentId = "xray_base_001";
        item.remarks = "Default Xray config template";
        item.enabled = 1;
        item.coreType = 2;
        item.config = R"JSON({
  "inbounds": [
    {
      "tag": "socks",
      "listen": "127.0.0.1",
      "port": "${socksPort}",
      "protocol": "mixed",
      "settings": {"auth": "noauth", "udp": true, "allowTransparent": false},
      "sniffing": {"enabled": true, "destOverride": ["http", "tls", "quic"], "routeOnly": false}
    }
  ],
  "routing": {
    "domainStrategy": "AsIs",
    "rules": [
      {"type": "field", "port": "443", "network": "udp", "outboundTag": "block"},
      {"type": "field", "outboundTag": "proxy", "domain": ["geosite:google"]},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:private"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:private"]},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:cn"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:cn"]}
    ]
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted default Xray template", LogLevel::INFO);
        }
    }
    
    // Default SingBox config template (CoreType=24) - inherits singbox_base_001
    // ConfigBuilder generates outbounds (with dynamic proxy)
    {
        FullConfigTemplateItem item;
        item.id = "singbox_default_001";
        item.parentId = "singbox_base_001";
        item.remarks = "Default SingBox config template";
        item.enabled = 1;
        item.coreType = 24;
        item.config = R"JSON({
  "inbounds": [
    {
      "tag": "socks",
      "type": "socks",
      "listen": "127.0.0.1",
      "listen_port": "${socksPort}",
      "users": [
        {"user": "", "password": ""}
      ]
    }
  ],
  "route": {
    "default_domain_resolver": {"server": "direct_dns"},
    "rules": [
      {"action": "sniff"},
      {"protocol": ["dns"], "action": "hijack-dns"},
      {"outbound": "direct", "clash_mode": "Direct"},
      {"outbound": "proxy", "clash_mode": "Global"},
      {"network": ["udp"], "port": [443], "action": "reject"},
      {"outbound": "proxy", "rule_set": ["geosite-google"]},
      {"outbound": "direct", "ip_is_private": true},
      {"outbound": "direct", "rule_set": ["geosite-private"]},
      {"outbound": "direct", "rule_set": ["geoip-cn"]},
      {"outbound": "direct", "rule_set": ["geosite-cn"]}
    ],
    "rule_set": [],
    "final": "proxy"
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted default SingBox template", LogLevel::INFO);
        }
    }
    
    // Default Xray test config template (CoreType=2) - inherits xray_base_001
    // Contains API + stats + policy (required for gRPC dynamic injection)
    // ConfigBuilder generates outbounds (with dynamic proxy)
    {
        FullConfigTemplateItem item;
        item.id = "xray_test_001";
        item.parentId = "xray_base_001";
        item.remarks = "Default Xray test config template (for batch testing)";
        item.enabled = 1;
        item.coreType = 2;
        item.config = R"JSON({
  "api": {
    "tag": "api",
    "listen": "127.0.0.1:${apiPort}",
    "services": ["HandlerService", "LoggerService", "StatsService", "RoutingService"]
  },
  "stats": {},
  "policy": {
    "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
    "system": {"statsInboundUplink": true, "statsInboundDownlink": true, "statsOutboundUplink": true, "statsOutboundDownlink": true}
  },
  "inbounds": [
    {
      "tag": "socks",
      "listen": "127.0.0.1",
      "port": "${socksPort}",
      "protocol": "mixed",
      "settings": {"auth": "noauth", "udp": true, "allowTransparent": false},
      "sniffing": {"enabled": true, "destOverride": ["http", "tls", "quic"], "routeOnly": false}
    }
  ],
  "routing": {
    "domainStrategy": "AsIs",
    "rules": [
      {"type": "field", "inboundTag": ["api"], "outboundTag": "api"}
    ]
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted default Xray test template", LogLevel::INFO);
        }
    }
    
    // Default SingBox test config template (CoreType=24) - inherits singbox_base_001
    // SingBox doesn't use API port, so simplified config
    {
        FullConfigTemplateItem item;
        item.id = "singbox_test_001";
        item.parentId = "singbox_base_001";
        item.remarks = "Default SingBox test config template (for batch testing)";
        item.enabled = 1;
        item.coreType = 24;
        item.config = R"JSON({
  "inbounds": [
    {
      "tag": "socks",
      "type": "socks",
      "listen": "127.0.0.1",
      "listen_port": "${socksPort}",
      "users": [
        {"user": "", "password": ""}
      ]
    }
  ],
  "route": {
    "default_domain_resolver": {"server": "local_dns"},
    "rules": [
      {"action": "sniff"},
      {"protocol": ["dns"], "action": "hijack-dns"},
      {"outbound": "proxy", "clash_mode": "Global"},
      {"network": ["udp"], "port": [443], "action": "reject"}
    ],
    "rule_set": [],
    "final": "proxy"
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted default SingBox test template", LogLevel::INFO);
        }
    }
    
    // Default Xray pool config template (CoreType=2) - inherits xray_base_001
    // Contains log + dns + api + stats + policy + routing balancers
    // ConfigBuilder generates outbounds dynamically
    {
        FullConfigTemplateItem item;
        item.id = "pool_xray_001";
        item.parentId = "xray_base_001";
        item.remarks = "Default Xray pool config template";
        item.enabled = 1;
        item.coreType = 2;
        item.config = R"JSON({
  "api": {
    "tag": "api",
    "listen": "127.0.0.1:${apiPort}",
    "services": ["HandlerService", "LoggerService", "StatsService", "RoutingService"]
  },
  "stats": {},
  "policy": {
    "levels": {"0": {"statsUserUplink": true, "statsUserDownlink": true}},
    "system": {"statsInboundUplink": true, "statsInboundDownlink": true, "statsOutboundUplink": true, "statsOutboundDownlink": true}
  },
  "inbounds": [
    {
      "tag": "socks",
      "listen": "127.0.0.1",
      "port": "${socksPort}",
      "protocol": "mixed",
      "settings": {"auth": "noauth", "udp": true, "allowTransparent": false},
      "sniffing": {"enabled": true, "destOverride": ["http", "tls", "quic"], "routeOnly": false}
    }
  ],
  "routing": {
    "domainStrategy": "AsIs",
    "balancers": [
      {
        "tag": "px-",
        "strategy": {
          "type": "leastPing",
          "fallbackProxy": "api"
        },
        "observation": {
          "probeUrl": "https://www.google.com/generate_204",
          "probeInterval": "1m"
        }
      }
    ],
    "rules": [
      {"type": "field", "inboundTag": ["api"], "outboundTag": "api"},
      {"type": "field", "port": "443", "network": "udp", "outboundTag": "block"},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:private"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:private"]},
      {"type": "field", "outboundTag": "direct", "ip": ["geoip:cn"]},
      {"type": "field", "outboundTag": "direct", "domain": ["geosite:cn"]}
    ]
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted default Xray pool template", LogLevel::INFO);
        }
    }
    
    // Default SingBox pool config template (CoreType=24) - inherits singbox_base_001
    // SingBox doesn't use API, simplified config with balancer support
    {
        FullConfigTemplateItem item;
        item.id = "pool_singbox_001";
        item.parentId = "singbox_base_001";
        item.remarks = "Default SingBox pool config template";
        item.enabled = 1;
        item.coreType = 24;
        item.config = R"JSON({
  "inbounds": [
    {
      "tag": "socks",
      "type": "socks",
      "listen": "127.0.0.1",
      "listen_port": "${socksPort}",
      "users": [
        {"user": "", "password": ""}
      ]
    }
  ],
  "route": {
    "default_domain_resolver": {"server": "local_dns"},
    "rules": [
      {"action": "sniff"},
      {"protocol": ["dns"], "action": "hijack-dns"},
      {"outbound": "proxy", "clash_mode": "Global"},
      {"network": ["udp"], "port": [443], "action": "reject"}
    ],
    "rule_set": [],
    "final": "proxy"
  }
})JSON";
        item.tunConfig = "";
        item.addProxyOnly = 0;
        item.proxyDetour = "";
        
        if (save(item)) {
            inserted++;
            Logger::write("[FullConfigTemplateDAO] Inserted default SingBox pool template", LogLevel::INFO);
        }
    }
    
    if (inserted > 0) {
        Logger::write("[FullConfigTemplateDAO] Seeded " + std::to_string(inserted) + " default templates", LogLevel::INFO);
    }
    
    return inserted;
}

bool FullConfigTemplateDAO::prepareStatement(sqlite3_stmt** stmt, const std::string& sql) const {
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("[FullConfigTemplateDAO] Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)) + 
                     " SQL: " + sql, LogLevel::ERR);
        return false;
    }
    return true;
}

void FullConfigTemplateDAO::finalizeStatement(sqlite3_stmt* stmt) const {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

std::optional<FullConfigTemplateItem> FullConfigTemplateDAO::getById(const std::string& id) const {
    const char* sql = 
        "SELECT Id, ParentId, Remarks, Enabled, CoreType, Config, TunConfig, AddProxyOnly, ProxyDetour, "
        "BundleChain, TemplateType, BundleVersion "
        "FROM FullConfigTemplateItem WHERE Id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, sql)) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    FullConfigTemplateItem item;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.id = sqlite3_column_text(stmt, 0) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "";
        item.parentId = sqlite3_column_text(stmt, 1) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) : "";
        item.remarks = sqlite3_column_text(stmt, 2) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
        item.enabled = sqlite3_column_int(stmt, 3);
        item.coreType = sqlite3_column_int(stmt, 4);
        item.config = sqlite3_column_text(stmt, 5) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
        item.tunConfig = sqlite3_column_text(stmt, 6) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) : "";
        item.addProxyOnly = sqlite3_column_int(stmt, 7);
        item.proxyDetour = sqlite3_column_text(stmt, 8) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)) : "";
        item.bundleChain = sqlite3_column_text(stmt, 9) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)) : "";
        item.templateType = sqlite3_column_text(stmt, 10) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)) : "";
        item.bundleVersion = sqlite3_column_text(stmt, 11) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)) : "";
        finalizeStatement(stmt);
        return item;
    }
    
    finalizeStatement(stmt);
    return std::nullopt;
}

std::optional<FullConfigTemplateItem> FullConfigTemplateDAO::getByCoreType(int coreType) const {
    const char* sql = 
        "SELECT Id, ParentId, Remarks, Enabled, CoreType, Config, TunConfig, AddProxyOnly, ProxyDetour, "
        "BundleChain, TemplateType, BundleVersion "
        "FROM FullConfigTemplateItem WHERE CoreType = ? LIMIT 1";
    
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, sql)) {
        return std::nullopt;
    }
    
    sqlite3_bind_int(stmt, 1, coreType);
    
    FullConfigTemplateItem item;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        item.id = sqlite3_column_text(stmt, 0) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "";
        item.parentId = sqlite3_column_text(stmt, 1) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) : "";
        item.remarks = sqlite3_column_text(stmt, 2) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
        item.enabled = sqlite3_column_int(stmt, 3);
        item.coreType = sqlite3_column_int(stmt, 4);
        item.config = sqlite3_column_text(stmt, 5) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
        item.tunConfig = sqlite3_column_text(stmt, 6) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) : "";
        item.addProxyOnly = sqlite3_column_int(stmt, 7);
        item.proxyDetour = sqlite3_column_text(stmt, 8) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)) : "";
        item.bundleChain = sqlite3_column_text(stmt, 9) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)) : "";
        item.templateType = sqlite3_column_text(stmt, 10) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)) : "";
        item.bundleVersion = sqlite3_column_text(stmt, 11) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)) : "";
        finalizeStatement(stmt);
        return item;
    }
    
    finalizeStatement(stmt);
    return std::nullopt;
}

std::vector<FullConfigTemplateItem> FullConfigTemplateDAO::getAllEnabled() const {
    std::vector<FullConfigTemplateItem> items;
    const char* sql = 
        "SELECT Id, ParentId, Remarks, Enabled, CoreType, Config, TunConfig, AddProxyOnly, ProxyDetour, "
        "BundleChain, TemplateType, BundleVersion "
        "FROM FullConfigTemplateItem WHERE Enabled = 1";
    
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, sql)) {
        return items;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FullConfigTemplateItem item;
        item.id = sqlite3_column_text(stmt, 0) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "";
        item.parentId = sqlite3_column_text(stmt, 1) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) : "";
        item.remarks = sqlite3_column_text(stmt, 2) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
        item.enabled = sqlite3_column_int(stmt, 3);
        item.coreType = sqlite3_column_int(stmt, 4);
        item.config = sqlite3_column_text(stmt, 5) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
        item.tunConfig = sqlite3_column_text(stmt, 6) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) : "";
        item.addProxyOnly = sqlite3_column_int(stmt, 7);
        item.proxyDetour = sqlite3_column_text(stmt, 8) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)) : "";
        item.bundleChain = sqlite3_column_text(stmt, 9) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)) : "";
        item.templateType = sqlite3_column_text(stmt, 10) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)) : "";
        item.bundleVersion = sqlite3_column_text(stmt, 11) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)) : "";
        items.push_back(item);
    }
    
    finalizeStatement(stmt);
    return items;
}

std::vector<FullConfigTemplateItem> FullConfigTemplateDAO::getAll() const {
    std::vector<FullConfigTemplateItem> items;
    const char* sql = 
        "SELECT Id, ParentId, Remarks, Enabled, CoreType, Config, TunConfig, AddProxyOnly, ProxyDetour, "
        "BundleChain, TemplateType, BundleVersion "
        "FROM FullConfigTemplateItem";
    
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, sql)) {
        return items;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FullConfigTemplateItem item;
        item.id = sqlite3_column_text(stmt, 0) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)) : "";
        item.parentId = sqlite3_column_text(stmt, 1) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) : "";
        item.remarks = sqlite3_column_text(stmt, 2) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) : "";
        item.enabled = sqlite3_column_int(stmt, 3);
        item.coreType = sqlite3_column_int(stmt, 4);
        item.config = sqlite3_column_text(stmt, 5) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)) : "";
        item.tunConfig = sqlite3_column_text(stmt, 6) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) : "";
        item.addProxyOnly = sqlite3_column_int(stmt, 7);
        item.proxyDetour = sqlite3_column_text(stmt, 8) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8)) : "";
        item.bundleChain = sqlite3_column_text(stmt, 9) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9)) : "";
        item.templateType = sqlite3_column_text(stmt, 10) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10)) : "";
        item.bundleVersion = sqlite3_column_text(stmt, 11) ? reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11)) : "";
        items.push_back(item);
    }
    
    finalizeStatement(stmt);
    return items;
}

std::vector<FullConfigTemplateItem> FullConfigTemplateDAO::getByBundleId(
    const std::string& bundleId) const {
    std::vector<FullConfigTemplateItem> result;
    if (bundleId.empty()) {
        return result;
    }
    const std::vector<FullConfigTemplateItem> all = getAll();
    for (std::vector<FullConfigTemplateItem>::const_iterator it = all.begin();
         it != all.end(); ++it) {
        if (it->bundleChain.empty()) {
            continue;
        }
        // 解析 bundleChain 后按 bundleIds 逐项精确比较；
        // 不能用 bundleChain.find(bundleId) 子串匹配——那会让
        // 「只含 xray_LOG2 的链」被 xray_LOG 的查询误命中。
        boost::system::error_code ec;
        boost::json::value val = boost::json::parse(it->bundleChain, ec);
        if (ec || !val.is_object()) {
            continue;
        }
        const boost::json::object& obj = val.as_object();
        if (!obj.contains("bundleIds") || !obj.at("bundleIds").is_array()) {
            continue;
        }
        const boost::json::array& ids = obj.at("bundleIds").as_array();
        bool matched = false;
        for (boost::json::array::const_iterator ai = ids.begin(); ai != ids.end(); ++ai) {
            if (ai->is_string() && std::string(ai->as_string().c_str()) == bundleId) {
                matched = true;
                break;
            }
        }
        if (matched) {
            result.push_back(*it);
        }
    }
    return result;
}

bool FullConfigTemplateDAO::save(const FullConfigTemplateItem& item) const {
    const char* sql = 
        "INSERT OR REPLACE INTO FullConfigTemplateItem "
        "(Id, ParentId, Remarks, Enabled, CoreType, Config, TunConfig, AddProxyOnly, ProxyDetour, "
        "BundleChain, TemplateType, BundleVersion) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
    
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, sql)) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, item.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, item.parentId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, item.remarks.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, item.enabled);
    sqlite3_bind_int(stmt, 5, item.coreType);
    sqlite3_bind_text(stmt, 6, item.config.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, item.tunConfig.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 8, item.addProxyOnly);
    sqlite3_bind_text(stmt, 9, item.proxyDetour.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, item.bundleChain.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, item.templateType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, item.bundleVersion.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        Logger::write("[FullConfigTemplateDAO] Failed to save template: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    
    return true;
}

bool FullConfigTemplateDAO::deleteById(const std::string& id) const {
    const char* sql = "DELETE FROM FullConfigTemplateItem WHERE Id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, sql)) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        Logger::write("[FullConfigTemplateDAO] Failed to delete template: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    
    return true;
}

bool FullConfigTemplateDAO::isTemplateInSync(const std::string& templateId) const {
    std::optional<FullConfigTemplateItem> item = getById(templateId);
    if (!item.has_value()) {
        return false;
    }
    // Templates without a BundleChain are always considered in sync
    if (item->bundleChain.empty()) {
        return true;
    }
    
    // Parse recorded versions: [{"bundleId":"...","version":N}, ...]
    std::map<std::string, int> recorded;
    boost::system::error_code ec;
    boost::json::value recVal = boost::json::parse(item->bundleVersion, ec);
    if (!ec && recVal.is_array()) {
        for (const boost::json::value& entry : recVal.as_array()) {
            if (!entry.is_object()) {
                continue;
            }
            const boost::json::object& entryObj = entry.as_object();
            if (entryObj.contains("bundleId") && entryObj.at("bundleId").is_string() &&
                entryObj.contains("version") && entryObj.at("version").is_int64()) {
                recorded[std::string(entryObj.at("bundleId").get_string().c_str())] =
                    static_cast<int>(entryObj.at("version").as_int64());
            }
        }
    }
    
    // Parse the BundleChain to obtain referenced bundle ids
    boost::json::value chainVal = boost::json::parse(item->bundleChain, ec);
    if (ec || !chainVal.is_object()) {
        return false;
    }
    const boost::json::object& chainObj = chainVal.as_object();
    if (!chainObj.contains("bundleIds") || !chainObj.at("bundleIds").is_array()) {
        return false;
    }
    
    // Compare each bundle's current version against the recorded version
    for (const boost::json::value& idVal : chainObj.at("bundleIds").as_array()) {
        if (!idVal.is_string()) {
            continue;
        }
        std::string bundleId = std::string(idVal.get_string().c_str());
        std::map<std::string, int>::const_iterator rit = recorded.find(bundleId);
        if (rit == recorded.end()) {
            // Referenced bundle has no recorded version -> not in sync
            return false;
        }
        
        int currentVersion = -1;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT Version FROM ComponentBundle WHERE Id = ?";
        if (prepareStatement(&stmt, sql)) {
            sqlite3_bind_text(stmt, 1, bundleId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                currentVersion = sqlite3_column_int(stmt, 0);
            }
            finalizeStatement(stmt);
        }
        if (currentVersion < 0 || currentVersion > rit->second) {
            return false;
        }
    }
    return true;
}

} // namespace db