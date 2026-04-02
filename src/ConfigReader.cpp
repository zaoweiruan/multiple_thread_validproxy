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
            
            if (obj.contains("database")) {
                config.database_path = obj["database"].as_string().c_str();
            }
            if (obj.contains("sql")) {
                config.sql_query = obj["sql"].as_string().c_str();
            }
            
            if (obj.contains("xray") && obj["xray"].is_object()) {
                auto& xray = obj["xray"].as_object();
                if (xray.contains("executable")) {
                    config.xray_executable = xray["executable"].as_string().c_str();
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
            } else {
                config.log_enabled = true;
            }
        }
        
        return config;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace config
