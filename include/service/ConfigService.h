#ifndef SERVICE_CONFIG_SERVICE_H
#define SERVICE_CONFIG_SERVICE_H

#include <string>
#include "ConfigReader.h"

namespace service {

class ConfigService {
public:
    ConfigService();

    bool save(const config::AppConfig& cfg);
    std::string getDefaultConfigPath() const;

private:
};

} // namespace service

#endif // SERVICE_CONFIG_SERVICE_H