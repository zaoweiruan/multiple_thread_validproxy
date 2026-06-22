#ifndef CONFIG_JSON_SERIALIZER_H
#define CONFIG_JSON_SERIALIZER_H

#include <boost/json.hpp>
#include "ConfigReader.h"

namespace config {

class ConfigJsonSerializer {
public:
    boost::json::object serialize(const AppConfig& config) const;
};

} // namespace config

#endif // CONFIG_CONFIGJSONSERIALIZER_H
