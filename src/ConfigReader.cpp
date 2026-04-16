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
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string exePath = buffer;
    size_t pos = exePath.rfind('\\');
    if (pos != std::string::npos) {
        exePath = exePath.substr(0, pos);
    }
    return exePath + "\\config.json";
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
    
    if (obj.contains("xray") && obj["xray"].is_object()) {
        auto& xray = obj["xray"].as_object();
        if (xray.contains("executable")) {
            config.xray_executable = resolvePath(xray["executable"].as_string().c_str(), exeDir);
        }
        if (xray.contains("workers")) {
            config.xray_workers = static_cast<int>(xray["workers"].as_int64());
        } else {
            config.xray_workers = 1;
        }
        if (xray.contains("start_port")) {
            config.xray_start_port = static_cast<int>(xray["start_port"].as_int64());
        } else {
            config.xray_start_port = 1083;
        }
        if (xray.contains("api_port")) {
            config.xray_api_port = static_cast<int>(xray["api_port"].as_int64());
        }
    }
    
    if (obj.contains("test") && obj["test"].is_object()) {
        auto& test = obj["test"].as_object();
        if (test.contains("url")) {
            config.test_url = test["url"].as_string().c_str();
        }
        if (test.contains("timeout_ms")) {
            config.test_timeout_ms = static_cast<int>(test["timeout_ms"].as_int64());
        } else {
            config.test_timeout_ms = 5000;
        }
    }
    
    if (obj.contains("log") && obj["log"].is_object()) {
        auto& log = obj["log"].as_object();
        if (log.contains("enabled")) {
            config.log_enabled = log["enabled"].as_bool();
        } else {
            config.log_enabled = true;
        }
        if (log.contains("network_failures")) {
            config.log_network_failures = log["network_failures"].as_bool();
        } else {
            config.log_network_failures = false;
        }
    } else {
        config.log_enabled = true;
        config.log_network_failures = false;
    }

    if (obj.contains("subscription") && obj["subscription"].is_object()) {
        auto& sub = obj["subscription"].as_object();
        if (sub.contains("priority_mode")) {
            config.priority_mode = sub["priority_mode"].as_string().c_str();
        } else {
            config.priority_mode = "direct_first";
        }
    } else {
        config.priority_mode = "direct_first";
    }
    
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
