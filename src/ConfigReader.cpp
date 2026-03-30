#include <fstream>
#include <sstream>
#include <boost/json.hpp>

#include "ConfigReader.h"

namespace config {

std::string ConfigReader::getDefaultConfigPath() {
    return "config.json";
}

std::optional<AppConfig> ConfigReader::load(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    try {
        boost::json::value jv = boost::json::parse(content);
        
        AppConfig config;
        
        if (jv.is_object()) {
            auto& obj = jv.as_object();
            
            if (obj.contains("database") && obj["database"].is_object()) {
                auto& db = obj["database"].as_object();
                if (db.contains("path")) {
                    config.database_path = db["path"].as_string().c_str();
                }
                if (db.contains("sql")) {
                    config.sql_query = db["sql"].as_string().c_str();
                }
            }
            
            if (obj.contains("xray") && obj["xray"].is_object()) {
                auto& xray = obj["xray"].as_object();
                if (xray.contains("executable")) {
                    config.xray_executable = xray["executable"].as_string().c_str();
                }
                if (xray.contains("config")) {
                    config.xray_config = xray["config"].as_string().c_str();
                }
                if (xray.contains("api_port")) {
                    config.xray_api_port = static_cast<int>(xray["api_port"].as_int64());
                }
                if (xray.contains("socks_port")) {
                    config.xray_socks_port = static_cast<int>(xray["socks_port"].as_int64());
                }
            }
            
            if (obj.contains("test") && obj["test"].is_object()) {
                auto& test = obj["test"].as_object();
                if (test.contains("url")) {
                    config.test_url = test["url"].as_string().c_str();
                }
                if (test.contains("timeout_ms")) {
                    config.test_timeout_ms = static_cast<int>(test["timeout_ms"].as_int64());
                }
            }
        }
        
        return config;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace config
