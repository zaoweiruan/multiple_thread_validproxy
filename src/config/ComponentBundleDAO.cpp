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
    return migrateDropTemplateTypeColumn();
}

bool ComponentBundleDAO::migrateDropTemplateTypeColumn() const {
    // 旧库的 ComponentBundle.TemplateType 为 NOT NULL 且无默认值；
    // 组件包已不再从属于模板角色，该列必须移除，否则新 INSERT 会触发 NOT NULL 约束失败。
    sqlite3_stmt* stmt = nullptr;
    if (!prepareStatement(&stmt, "PRAGMA table_info(ComponentBundle)")) {
        return false;
    }
    bool hasLegacyColumn = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* colName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (colName && std::string(colName) == "TemplateType") {
            hasLegacyColumn = true;
            break;
        }
    }
    finalizeStatement(stmt);
    if (!hasLegacyColumn) {
        return true;
    }

    // 重建表（兼容不支持 ALTER TABLE DROP COLUMN 的旧 SQLite）
    const char* migrate =
        "PRAGMA foreign_keys=off;"
        "BEGIN;"
        "CREATE TABLE ComponentBundle_new ("
        "Id varchar PRIMARY KEY,"
        "Name varchar NOT NULL,"
        "Description varchar,"
        "CoreType INTEGER NOT NULL,"
        "Version INTEGER NOT NULL DEFAULT 1,"
        "Enabled INTEGER NOT NULL DEFAULT 1,"
        "Components varchar NOT NULL,"
        "CreatedAt TEXT NOT NULL,"
        "UpdatedAt TEXT NOT NULL"
        ");"
        "INSERT INTO ComponentBundle_new "
        "(Id, Name, Description, CoreType, Version, Enabled, Components, CreatedAt, UpdatedAt) "
        "SELECT Id, Name, Description, CoreType, Version, Enabled, Components, CreatedAt, UpdatedAt "
        "FROM ComponentBundle;"
        "DROP TABLE ComponentBundle;"
        "ALTER TABLE ComponentBundle_new RENAME TO ComponentBundle;"
        "COMMIT;";

    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, migrate, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string msg = errMsg ? errMsg : sqlite3_errmsg(db_);
        if (errMsg) sqlite3_free(errMsg);
        Logger::write("[ComponentBundleDAO] Failed to drop TemplateType column: " + msg,
                      LogLevel::ERR);
        return false;
    }
    Logger::write("[ComponentBundleDAO] Dropped legacy TemplateType column", LogLevel::INFO);
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
    std::string sql = "SELECT Id, Name, Description, CoreType, Version, Enabled, Components FROM ComponentBundle WHERE Id = ?";
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
        int version = sqlite3_column_int(stmt, 4);
        int enabled = sqlite3_column_int(stmt, 5);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
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
    std::string sql = "SELECT Id, Name, Description, CoreType, Version, Enabled, Components FROM ComponentBundle WHERE Id = ? AND Version = ?";
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
        int enabled = sqlite3_column_int(stmt, 5);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
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
    std::string sql = "SELECT Id, Name, Description, CoreType, Version, Enabled, Components FROM ComponentBundle";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return bundles;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* idStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* nameStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* descStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int coreType = sqlite3_column_int(stmt, 3);
        int version = sqlite3_column_int(stmt, 4);
        int enabled = sqlite3_column_int(stmt, 5);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
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
    std::string sql = "SELECT Id, Name, Description, CoreType, Version, Enabled, Components FROM ComponentBundle WHERE Enabled = 1";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return bundles;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* idStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        const char* nameStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* descStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        int coreType = sqlite3_column_int(stmt, 3);
        int version = sqlite3_column_int(stmt, 4);
        int enabled = sqlite3_column_int(stmt, 5);
        const char* componentsStr = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        
        ComponentBundle bundle;
        bundle.id = idStr ? idStr : "";
        bundle.name = nameStr ? nameStr : "";
        bundle.description = descStr ? descStr : "";
        bundle.coreType = static_cast<CoreType>(coreType);
        
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

bool ComponentBundleDAO::save(const ComponentBundle& bundle) const {
    std::string sql = "INSERT OR REPLACE INTO ComponentBundle (Id, Name, Description, CoreType, Version, Enabled, Components, CreatedAt, UpdatedAt) VALUES (?, ?, ?, ?, ?, ?, ?, datetime('now'), datetime('now'))";
    sqlite3_stmt* stmt = nullptr;
    
    if (!prepareStatement(&stmt, sql)) {
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, bundle.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bundle.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, bundle.description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(bundle.coreType));
    sqlite3_bind_int(stmt, 5, bundle.version);
    sqlite3_bind_int(stmt, 6, bundle.enabled ? 1 : 0);
    sqlite3_bind_text(stmt, 7, serializeBundle(bundle).c_str(), -1, SQLITE_TRANSIENT);
    
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

} // namespace config
