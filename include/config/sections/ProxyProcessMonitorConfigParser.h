#ifndef CONFIG_SECTIONS_PROXY_PROCESS_MONITOR_H
#define CONFIG_SECTIONS_PROXY_PROCESS_MONITOR_H

#include <boost/json.hpp>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class ProxyProcessMonitorConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config);
};

inline void ProxyProcessMonitorConfigParser::parse(const boost::json::value& root, AppConfig& config) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("proxy_process_monitor") && obj.at("proxy_process_monitor").is_object()) {
        const boost::json::object& pm = obj.at("proxy_process_monitor").as_object();

        if (pm.contains("enabled") && pm.at("enabled").is_bool()) {
            config.proxy_process_monitor.enabled = pm.at("enabled").as_bool();
        } else if (pm.contains("enabled")) {
            Logger::write("WARNING: config.proxy_process_monitor.enabled has wrong type (expected bool), using default", LogLevel::WARN);
        }

        if (pm.contains("check_interval_ms") && pm.at("check_interval_ms").is_int64()) {
            config.proxy_process_monitor.checkIntervalMs = static_cast<int>(pm.at("check_interval_ms").as_int64());
            if (config.proxy_process_monitor.checkIntervalMs < 5000) {
                config.proxy_process_monitor.checkIntervalMs = 5000;
            }
            if (config.proxy_process_monitor.checkIntervalMs > 300000) {
                config.proxy_process_monitor.checkIntervalMs = 300000;
            }
        } else if (pm.contains("check_interval_ms")) {
            Logger::write("WARNING: config.proxy_process_monitor.check_interval_ms has wrong type (expected int64), using default", LogLevel::WARN);
        }
    } else if (obj.contains("proxy_process_monitor")) {
        Logger::write("WARNING: config.proxy_process_monitor has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
