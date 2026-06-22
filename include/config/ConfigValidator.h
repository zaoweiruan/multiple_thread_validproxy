#ifndef CONFIG_VALIDATOR_H
#define CONFIG_VALIDATOR_H

#include <string>
#include <vector>
#include "ConfigReader.h"

namespace config {

class ConfigValidator {
public:
    struct ValidationResult {
        bool valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    ValidationResult validate(const AppConfig& config, const std::string& configPath);
};

} // namespace config

#endif // CONFIG_VALIDATOR_H
