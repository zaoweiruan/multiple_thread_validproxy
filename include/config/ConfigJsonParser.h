#ifndef CONFIG_JSON_PARSER_H
#define CONFIG_JSON_PARSER_H

#include <string>
#include <boost/json.hpp>

namespace config {

class ConfigJsonParser {
public:
    boost::json::value parse(const std::string& jsonStr);
    std::string lastError() const;

private:
    std::string lastError_;
};

} // namespace config

#endif // CONFIG_JSON_PARSER_H
