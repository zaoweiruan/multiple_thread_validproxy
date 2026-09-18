#ifndef CONFIG_SECTIONS_SUBSCRIPTION_H
#define CONFIG_SECTIONS_SUBSCRIPTION_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class SubscriptionConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void SubscriptionConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("subscription") && obj.at("subscription").is_object()) {
        const boost::json::object& sub = obj.at("subscription").as_object();
        if (sub.contains("accelerator_url") && sub.at("accelerator_url").is_string()) {
            config.accelerator_url = sub.at("accelerator_url").as_string().c_str();
        } else if (sub.contains("accelerator_url")) {
            Logger::write("WARNING: config.subscription.accelerator_url has wrong type (expected string), using default", LogLevel::WARN);
        }

        if (sub.contains("update_methods") && sub.at("update_methods").is_array()) {
            for (const boost::json::value& m : sub.at("update_methods").as_array()) {
                if (m.is_string()) {
                    std::string val = m.as_string().c_str();
                    if (val == "accelerator" || val == "proxy" || val == "direct") {
                        config.update_methods.push_back(val);
                    }
                }
            }
        } else if (sub.contains("update_methods")) {
            Logger::write("WARNING: config.subscription.update_methods has wrong type (expected array), using default", LogLevel::WARN);
        }

        if (sub.contains("priority_mode") && sub.at("priority_mode").is_string()
            && !sub.contains("update_methods")) {
            std::string pm = sub.at("priority_mode").as_string().c_str();
            if (pm == "direct_first") {
                config.update_methods = std::vector<std::string>{"direct", "proxy"};
            } else if (pm == "proxy_first") {
                config.update_methods = std::vector<std::string>{"proxy"};
            } else if (pm == "direct_only") {
                config.update_methods = std::vector<std::string>{"direct"};
            }
        }

        if (config.update_methods.empty()) {
            config.update_methods = std::vector<std::string>{"accelerator"};
        }
        if (sub.contains("check_auto_update_interval") && sub.at("check_auto_update_interval").is_bool()) {
            config.check_auto_update_interval = sub.at("check_auto_update_interval").as_bool();
        } else if (sub.contains("check_auto_update_interval")) {
            Logger::write("WARNING: config.subscription.check_auto_update_interval has wrong type (expected bool), using default", LogLevel::WARN);
            config.check_auto_update_interval = false;
        } else {
            config.check_auto_update_interval = false;
        }
        if (sub.contains("connect_timeout_ms") && sub.at("connect_timeout_ms").is_int64()) {
            config.subscription_connect_timeout_ms = static_cast<int>(sub.at("connect_timeout_ms").as_int64());
            if (config.subscription_connect_timeout_ms <= 0) config.subscription_connect_timeout_ms = 10000;
        } else if (sub.contains("connect_timeout_ms")) {
            Logger::write("WARNING: config.subscription.connect_timeout_ms has wrong type (expected int64), using default", LogLevel::WARN);
            config.subscription_connect_timeout_ms = 10000;
        } else {
            config.subscription_connect_timeout_ms = 10000;
        }
        if (sub.contains("timeout_ms") && sub.at("timeout_ms").is_int64()) {
            config.subscription_timeout_ms = static_cast<int>(sub.at("timeout_ms").as_int64());
            if (config.subscription_timeout_ms <= 0) config.subscription_timeout_ms = 30000;
        } else if (sub.contains("timeout_ms")) {
            Logger::write("WARNING: config.subscription.timeout_ms has wrong type (expected int64), using default", LogLevel::WARN);
            config.subscription_timeout_ms = 30000;
        } else {
            config.subscription_timeout_ms = 30000;
        }

        // priority_subids: comma-separated list of subscription IDs to show at top
        if (sub.contains("priority_subids") && sub.at("priority_subids").is_string()) {
            std::string raw = sub.at("priority_subids").as_string().c_str();
            // Split by comma, trim whitespace, skip empty tokens
            std::string token;
            for (char ch : raw) {
                if (ch == ',') {
                    if (!token.empty()) {
                        config.priority_subids.push_back(token);
                    }
                    token.clear();
                } else if (ch != ' ' && ch != '\t') {
                    token += ch;
                }
            }
            if (!token.empty()) {
                config.priority_subids.push_back(token);
            }
        } else if (sub.contains("priority_subids")) {
            Logger::write("WARNING: config.subscription.priority_subids has wrong type (expected string), using default", LogLevel::WARN);
        }
    } else if (obj.contains("subscription")) {
        Logger::write("WARNING: config.subscription has wrong type (expected object), using default", LogLevel::WARN);
    } else {
        config.check_auto_update_interval = false;
        config.subscription_connect_timeout_ms = 10000;
        config.subscription_timeout_ms = 30000;
        config.update_methods = std::vector<std::string>{"accelerator"};
    }
}

} // namespace config
#endif
