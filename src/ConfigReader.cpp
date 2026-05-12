#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <windows.h>
#include <boost/json.hpp>

#include "Logger.h"
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
    Logger::write("DEBUG: Loading config from: " + configPath, LogLevel::DEBUG);
    std::ifstream file(configPath);
    if (!file.is_open()) {
        Logger::write("DEBUG: Failed to open file", LogLevel::DEBUG);
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    Logger::write("DEBUG: File read successfully, content length: " + std::to_string(content.length()), LogLevel::DEBUG);

    boost::json::value jv;
    try {
        Logger::write("DEBUG: About to parse JSON...", LogLevel::DEBUG);
        jv = boost::json::parse(content);
        Logger::write("DEBUG: JSON parsed successfully, is_object: " + std::to_string(jv.is_object()), LogLevel::DEBUG);
    } catch (const std::exception& e) {
        Logger::write("DEBUG: JSON exception: " + std::string(e.what()), LogLevel::DEBUG);
        return std::nullopt;
    }

    if (!jv.is_object()) {
        Logger::write("DEBUG: JSON is not an object", LogLevel::DEBUG);
        return std::nullopt;
    }

    std::string exeDir = utils::getExecutableDir();
    
    AppConfig config;
    
    auto& obj = jv.as_object();
    Logger::write("DEBUG: JSON object has " + std::to_string(obj.size()) + " keys", LogLevel::DEBUG);
    
    if (obj.contains("database")) {
        if (obj["database"].is_string()) {
            config.database_path = resolvePath(obj["database"].as_string().c_str(), exeDir);
        } else if (obj["database"].is_object()) {
            auto& db = obj["database"].as_object();
            if (db.contains("path") && db["path"].is_string()) {
                config.database_path = resolvePath(db["path"].as_string().c_str(), exeDir);
            }
            if (db.contains("sql") && db["sql"].is_string()) {
                config.sql_query = db["sql"].as_string().c_str();
            }
            if (db.contains("sql_by_subid") && db["sql_by_subid"].is_string()) {
                config.sql_by_subid = db["sql_by_subid"].as_string().c_str();
            }
        }
    }
    
    if (obj.contains("xray") && obj["xray"].is_object()) {
        auto& xray = obj["xray"].as_object();
        if (xray.contains("executable") && xray["executable"].is_string()) {
            config.xray_executable = resolvePath(xray["executable"].as_string().c_str(), exeDir);
        }
        if (xray.contains("workers") && xray["workers"].is_int64()) {
            config.xray_workers = static_cast<int>(xray["workers"].as_int64());
            if (config.xray_workers <= 0) config.xray_workers = 1;
        } else {
            config.xray_workers = 1;
        }
        if (xray.contains("start_port") && xray["start_port"].is_int64()) {
            config.xray_start_port = static_cast<int>(xray["start_port"].as_int64());
            if (config.xray_start_port <= 0) config.xray_start_port = 1083;
        } else {
            config.xray_start_port = 1083;
        }
        if (xray.contains("api_port") && xray["api_port"].is_int64()) {
            config.xray_api_port = static_cast<int>(xray["api_port"].as_int64());
        }
    }
    
    if (obj.contains("test") && obj["test"].is_object()) {
        auto& test = obj["test"].as_object();
        if (test.contains("url") && test["url"].is_string()) {
            config.test_url = test["url"].as_string().c_str();
        }
        if (test.contains("timeout_ms") && test["timeout_ms"].is_int64()) {
            config.test_timeout_ms = static_cast<int>(test["timeout_ms"].as_int64());
            if (config.test_timeout_ms <= 0) config.test_timeout_ms = 5000;
        } else {
            config.test_timeout_ms = 5000;
        }
    }
    
    if (obj.contains("log") && obj["log"].is_object()) {
        auto& log = obj["log"].as_object();
        if (log.contains("enabled") && log["enabled"].is_bool()) {
            config.log_enabled = log["enabled"].as_bool();
        } else {
            config.log_enabled = true;
        }
        if (log.contains("network_failures") && log["network_failures"].is_bool()) {
            config.log_network_failures = log["network_failures"].as_bool();
        } else {
            config.log_network_failures = false;
        }
        if (log.contains("console_level") && log["console_level"].is_string()) {
            config.log_console_level = log["console_level"].as_string().c_str();
        } else {
            config.log_console_level = "INFO";
        }
        if (log.contains("file_level") && log["file_level"].is_string()) {
            config.log_file_level = log["file_level"].as_string().c_str();
        } else {
            config.log_file_level = "DEBUG";
        }
    } else {
        config.log_enabled = true;
        config.log_network_failures = false;
        config.log_console_level = "INFO";
        config.log_file_level = "DEBUG";
    }
    
    if (obj.contains("subscription") && obj["subscription"].is_object()) {
        auto& sub = obj["subscription"].as_object();
        if (sub.contains("priority_mode") && sub["priority_mode"].is_string()) {
            config.priority_mode = sub["priority_mode"].as_string().c_str();
        } else {
            config.priority_mode = "direct_first";
        }
        if (sub.contains("check_auto_update_interval") && sub["check_auto_update_interval"].is_bool()) {
            config.check_auto_update_interval = sub["check_auto_update_interval"].as_bool();
        } else {
            config.check_auto_update_interval = false;
        }
    } else {
        config.priority_mode = "direct_first";
        config.check_auto_update_interval = false;
    }
    
    if (obj.contains("dedup") && obj["dedup"].is_object()) {
        auto& dedup = obj["dedup"].as_object();
        if (dedup.contains("enabled") && dedup["enabled"].is_bool()) {
            config.dedup_enabled = dedup["enabled"].as_bool();
        } else {
            config.dedup_enabled = false;
        }
        if (dedup.contains("dedup_after_update") && dedup["dedup_after_update"].is_bool()) {
            config.dedup_after_update = dedup["dedup_after_update"].as_bool();
        } else {
            config.dedup_after_update = false;
        }
        if (dedup.contains("blacklist_threshold") && dedup["blacklist_threshold"].is_int64()) {
            config.blacklist_threshold = static_cast<int>(dedup["blacklist_threshold"].as_int64());
            if (config.blacklist_threshold < 0) config.blacklist_threshold = 5;
        } else {
            config.blacklist_threshold = 5;
        }
        if (dedup.contains("subids") && dedup["subids"].is_array()) {
            for (const auto& sid : dedup["subids"].as_array()) {
                if (sid.is_string()) {
                    config.dedup_subids.push_back(sid.as_string().c_str());
                }
            }
        }
    } else {
        config.dedup_enabled = false;
        config.dedup_after_update = false;
        config.blacklist_threshold = 5;
    }
    
    if (obj.contains("notification") && obj["notification"].is_object()) {
        auto& notification = obj["notification"].as_object();
        if (notification.contains("enabled") && notification["enabled"].is_bool()) {
            config.notification_enabled = notification["enabled"].as_bool();
        } else {
            config.notification_enabled = false;
        }
        if (notification.contains("on_update") && notification["on_update"].is_bool()) {
            config.notification_on_update = notification["on_update"].as_bool();
        } else {
            config.notification_on_update = false;
        }
        if (notification.contains("on_test") && notification["on_test"].is_bool()) {
            config.notification_on_test = notification["on_test"].as_bool();
        } else {
            config.notification_on_test = false;
        }
    } else {
        config.notification_enabled = false;
        config.notification_on_update = false;
        config.notification_on_test = false;
    }
    
    if (obj.contains("sync") && obj["sync"].is_object()) {
        auto& sync = obj["sync"].as_object();
        if (sync.contains("source_db") && sync["source_db"].is_string()) {
            config.sync.source_db = resolvePath(sync["source_db"].as_string().c_str(), exeDir);
        }
        if (sync.contains("target_db") && sync["target_db"].is_string()) {
            config.sync.target_db = resolvePath(sync["target_db"].as_string().c_str(), exeDir);
        }
        if (sync.contains("sync_skip_subids") && sync["sync_skip_subids"].is_bool()) {
            config.sync.sync_skip_subids = sync["sync_skip_subids"].as_bool();
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
    
    if (!config.sql_query.empty())
        Logger::write("SQL query: " + config.sql_query, LogLevel::DEBUG);
    if (!config.sql_by_subid.empty())
        Logger::write("SQL by_subid: " + config.sql_by_subid, LogLevel::DEBUG);
    
    if (!config.database_path.empty()) {
        std::filesystem::path dbPath(config.database_path);
        if (!std::filesystem::exists(dbPath)) {
            Logger::write("ERROR: Database file not found: " + config.database_path, LogLevel::ERR);
            return std::nullopt;
        }
    }
    
    Logger::write("DEBUG: Config loaded successfully", LogLevel::DEBUG);
    return config;
}

} // namespace config
