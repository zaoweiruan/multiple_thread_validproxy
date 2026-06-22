#ifndef CONFIG_SECTIONS_SYNC_H
#define CONFIG_SECTIONS_SYNC_H

#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class SyncConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void SyncConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("sync") && obj.at("sync").is_object()) {
        const boost::json::object& sync = obj.at("sync").as_object();
        if (sync.contains("source_db") && sync.at("source_db").is_string()) {
            std::string rawPath = sync.at("source_db").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) {
                p = std::filesystem::path(exeDir) / p;
            }
            config.sync.source_db = p.string();
        } else if (sync.contains("source_db")) {
            Logger::write("WARNING: config.sync.source_db has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (sync.contains("target_db") && sync.at("target_db").is_string()) {
            std::string rawPath = sync.at("target_db").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) {
                p = std::filesystem::path(exeDir) / p;
            }
            config.sync.target_db = p.string();
        } else if (sync.contains("target_db")) {
            Logger::write("WARNING: config.sync.target_db has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (sync.contains("sync_skip_subids") && sync.at("sync_skip_subids").is_bool()) {
            config.sync.sync_skip_subids = sync.at("sync_skip_subids").as_bool();
        } else if (sync.contains("sync_skip_subids")) {
            Logger::write("WARNING: config.sync.sync_skip_subids has wrong type (expected bool), using default", LogLevel::WARN);
        }
    } else if (obj.contains("sync")) {
        Logger::write("WARNING: config.sync has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
