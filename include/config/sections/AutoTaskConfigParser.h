#ifndef CONFIG_SECTIONS_AUTOTASK_H
#define CONFIG_SECTIONS_AUTOTASK_H

#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class AutoTaskConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void AutoTaskConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("auto_task") && obj.at("auto_task").is_object()) {
        const boost::json::object& at = obj.at("auto_task").as_object();
        if (at.contains("steps") && at.at("steps").is_array()) {
            for (const boost::json::value& s : at.at("steps").as_array()) {
                if (s.is_string()) {
                    config.auto_task.steps.push_back(s.as_string().c_str());
                }
            }
        } else if (at.contains("steps")) {
            Logger::write("WARNING: config.auto_task.steps has wrong type (expected array), using default", LogLevel::WARN);
        }
        if (at.contains("notify_on_complete") && at.at("notify_on_complete").is_bool()) {
            config.auto_task.notify_on_complete = at.at("notify_on_complete").as_bool();
        } else if (at.contains("notify_on_complete")) {
            Logger::write("WARNING: config.auto_task.notify_on_complete has wrong type (expected bool), using default", LogLevel::WARN);
        }
        if (at.contains("state_file") && at.at("state_file").is_string()) {
            std::string rawPath = at.at("state_file").as_string().c_str();
            std::filesystem::path p(rawPath);
            if (!p.is_absolute()) {
                p = std::filesystem::path(exeDir) / p;
            }
            config.auto_task.state_file = p.string();
        } else if (at.contains("state_file")) {
            Logger::write("WARNING: config.auto_task.state_file has wrong type (expected string), using default", LogLevel::WARN);
        }
    } else if (obj.contains("auto_task")) {
        Logger::write("WARNING: config.auto_task has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
