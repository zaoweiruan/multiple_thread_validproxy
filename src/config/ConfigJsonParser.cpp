#include <boost/json.hpp>

#include "config/ConfigJsonParser.h"
#include "Logger.h"

namespace config {

boost::json::value ConfigJsonParser::parse(const std::string& jsonStr) {
    boost::json::value jv;
    try {
        Logger::write("DEBUG: About to parse JSON...", LogLevel::DEBUG);
        jv = boost::json::parse(jsonStr);
        Logger::write("DEBUG: JSON parsed successfully, is_object: "
            + std::to_string(jv.is_object()), LogLevel::DEBUG);
    } catch (const std::exception& e) {
        Logger::write("DEBUG: JSON exception: " + std::string(e.what()), LogLevel::DEBUG);
        lastError_ = "Failed to parse configuration file.\n\nError:\n" + std::string(e.what());
        return boost::json::value();
    }

    if (!jv.is_object()) {
        Logger::write("DEBUG: JSON is not an object", LogLevel::DEBUG);
        lastError_ = "Invalid configuration file.\n\nRoot element must be a JSON object.";
        return boost::json::value();
    }

    lastError_.clear();
    return jv;
}

std::string ConfigJsonParser::lastError() const {
    return lastError_;
}

} // namespace config
