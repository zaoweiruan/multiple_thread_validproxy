#ifndef CONFIG_SECTIONS_NOTIFICATION_H
#define CONFIG_SECTIONS_NOTIFICATION_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class NotificationConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void NotificationConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("notification") && obj.at("notification").is_object()) {
        const boost::json::object& notification = obj.at("notification").as_object();
        if (notification.contains("enabled") && notification.at("enabled").is_bool()) {
            config.notification_enabled = notification.at("enabled").as_bool();
        } else if (notification.contains("enabled")) {
            Logger::write("WARNING: config.notification.enabled has wrong type (expected bool), using default", LogLevel::WARN);
            config.notification_enabled = false;
        } else {
            config.notification_enabled = false;
        }
        if (notification.contains("on_update") && notification.at("on_update").is_bool()) {
            config.notification_on_update = notification.at("on_update").as_bool();
        } else if (notification.contains("on_update")) {
            Logger::write("WARNING: config.notification.on_update has wrong type (expected bool), using default", LogLevel::WARN);
            config.notification_on_update = false;
        } else {
            config.notification_on_update = false;
        }
        if (notification.contains("on_test") && notification.at("on_test").is_bool()) {
            config.notification_on_test = notification.at("on_test").as_bool();
        } else if (notification.contains("on_test")) {
            Logger::write("WARNING: config.notification.on_test has wrong type (expected bool), using default", LogLevel::WARN);
            config.notification_on_test = false;
        } else {
            config.notification_on_test = false;
        }
    } else if (obj.contains("notification")) {
        Logger::write("WARNING: config.notification has wrong type (expected object), using default", LogLevel::WARN);
    } else {
        config.notification_enabled = false;
        config.notification_on_update = false;
        config.notification_on_test = false;
    }
}

} // namespace config
#endif
