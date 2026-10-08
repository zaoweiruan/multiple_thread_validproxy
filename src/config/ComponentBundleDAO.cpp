#include "config/ComponentBundleDAO.h"
#include "Logger.h"
#include <sqlite3.h>
#include <sstream>
#include <boost/json.hpp>

namespace config {

ComponentBundleDAO::ComponentBundleDAO(sqlite3* db) : db_(db) {
    // Table creation will be handled by createTableIfNotExists()
}

bool ComponentBundleDAO::tableExists() const {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT name FROM sqlite_master WHERE type='table' AND name='ComponentBundle'";
    
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("[ComponentBundleDAO] Failed to prepare table exists check: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

bool ComponentBundleDAO::createTableIfNotExists() const {
    const char* sql = 
        "CREATE TABLE IF NOT EXISTS ComponentBundle ("
        "Id varchar PRIMARY KEY,"
        "Name varchar NOT NULL,"
        "Description varchar,"
        "CoreType INTEGER NOT NULL,"
        "TemplateType varchar NOT NULL,"
        "Version INTEGER NOT NULL DEFAULT 1,"
        "Enabled INTEGER NOT NULL DEFAULT 1,"
        "Components varchar NOT NULL,"
        "CreatedAt TEXT NOT NULL,"
        "UpdatedAt TEXT NOT NULL"
        ");";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
        if (errMsg) sqlite3_free(errMsg);
        Logger::write("[ComponentBundleDAO] Failed to create table: " + msg, LogLevel::ERR);
        return false;
    }
    
    Logger::write("[ComponentBundleDAO] Created ComponentBundle table", LogLevel::INFO);
    return true;
}

bool ComponentBundleDAO::prepareStatement(sqlite3_stmt** stmt, const std::string& sql) const {
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, stmt, nullptr);
    if (rc != SQLITE_OK) {
        Logger::write("[ComponentBundleDAO] Failed to prepare statement: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    return true;
}

void ComponentBundleDAO::finalizeStatement(sqlite3_stmt* stmt) const {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

std::string ComponentBundleDAO::serializeBundle(const ComponentBundle& bundle) const {
    boost::json::object j;
    j["id"] = bundle.id;
    j["name"] = bundle.name;
    j["description"] = bundle.description;
    j["coreType"] = static_cast<int>(bundle.coreType);
    j["templateType"] = static_cast<int>(bundle.templateType);
    j["version"] = bundle.version;
    j["enabled"] = bundle.enabled;
    
    boost::json::array components;
    for (const auto& comp : bundle.components) {
        boost::json::object compJson;
        compJson["type"] = static_cast<int>(comp.type);
        
        boost::json::object paramsObj;
        for (const auto& param : comp.params) {
            paramsObj[param.first] = param.second;
        }
        compJson["params"] = paramsObj;
        
        if (!comp.id.empty()) {
            compJson["id"] = comp.id;
        }
        components.push_back(compJson);
    }
    j["components"] = components;
    
    return boost::json::serialize(j);
}

ComponentBundle ComponentBundleDAO::deserializeBundle(const std::string& json) const {
    ComponentBundle bundle;
    boost::json::value j = boost::json::parse(json);
    const auto& obj = j.as_object();
    
    // Get string fields using find() (works with const objects)
    auto idIt = obj.find("id");
    if (idIt != obj.end()) {
        bundle.id = std::string(idIt->value().as_string());
    }
    
    auto nameIt = obj.find("name");
    if (nameIt != obj.end()) {
        bundle.name = std::string(nameIt->value().as_string());
    }
    
    auto descIt = obj.find("description");
    if (descIt != obj.end()) {
        bundle.description = std::string(descIt->value().as_string());
    }
    
    // Get integer fields
    auto coreTypeIt = obj.find("coreType");
    bundle.coreType = static_cast<CoreType>(static_cast<int>(coreTypeIt != obj.end() ? coreTypeIt->value().as_int64() : 2));
    
    auto templateTypeIt = obj.find("templateType");
    bundle.templateType = static_cast<TemplateType>(static_cast<int>(templateTypeIt != obj.end() ? templateTypeIt->value().as_int64() : 0));
    
    auto versionIt = obj.find("version");
    bundle.version = static_cast<int>(versionIt != obj.end() ? versionIt->value().as_int64() : 1);
    
    auto enabledIt = obj.find("enabled");
    bundle.enabled = enabledIt != obj.end() ? enabledIt->value().as_bool() : true;
    
    // Get components array
    auto componentsIt = obj.find("components");
    if (componentsIt != obj.end() && componentsIt->value().is_array()) {
        for (const auto& compJson : componentsIt->value().as_array()) {
            const auto& compObj = compJson.as_object();
            ComponentConfig comp;
            
            auto typeIt = compObj.find("type");
            comp.type = static_cast<ComponentType>(static_cast<int>(typeIt != compObj.end() ? typeIt->value().as_int64() : 0));
            
            auto idIt2 = compObj.find("id");
            comp.id = idIt2 != compObj.end() ? std::string(idIt2->value().as_string()) : "";
            
            auto paramsIt = compObj.find("params");
            if (paramsIt != compObj.end() && paramsIt->value().is_object()) {
                for (const auto& paramPair : paramsIt->value().as_object()) {
                    comp.params[std::string(paramPair.key())] = paramPair.value();
                }
            }
            bundle.components.push_back(comp);
        }
    }
    
    return bundle;
}

std::optional<ComponentBundle> ComponentBundleDAO::getById(const std::string& id) const {
    std::string sql = "SELECT Id, Name, Description, CoreType, TemplateType, Version, Enabled, Components FROM ComponentBundle WHERE Id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* idStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* nameStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* descStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int coreType = sqlite3_column_int(stmt, 3);
        const char* templateTypeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int version = sqlite3_column_int(stmt, 5);
        int enabled = sqlite3_column_int(stmt, 6);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
        // Parse template type string
        std::string templateType = templateTypeStr ? templateTypeStr : "";
        if (templateType == "base") bundle.templateType = TemplateType::BASE;
        else if (templateType == "test") bundle.templateType = TemplateType::TEST;
        else if (templateType == "standalone") bundle.templateType = TemplateType::STANDALONE;
        else if (templateType == "pool") bundle.templateType = TemplateType::POOL;
        else bundle.templateType = TemplateType::BASE;
        
        bundle.version = version;
        bundle.enabled = (enabled != 0);
        bundle.components = deserializeBundle(componentsStr ? componentsStr : "{}").components;
        
        finalizeStatement(stmt);
        return bundle;
    }
    
    finalizeStatement(stmt);
    return std::nullopt;
}

std::optional<ComponentBundle> ComponentBundleDAO::getByIdAndVersion(
    const std::string& id, int version) const {
    std::string sql = "SELECT Id, Name, Description, CoreType, TemplateType, Version, Enabled, Components FROM ComponentBundle WHERE Id = ? AND Version = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return std::nullopt;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, version);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* idStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* nameStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* descStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int coreType = sqlite3_column_int(stmt, 3);
        const char* templateTypeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int enabled = sqlite3_column_int(stmt, 6);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
        std::string templateType = templateTypeStr ? templateTypeStr : "";
        if (templateType == "base") bundle.templateType = TemplateType::BASE;
        else if (templateType == "test") bundle.templateType = TemplateType::TEST;
        else if (templateType == "standalone") bundle.templateType = TemplateType::STANDALONE;
        else if (templateType == "pool") bundle.templateType = TemplateType::POOL;
        else bundle.templateType = TemplateType::BASE;
        
        bundle.version = version;
        bundle.enabled = (enabled != 0);
        bundle.components = deserializeBundle(componentsStr ? componentsStr : "{}").components;
        
        finalizeStatement(stmt);
        return bundle;
    }
    
    finalizeStatement(stmt);
    return std::nullopt;
}

std::vector<ComponentBundle> ComponentBundleDAO::getAll() const {
    std::vector<ComponentBundle> bundles;
    std::string sql = "SELECT Id, Name, Description, CoreType, TemplateType, Version, Enabled, Components FROM ComponentBundle";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return bundles;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* idStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* nameStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* descStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int coreType = sqlite3_column_int(stmt, 3);
        const char* templateTypeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int version = sqlite3_column_int(stmt, 5);
        int enabled = sqlite3_column_int(stmt, 6);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
        std::string templateType = templateTypeStr ? templateTypeStr : "";
        if (templateType == "base") bundle.templateType = TemplateType::BASE;
        else if (templateType == "test") bundle.templateType = TemplateType::TEST;
        else if (templateType == "standalone") bundle.templateType = TemplateType::STANDALONE;
        else if (templateType == "pool") bundle.templateType = TemplateType::POOL;
        else bundle.templateType = TemplateType::BASE;
        
        bundle.version = version;
        bundle.enabled = (enabled != 0);
        bundle.components = deserializeBundle(componentsStr ? componentsStr : "{}").components;
        
        bundles.push_back(bundle);
    }
    
    finalizeStatement(stmt);
    return bundles;
}

std::vector<ComponentBundle> ComponentBundleDAO::getAllEnabled() const {
    std::vector<ComponentBundle> bundles;
    std::string sql = "SELECT Id, Name, Description, CoreType, TemplateType, Version, Enabled, Components FROM ComponentBundle WHERE Enabled = 1";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return bundles;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* idStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* nameStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* descStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int coreType = sqlite3_column_int(stmt, 3);
        const char* templateTypeStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        int version = sqlite3_column_int(stmt, 5);
        int enabled = sqlite3_column_int(stmt, 6);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
        std::string templateType = templateTypeStr ? templateTypeStr : "";
        if (templateType == "base") bundle.templateType = TemplateType::BASE;
        else if (templateType == "test") bundle.templateType = TemplateType::TEST;
        else if (templateType == "standalone") bundle.templateType = TemplateType::STANDALONE;
        else if (templateType == "pool") bundle.templateType = TemplateType::POOL;
        else bundle.templateType = TemplateType::BASE;
        
        bundle.version = version;
        bundle.enabled = (enabled != 0);
        bundle.components = deserializeBundle(componentsStr ? componentsStr : "{}").components;
        
        bundles.push_back(bundle);
    }
    
    finalizeStatement(stmt);
    return bundles;
}

std::vector<ComponentBundle> ComponentBundleDAO::getByCoreType(CoreType coreType) const {
    std::vector<ComponentBundle> bundles = getAll();
    std::vector<ComponentBundle> result;
    
    for (const auto& bundle : bundles) {
        if (bundle.coreType == coreType) {
            result.push_back(bundle);
        }
    }
    
    return result;
}

std::vector<ComponentBundle> ComponentBundleDAO::getByTemplateType(TemplateType templateType) const {
    std::vector<ComponentBundle> bundles = getAll();
    std::vector<ComponentBundle> result;
    
    for (const auto& bundle : bundles) {
        if (bundle.templateType == templateType) {
            result.push_back(bundle);
        }
    }
    
    return result;
}

bool ComponentBundleDAO::save(const ComponentBundle& bundle) const {
    std::string sql = "INSERT OR REPLACE INTO ComponentBundle (Id, Name, Description, CoreType, TemplateType, Version, Enabled, Components, CreatedAt, UpdatedAt) VALUES (?, ?, ?, ?, ?, ?, ?, ?, datetime('now'), datetime('now'))";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return false;
    }
    
    // Convert enum to string for storage
    std::string templateTypeStr;
    switch (bundle.templateType) {
        case TemplateType::BASE: templateTypeStr = "base"; break;
        case TemplateType::TEST: templateTypeStr = "test"; break;
        case TemplateType::STANDALONE: templateTypeStr = "standalone"; break;
        case TemplateType::POOL: templateTypeStr = "pool"; break;
        default: templateTypeStr = "base"; break;
    }
    
    sqlite3_bind_text(stmt, 1, bundle.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bundle.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, bundle.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(bundle.coreType));
    sqlite3_bind_text(stmt, 5, templateTypeStr.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, bundle.version);
    sqlite3_bind_int(stmt, 7, bundle.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 8, serializeBundle(bundle).c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        Logger::write("[ComponentBundleDAO] Failed to save bundle: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    
    return true;
}

int ComponentBundleDAO::updateVersion(const std::string& id) const {
    std::optional<ComponentBundle> existing = getById(id);
    if (!existing) {
        return -1;
    }
    
    int newVersion = existing->version + 1;
    std::string sql = "UPDATE ComponentBundle SET Version = ?, UpdatedAt = datetime('now') WHERE Id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, newVersion);
    sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        Logger::write("[ComponentBundleDAO] Failed to update version: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return -1;
    }
    
    return newVersion;
}

bool ComponentBundleDAO::deleteById(const std::string& id) const {
    std::string sql = "DELETE FROM ComponentBundle WHERE Id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    finalizeStatement(stmt);
    
    if (rc != SQLITE_DONE) {
        Logger::write("[ComponentBundleDAO] Failed to delete bundle: " + std::string(sqlite3_errmsg(db_)), LogLevel::ERR);
        return false;
    }
    
    return true;
}

bool ComponentBundleDAO::exists(const std::string& id) const {
    std::string sql = "SELECT COUNT(*) FROM ComponentBundle WHERE Id = ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    
    bool exists = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = (sqlite3_column_int(stmt, 0) > 0);
    }
    
    finalizeStatement(stmt);
    return exists;
}

int ComponentBundleDAO::getReferencedCount(const std::string& id) const {
    // Count how many templates reference this bundle in their BundleChain
    std::string sql = "SELECT COUNT(*) FROM FullConfigTemplateItem WHERE BundleChain LIKE ?";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return 0;
    }
    
    std::string pattern = "%\"" + id + "\"%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    finalizeStatement(stmt);
    return count;
}

int ComponentBundleDAO::seedDefaultBundles() const {
    // Check if table already has data
    std::vector<ComponentBundle> existing = getAll();
    if (!existing.empty()) {
        Logger::write("[ComponentBundleDAO] Table already has bundles, skipping seed", LogLevel::INFO);
        return 0;
    }
    
    int inserted = 0;
    
    // === Xray Base Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "base_xray_001";
        bundle.name = "Xray Base Components";
        bundle.description = "Shared base components for Xray (log, dns)";
        bundle.coreType = CoreType::XRAY;
        bundle.templateType = TemplateType::BASE;
        bundle.version = 1;
        bundle.enabled = true;
        
        // Log component
        ComponentConfig logComp;
        logComp.type = ComponentType::LOG;
        logComp.params["loglevel"] = "warning";
        bundle.components.push_back(logComp);
        
        // DNS component
        ComponentConfig dnsComp;
        dnsComp.type = ComponentType::DNS;
        boost::json::object dnsParams;
        dnsParams["servers"] = boost::json::array({"1.1.1.1", "8.8.8.8", "https://doh.pub/dns-query"});
        dnsParams["fallback"] = boost::json::array({"1.1.1.1", "8.8.8.8"});
        dnsParams["fallbackFilterGeoip"] = "cn";
        dnsComp.params["dns"] = dnsParams;
        bundle.components.push_back(dnsComp);
        
        if (save(bundle)) inserted++;
    }
    
    // === SingBox Base Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "base_singbox_001";
        bundle.name = "SingBox Base Components";
        bundle.description = "Shared base components for SingBox (log, dns)";
        bundle.coreType = CoreType::SINGBOX;
        bundle.templateType = TemplateType::BASE;
        bundle.version = 1;
        bundle.enabled = true;
        
        // Log component
        ComponentConfig logComp;
        logComp.type = ComponentType::LOG;
        logComp.params["loglevel"] = "warn";
        bundle.components.push_back(logComp);
        
        // DNS component
        ComponentConfig dnsComp;
        dnsComp.type = ComponentType::DNS;
        boost::json::object dnsParams;
        dnsParams["servers"] = boost::json::array({"https://doh.pub/dns-query", "https://dns.cloudflare.com/dns-query"});
        dnsComp.params["dns"] = dnsParams;
        bundle.components.push_back(dnsComp);
        
        if (save(bundle)) inserted++;
    }
    
    // === Xray Test Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "test_xray_001";
        bundle.name = "Xray Test Components";
        bundle.description = "Components for batch test configuration";
        bundle.coreType = CoreType::XRAY;
        bundle.templateType = TemplateType::TEST;
        bundle.version = 1;
        bundle.enabled = true;
        
        // API component
        ComponentConfig apiComp;
        apiComp.type = ComponentType::API;
        apiComp.params["tag"] = "api";
        apiComp.params["listen"] = "127.0.0.1:${apiPort}";
        bundle.components.push_back(apiComp);
        
        // SOCKS inbound
        ComponentConfig socksInbound;
        socksInbound.type = ComponentType::INBOUND;
        socksInbound.id = "socks";
        boost::json::object socksParams;
        socksParams["tag"] = "socks";
        socksParams["listen"] = "127.0.0.1:${socksPort}";
        socksParams["port"] = "${socksPort}";
        socksParams["protocol"] = "mixed";
        socksInbound.params["inbound"] = socksParams;
        bundle.components.push_back(socksInbound);
        
        // Proxy outbound
        ComponentConfig proxyOutbound;
        proxyOutbound.type = ComponentType::OUTBOUND;
        proxyOutbound.id = "proxy";
        boost::json::object proxyParams;
        proxyParams["tag"] = "proxy";
        proxyParams["type"] = "proxy";
        proxyOutbound.params["outbound"] = proxyParams;
        bundle.components.push_back(proxyOutbound);
        
        // Routing component
        ComponentConfig routingComp;
        routingComp.type = ComponentType::ROUTING;
        boost::json::object routingParams;
        routingParams["domainStrategy"] = "AsIs";
        boost::json::array rules;
        boost::json::object socksRule;
        socksRule["type"] = "field";
        socksRule["inboundTag"] = boost::json::array({"socks"});
        socksRule["outboundTag"] = "proxy";
        rules.push_back(socksRule);
        routingParams["rules"] = rules;
        routingComp.params["routing"] = routingParams;
        bundle.components.push_back(routingComp);
        
        // Observatory component
        ComponentConfig obsComp;
        obsComp.type = ComponentType::OBSERVATORY;
        boost::json::object obsParams;
        obsParams["subjectName"] = boost::json::array({"proxy"});
        obsParams["probeUrl"] = "${probeUrl}";
        obsParams["probeInterval"] = 60000;
        obsComp.params["observatory"] = obsParams;
        bundle.components.push_back(obsComp);
        
        // BurstObservatory component
        ComponentConfig burstObsComp;
        burstObsComp.type = ComponentType::BURST_OBSERVATORY;
        boost::json::object burstObsParams;
        burstObsComp.params["burstObservatory"] = burstObsParams;
        bundle.components.push_back(burstObsComp);
        
        if (save(bundle)) inserted++;
    }
    
    // === SingBox Test Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "test_singbox_001";
        bundle.name = "SingBox Test Components";
        bundle.description = "Components for batch test configuration (SingBox)";
        bundle.coreType = CoreType::SINGBOX;
        bundle.templateType = TemplateType::TEST;
        bundle.version = 1;
        bundle.enabled = true;
        
        // SOCKS inbound
        ComponentConfig socksInbound;
        socksInbound.type = ComponentType::INBOUND;
        socksInbound.id = "socks";
        boost::json::object socksParams;
        socksParams["tag"] = "socks";
        socksParams["type"] = "socks";
        socksParams["listen"] = "127.0.0.1:${socksPort}";
        socksParams["listen_port"] = "${socksPort}";
        socksInbound.params["inbound"] = socksParams;
        bundle.components.push_back(socksInbound);
        
        // SOCKS outbound
        ComponentConfig socksOutbound;
        socksOutbound.type = ComponentType::OUTBOUND;
        socksOutbound.id = "socks-out";
        boost::json::object socksOutParams;
        socksOutParams["tag"] = "socks-out";
        socksOutParams["type"] = "socks";
        socksOutbound.params["outbound"] = socksOutParams;
        bundle.components.push_back(socksOutbound);
        
        // Route component (SingBox uses "route" instead of "routing")
        ComponentConfig routeComp;
        routeComp.type = ComponentType::ROUTING;
        boost::json::object routeParams;
        routeParams["rule_set"] = boost::json::array();
        routeParams["rules"] = boost::json::array();
        routeComp.params["route"] = routeParams;
        bundle.components.push_back(routeComp);
        
        if (save(bundle)) inserted++;
    }
    
    // === Xray Standalone Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "standalone_xray_001";
        bundle.name = "Xray Standalone Components";
        bundle.description = "Components for standalone proxy configuration";
        bundle.coreType = CoreType::XRAY;
        bundle.templateType = TemplateType::STANDALONE;
        bundle.version = 1;
        bundle.enabled = true;
        
        // API component
        ComponentConfig apiComp;
        apiComp.type = ComponentType::API;
        apiComp.params["tag"] = "api";
        apiComp.params["listen"] = "127.0.0.1:${apiPort}";
        bundle.components.push_back(apiComp);
        
        // SOCKS inbound
        ComponentConfig socksInbound;
        socksInbound.type = ComponentType::INBOUND;
        socksInbound.id = "socks";
        boost::json::object socksParams;
        socksParams["tag"] = "socks";
        socksParams["listen"] = "127.0.0.1:${socksPort}";
        socksParams["port"] = "${socksPort}";
        socksParams["protocol"] = "mixed";
        socksInbound.params["inbound"] = socksParams;
        bundle.components.push_back(socksInbound);
        
        // Proxy outbound
        ComponentConfig proxyOutbound;
        proxyOutbound.type = ComponentType::OUTBOUND;
        proxyOutbound.id = "proxy";
        boost::json::object proxyParams;
        proxyParams["tag"] = "proxy";
        proxyParams["type"] = "proxy";
        proxyOutbound.params["outbound"] = proxyParams;
        bundle.components.push_back(proxyOutbound);
        
        // Routing component
        ComponentConfig routingComp;
        routingComp.type = ComponentType::ROUTING;
        boost::json::object routingParams;
        routingParams["domainStrategy"] = "AsIs";
        routingComp.params["routing"] = routingParams;
        bundle.components.push_back(routingComp);
        
        if (save(bundle)) inserted++;
    }
    
    // === SingBox Standalone Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "standalone_singbox_001";
        bundle.name = "SingBox Standalone Components";
        bundle.description = "Components for standalone proxy configuration (SingBox)";
        bundle.coreType = CoreType::SINGBOX;
        bundle.templateType = TemplateType::STANDALONE;
        bundle.version = 1;
        bundle.enabled = true;
        
        // SOCKS inbound
        ComponentConfig socksInbound;
        socksInbound.type = ComponentType::INBOUND;
        socksInbound.id = "socks";
        boost::json::object socksParams;
        socksParams["tag"] = "socks";
        socksParams["type"] = "socks";
        socksParams["listen"] = "127.0.0.1:${socksPort}";
        socksParams["listen_port"] = "${socksPort}";
        socksInbound.params["inbound"] = socksParams;
        bundle.components.push_back(socksInbound);
        
        // SOCKS outbound
        ComponentConfig socksOutbound;
        socksOutbound.type = ComponentType::OUTBOUND;
        socksOutbound.id = "socks-out";
        boost::json::object socksOutParams;
        socksOutParams["tag"] = "socks-out";
        socksOutParams["type"] = "socks";
        socksOutbound.params["outbound"] = socksOutParams;
        bundle.components.push_back(socksOutbound);
        
        if (save(bundle)) inserted++;
    }
    
    // === Xray Pool Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "pool_xray_001";
        bundle.name = "Xray Pool Components";
        bundle.description = "Components for proxy pool configuration";
        bundle.coreType = CoreType::XRAY;
        bundle.templateType = TemplateType::POOL;
        bundle.version = 1;
        bundle.enabled = true;
        
        // API component
        ComponentConfig apiComp;
        apiComp.type = ComponentType::API;
        apiComp.params["tag"] = "api";
        apiComp.params["listen"] = "127.0.0.1:${apiPort}";
        bundle.components.push_back(apiComp);
        
        // SOCKS inbound
        ComponentConfig socksInbound;
        socksInbound.type = ComponentType::INBOUND;
        socksInbound.id = "socks";
        boost::json::object socksParams;
        socksParams["tag"] = "socks";
        socksParams["listen"] = "127.0.0.1:${socksPort}";
        socksParams["port"] = "${socksPort}";
        socksParams["protocol"] = "mixed";
        socksInbound.params["inbound"] = socksParams;
        bundle.components.push_back(socksInbound);
        
        // Proxy outbound
        ComponentConfig proxyOutbound;
        proxyOutbound.type = ComponentType::OUTBOUND;
        proxyOutbound.id = "proxy";
        boost::json::object proxyParams;
        proxyParams["tag"] = "proxy";
        proxyParams["type"] = "proxy";
        proxyOutbound.params["outbound"] = proxyParams;
        bundle.components.push_back(proxyOutbound);
        
        // Balancer outbound
        ComponentConfig balancerOutbound;
        balancerOutbound.type = ComponentType::OUTBOUND;
        balancerOutbound.id = "balancer";
        boost::json::object balancerParams;
        balancerParams["tag"] = "balancer-out";
        balancerParams["type"] = "balancer";
        boost::json::object balancerSettings;
        balancerSettings["balancerTag"] = "balancer";
        balancerParams["settings"] = balancerSettings;
        balancerOutbound.params["outbound"] = balancerParams;
        bundle.components.push_back(balancerOutbound);
        
        // Routing component with balancers
        ComponentConfig routingComp;
        routingComp.type = ComponentType::ROUTING;
        boost::json::object routingParams;
        boost::json::array balancers;
        boost::json::object balancer;
        balancer["tag"] = "balancer";
        balancer["selector"] = boost::json::array();
        balancer["strategy"] = "leastPing";
        balancers.push_back(balancer);
        routingParams["balancers"] = balancers;
        routingComp.params["routing"] = routingParams;
        bundle.components.push_back(routingComp);
        
        if (save(bundle)) inserted++;
    }
    
    // === SingBox Pool Bundle ===
    {
        ComponentBundle bundle;
        bundle.id = "pool_singbox_001";
        bundle.name = "SingBox Pool Components";
        bundle.description = "Components for proxy pool configuration (SingBox)";
        bundle.coreType = CoreType::SINGBOX;
        bundle.templateType = TemplateType::POOL;
        bundle.version = 1;
        bundle.enabled = true;
        
        // SOCKS inbound
        ComponentConfig socksInbound;
        socksInbound.type = ComponentType::INBOUND;
        socksInbound.id = "socks";
        boost::json::object socksParams;
        socksParams["tag"] = "socks";
        socksParams["type"] = "socks";
        socksParams["listen"] = "127.0.0.1:${socksPort}";
        socksParams["listen_port"] = "${socksPort}";
        socksInbound.params["inbound"] = socksParams;
        bundle.components.push_back(socksInbound);
        
        // SOCKS outbound
        ComponentConfig socksOutbound;
        socksOutbound.type = ComponentType::OUTBOUND;
        socksOutbound.id = "socks-out";
        boost::json::object socksOutParams;
        socksOutParams["tag"] = "socks-out";
        socksOutParams["type"] = "socks";
        socksOutbound.params["outbound"] = socksOutParams;
        bundle.components.push_back(socksOutbound);
        
        // Balancers (SingBox uses top-level "balancers")
        ComponentConfig balancerComp;
        balancerComp.type = ComponentType::ROUTING;
        boost::json::array balancersJson;
        boost::json::object balancer;
        balancer["tag"] = "balancer";
        balancer["selector"] = boost::json::array();
        balancersJson.push_back(balancer);
        balancerComp.params["balancers"] = balancersJson;
        bundle.components.push_back(balancerComp);
        
        if (save(bundle)) inserted++;
    }
    
    Logger::write("[ComponentBundleDAO] Seeded " + std::to_string(inserted) + " default bundles", LogLevel::INFO);
    return inserted;
}

} // namespace config
