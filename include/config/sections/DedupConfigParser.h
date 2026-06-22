#ifndef CONFIG_SECTIONS_DEDUP_H
#define CONFIG_SECTIONS_DEDUP_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class DedupConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void DedupConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("dedup") && obj.at("dedup").is_object()) {
        const boost::json::object& dedup = obj.at("dedup").as_object();
        if (dedup.contains("enabled") && dedup.at("enabled").is_bool()) {
            config.dedup_enabled = dedup.at("enabled").as_bool();
        } else if (dedup.contains("enabled")) {
            Logger::write("WARNING: config.dedup.enabled has wrong type (expected bool), using default", LogLevel::WARN);
            config.dedup_enabled = false;
        } else {
            config.dedup_enabled = false;
        }
        if (dedup.contains("dedup_after_update") && dedup.at("dedup_after_update").is_bool()) {
            config.dedup_after_update = dedup.at("dedup_after_update").as_bool();
        } else if (dedup.contains("dedup_after_update")) {
            Logger::write("WARNING: config.dedup.dedup_after_update has wrong type (expected bool), using default", LogLevel::WARN);
            config.dedup_after_update = false;
        } else {
            config.dedup_after_update = false;
        }
        if (dedup.contains("blacklist_threshold") && dedup.at("blacklist_threshold").is_int64()) {
            config.blacklist_threshold = static_cast<int>(dedup.at("blacklist_threshold").as_int64());
            if (config.blacklist_threshold < 0) config.blacklist_threshold = 5;
        } else if (dedup.contains("blacklist_threshold")) {
            Logger::write("WARNING: config.dedup.blacklist_threshold has wrong type (expected int64), using default", LogLevel::WARN);
            config.blacklist_threshold = 5;
        } else {
            config.blacklist_threshold = 5;
        }
        if (dedup.contains("blacklist_enabled") && dedup.at("blacklist_enabled").is_bool()) {
            config.blacklist_enabled = dedup.at("blacklist_enabled").as_bool();
        } else if (dedup.contains("blacklist_enabled")) {
            Logger::write("WARNING: config.dedup.blacklist_enabled has wrong type (expected bool), using default", LogLevel::WARN);
            config.blacklist_enabled = true;
        } else {
            config.blacklist_enabled = true;
        }
        if (dedup.contains("blacklist_subid") && dedup.at("blacklist_subid").is_string()) {
            config.blacklist_subid = dedup.at("blacklist_subid").as_string().c_str();
        } else if (dedup.contains("blacklist_subid")) {
            Logger::write("WARNING: config.dedup.blacklist_subid has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (dedup.contains("subids") && dedup.at("subids").is_array()) {
            for (const boost::json::value& sid : dedup.at("subids").as_array()) {
                if (sid.is_string()) {
                    config.dedup_subids.push_back(sid.as_string().c_str());
                } else {
                    Logger::write("WARNING: config.dedup.subids element is not a string, skipping", LogLevel::WARN);
                }
            }
        }
    } else if (obj.contains("dedup")) {
        Logger::write("WARNING: config.dedup has wrong type (expected object), using default", LogLevel::WARN);
    } else {
        config.dedup_enabled = true;
        config.dedup_after_update = false;
        config.blacklist_threshold = 5;
        config.blacklist_enabled = true;
        config.blacklist_subid = "";
    }
}

} // namespace config
#endif
