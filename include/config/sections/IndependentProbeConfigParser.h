#ifndef CONFIG_SECTIONS_INDEPENDENT_PROBE_H
#define CONFIG_SECTIONS_INDEPENDENT_PROBE_H

#include <boost/json.hpp>
#include "ConfigReader.h"
#include "Logger.h"

namespace config {

class IndependentProbeConfigParser {
public:
    void parse(const boost::json::value& root, AppConfig& config);
};

inline void IndependentProbeConfigParser::parse(const boost::json::value& root, AppConfig& config) {
    if (!root.is_object()) return;
    const boost::json::object& obj = root.as_object();

    if (obj.contains("independent_probe") && obj.at("independent_probe").is_object()) {
        const boost::json::object& ip = obj.at("independent_probe").as_object();

        if (ip.contains("enabled") && ip.at("enabled").is_bool()) {
            config.independent_probe.enabled = ip.at("enabled").as_bool();
        } else if (ip.contains("enabled")) {
            Logger::write("WARNING: config.independent_probe.enabled has wrong type (expected bool), using default", LogLevel::WARN);
        }
    } else if (obj.contains("independent_probe")) {
        Logger::write("WARNING: config.independent_probe has wrong type (expected object), using default", LogLevel::WARN);
    }
}

} // namespace config
#endif