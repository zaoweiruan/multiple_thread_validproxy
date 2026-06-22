#include "service/ConfigService.h"
#include "ConfigReader.h"
#include "Logger.h"

namespace service {

ConfigService::ConfigService() {
}

bool ConfigService::save(const config::AppConfig& cfg) {
    std::string configPath = config::ConfigReader::getDefaultConfigPath();
    return config::ConfigReader::save(configPath, cfg);
}

std::string ConfigService::getDefaultConfigPath() const {
    return config::ConfigReader::getDefaultConfigPath();
}

} // namespace service