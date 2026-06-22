#ifndef CONFIG_SECTIONS_NETWORKMONITOR_H
#define CONFIG_SECTIONS_NETWORKMONITOR_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class NetworkMonitorConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void NetworkMonitorConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("network_monitor") && obj.at("network_monitor").is_object()) {
        const boost::json::object& nm = obj.at("network_monitor").as_object();
        if (nm.contains("enabled") && nm.at("enabled").is_bool()) {
            config.network_monitor.enabled = nm.at("enabled").as_bool();
        } else if (nm.contains("enabled")) {
            Logger::write("WARNING: config.network_monitor.enabled has wrong type (expected bool), using default", LogLevel::WARN);
        }
        if (nm.contains("check_urls") && nm.at("check_urls").is_array()) {
            config.network_monitor.checkUrls.clear();
            for (const boost::json::value& u : nm.at("check_urls").as_array()) {
                if (u.is_string()) {
                    config.network_monitor.checkUrls.push_back(u.as_string().c_str());
                } else {
                    Logger::write("WARNING: config.network_monitor.check_urls element has wrong type (expected string), skipping", LogLevel::WARN);
                }
            }
        } else if (nm.contains("check_urls")) {
            Logger::write("WARNING: config.network_monitor.check_urls has wrong type (expected array), using default", LogLevel::WARN);
        }
        if (nm.contains("check_interval_ms") && nm.at("check_interval_ms").is_int64()) {
            config.network_monitor.checkIntervalMs = static_cast<int>(nm.at("check_interval_ms").as_int64());
            if (config.network_monitor.checkIntervalMs <= 0) config.network_monitor.checkIntervalMs = 10000;
        } else if (nm.contains("check_interval_ms")) {
            Logger::write("WARNING: config.network_monitor.check_interval_ms has wrong type (expected int64), using default", LogLevel::WARN);
        }
        if (nm.contains("check_timeout_ms") && nm.at("check_timeout_ms").is_int64()) {
            config.network_monitor.checkTimeoutMs = static_cast<int>(nm.at("check_timeout_ms").as_int64());
            if (config.network_monitor.checkTimeoutMs <= 0) config.network_monitor.checkTimeoutMs = 5000;
        } else if (nm.contains("check_timeout_ms")) {
            Logger::write("WARNING: config.network_monitor.check_timeout_ms has wrong type (expected int64), using default", LogLevel::WARN);
        }
    } else if (obj.contains("network_monitor")) {
        Logger::write("WARNING: config.network_monitor has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
