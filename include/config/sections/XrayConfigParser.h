#ifndef CONFIG_SECTIONS_XRAY_H
#define CONFIG_SECTIONS_XRAY_H

#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class XrayConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir);
};

inline void XrayConfigParser::parse(const boost::json::value& root, AppConfig& config, const std::string& exeDir) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();
    if (obj.contains("xray") && obj.at("xray").is_object()) {
        const boost::json::object& xray = obj.at("xray").as_object();
        if (xray.contains("workers") && xray.at("workers").is_int64()) {
            config.xray_workers = static_cast<int>(xray.at("workers").as_int64());
            if (config.xray_workers <= 0) config.xray_workers = 1;
        } else if (xray.contains("workers")) {
            Logger::write("WARNING: config.xray.workers has wrong type (expected int64), using default", LogLevel::WARN);
        } else {
            config.xray_workers = 1;
        }
        if (xray.contains("start_port") && xray.at("start_port").is_int64()) {
            config.xray_start_port = static_cast<int>(xray.at("start_port").as_int64());
            if (config.xray_start_port <= 0) config.xray_start_port = 1083;
        } else if (xray.contains("start_port")) {
            Logger::write("WARNING: config.xray.start_port has wrong type (expected int64), using default", LogLevel::WARN);
        } else {
            config.xray_start_port = 1083;
        }
        if (xray.contains("api_port") && xray.at("api_port").is_int64()) {
            config.xray_api_port = static_cast<int>(xray.at("api_port").as_int64());
        } else if (xray.contains("api_port")) {
            Logger::write("WARNING: config.xray.api_port has wrong type (expected int64), using default", LogLevel::WARN);
        }
    } else if (obj.contains("xray")) {
        Logger::write("WARNING: config.xray has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
