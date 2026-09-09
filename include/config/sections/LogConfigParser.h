#ifndef CONFIG_SECTIONS_LOG_H
#define CONFIG_SECTIONS_LOG_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class LogConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void LogConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("log") && obj.at("log").is_object()) {
        const boost::json::object& log = obj.at("log").as_object();
        if (log.contains("enabled") && log.at("enabled").is_bool()) {
            config.log_enabled = log.at("enabled").as_bool();
        } else if (log.contains("enabled")) {
            Logger::write("WARNING: config.log.enabled has wrong type (expected bool), using default", LogLevel::WARN);
            config.log_enabled = true;
        } else { config.log_enabled = true; }
        // network_failures key removed (Spec 附录A): unknown keys are silently
        // ignored; the stale-key regression fixture lives in test_config_reader_load.cpp full.json.
        if (log.contains("console_level") && log.at("console_level").is_string()) {
            config.log_console_level = log.at("console_level").as_string().c_str();
        } else if (log.contains("console_level")) {
            Logger::write("WARNING: config.log.console_level has wrong type (expected string), using default", LogLevel::WARN);
            config.log_console_level = "INFO";
        } else { config.log_console_level = "INFO"; }
        if (log.contains("file_level") && log.at("file_level").is_string()) {
            config.log_file_level = log.at("file_level").as_string().c_str();
        } else if (log.contains("file_level")) {
            Logger::write("WARNING: config.log.file_level has wrong type (expected string), using default", LogLevel::WARN);
            config.log_file_level = "DEBUG";
        } else { config.log_file_level = "DEBUG"; }
    } else if (obj.contains("log")) {
        Logger::write("WARNING: config.log has wrong type (expected object), using default", LogLevel::WARN);
    } else {
        config.log_enabled = true;
        config.log_console_level = "INFO";
        config.log_file_level = "DEBUG";
    }
}

} // namespace config
#endif
