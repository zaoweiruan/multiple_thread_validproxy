#ifndef CONFIG_SECTIONS_TEST_H
#define CONFIG_SECTIONS_TEST_H

#include <boost/json.hpp>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class TestConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void TestConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    (void)exeDir;
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("test") && obj.at("test").is_object()) {
        const boost::json::object& test = obj.at("test").as_object();
        if (test.contains("url") && test.at("url").is_string()) {
            config.test_url = test.at("url").as_string().c_str();
        } else if (test.contains("url")) {
            Logger::write("WARNING: config.test.url has wrong type (expected string), using default", LogLevel::WARN);
        }
        if (test.contains("timeout_ms") && test.at("timeout_ms").is_int64()) {
            config.test_timeout_ms = static_cast<int>(test.at("timeout_ms").as_int64());
            if (config.test_timeout_ms <= 0) config.test_timeout_ms = 5000;
        } else if (test.contains("timeout_ms")) {
            Logger::write("WARNING: config.test.timeout_ms has wrong type (expected int64), using default", LogLevel::WARN);
        } else {
            config.test_timeout_ms = 5000;
        }
        // ipinfo_token removed: region resolution migrated to ipwho.is (token-free);
        // legacy config.test.ipinfo_token keys in existing config.json are silently ignored.
    } else if (obj.contains("test")) {
        Logger::write("WARNING: config.test has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
