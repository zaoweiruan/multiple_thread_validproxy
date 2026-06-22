#ifndef CONFIG_SECTIONS_DATABASE_H
#define CONFIG_SECTIONS_DATABASE_H

#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class DatabaseConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void DatabaseConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("database")) {
        if (obj.at("database").is_string()) {
            std::string rawPath = obj.at("database").as_string().c_str();
            if (!rawPath.empty()) {
                std::filesystem::path p(rawPath);
                if (!p.is_absolute()) {
                    p = std::filesystem::path(exeDir) / p;
                }
                config.database_path = p.string();
            }
        } else if (obj.at("database").is_object()) {
            const boost::json::object& db = obj.at("database").as_object();
            if (db.contains("path") && db.at("path").is_string()) {
                std::string rawPath = db.at("path").as_string().c_str();
                std::filesystem::path p(rawPath);
                if (!p.is_absolute()) {
                    p = std::filesystem::path(exeDir) / p;
                }
                config.database_path = p.string();
            } else if (db.contains("path")) {
                Logger::write("WARNING: config.database.path has wrong type (expected string), using default", LogLevel::WARN);
            }
            if (db.contains("sql") && db.at("sql").is_string()) {
                config.sql_query = db.at("sql").as_string().c_str();
            } else if (db.contains("sql")) {
                Logger::write("WARNING: config.database.sql has wrong type (expected string), using default", LogLevel::WARN);
            }
            if (db.contains("sql_by_subid") && db.at("sql_by_subid").is_string()) {
                config.sql_by_subid = db.at("sql_by_subid").as_string().c_str();
            } else if (db.contains("sql_by_subid")) {
                Logger::write("WARNING: config.database.sql_by_subid has wrong type (expected string), using default", LogLevel::WARN);
            }
        } else {
            Logger::write("WARNING: config.database has wrong type (expected string|object), using default", LogLevel::WARN);
        }
    }
}

} // namespace config
#endif
