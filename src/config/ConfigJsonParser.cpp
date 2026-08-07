#include <boost/json.hpp>

#include "config/ConfigJsonParser.h"
#include "Logger.h"

namespace config {

boost::json::value ConfigJsonParser::parse(const std::string& jsonStr) {
    boost::json::value jv;
    try {
        jv = boost::json::parse(jsonStr);
    } catch (const std::exception& e) {
        lastError_ = "Failed to parse configuration file.\n\nError:\n" + std::string(e.what());
        return boost::json::value();
    }

    if (!jv.is_object()) {
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
