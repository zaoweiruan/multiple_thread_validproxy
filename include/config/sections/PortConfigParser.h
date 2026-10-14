#ifndef CONFIG_SECTIONS_PORT_H
#define CONFIG_SECTIONS_PORT_H

#include <boost/json.hpp>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

// Parses the `port` section. Two-argument form (no exeDir): the section holds
// no path-like values, so there is nothing to resolve against the executable
// directory. Follows the same warning convention as the other 14 section
// parsers: a present-but-wrong-typed field keeps its default and logs a WARN.
class PortConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config);
};

inline void PortConfigParser::parse(const boost::json::value& root, AppConfig& config) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("port") && obj.at("port").is_object()) {
        const boost::json::object& port = obj.at("port").as_object();

        if (port.contains("time_wait_blocking") && port.at("time_wait_blocking").is_bool()) {
            config.port.time_wait_blocking = port.at("time_wait_blocking").as_bool();
        } else if (port.contains("time_wait_blocking")) {
            Logger::write("WARNING: config.port.time_wait_blocking has wrong type (expected bool), using default", LogLevel::WARN);
        }
    } else if (obj.contains("port")) {
        Logger::write("WARNING: config.port has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif
