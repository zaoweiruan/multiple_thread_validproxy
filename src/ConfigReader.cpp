#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <windows.h>
#include <boost/json.hpp>

#include "ConfigReader.h"
#include "Utils.h"

namespace config {

static std::string resolvePath(const std::string& path, const std::string& exeDir) {
    if (path.empty()) return "";
    
    std::filesystem::path p(path);
    if (p.is_absolute()) {
        return path;
    }
    
    return (std::filesystem::path(exeDir) / p).string();
}

std::string ConfigReader::getDefaultConfigPath() {
    std::string exeDir = utils::getExecutableDir();
    return exeDir + "\\config.json";
}

std::optional<AppConfig> ConfigReader::load(const std::string& configPath) {
    std::cerr << "[DEBUG] Loading config from: " << configPath << std::endl;
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[DEBUG] Failed to open file" << std::endl;
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    std::cerr << "[DEBUG] File read successfully, content length: " << content.length() << std::endl;

    boost::json::value jv;
    try {
        std::cerr << "[DEBUG] About to parse JSON..." << std::endl;
        jv = boost::json::parse(content);
        std::cerr << "[DEBUG] JSON parsed successfully, is_object: " << jv.is_object() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[DEBUG] JSON exception: " << e.what() << std::endl;
        return std::nullopt;
    }
        
    if (!jv.is_object()) {
        std::cerr << "[DEBUG] JSON is not an object" << std::endl;
        return std::nullopt;
    }
    
    std::string exeDir = utils::getExecutableDir();
    
    AppConfig config;
    
    auto& obj = jv.as_object();
    std::cerr << "[DEBUG] JSON object has " << obj.size() << " keys" << std::endl;
    
    if (obj.contains("database")) {
        if (obj["database"].is_string()) {
            config.database_path = resolvePath(obj["database"].as_string().c_str(), exeDir);
        } else if (obj["database"].is_object()) {
            auto& db = obj["database"].as_object();
            if (db.contains("path")) {
                config.database_path = resolvePath(db["path"].as_string().c_str(), exeDir);
            }
            if (db.contains("sql")) {
                config.sql_query = db["sql"].as_string().c_str();
            }
            if (db.contains("sql_by_subid")) {
                config.sql_by_subid = db["sql_by_subid"].as_string().c_str();
            }
        }
    }
    
    // Replace placeholder {blacklist_threshold} in SQL queries
    auto replacePlaceholder = [&](std::string& sql, const std::string& placeholder, int value) {
        size_t pos = sql.find(placeholder);
        if (pos != std::string::npos) {
            sql.replace(pos, placeholder.length(), std::to_string(value));
        }
    };
    
    replacePlaceholder(config.sql_query, "{blacklist_threshold}", config.blacklist_threshold);
    replacePlaceholder(config.sql_by_subid, "{blacklist_threshold}", config.blacklist_threshold);
    
    if (!config.database_path.empty()) {
        std::filesystem::path dbPath(config.database_path);
        if (!std::filesystem::exists(dbPath)) {
            std::cerr << "[ERROR] Database file not found: " << config.database_path << std::endl;
            return std::nullopt;
        }
    }
    
    std::cerr << "[DEBUG] Config loaded successfully" << std::endl;
    return config;
}

} // namespace config
