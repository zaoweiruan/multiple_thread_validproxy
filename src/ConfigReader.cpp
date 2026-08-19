#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <windows.h>
#include <functional>
#include <boost/json.hpp>

#include "Logger.h"
#include "ConfigReader.h"
#include "Utils.h"
#include "config/ConfigFileStore.h"
#include "config/ConfigPathResolver.h"
#include "config/ConfigJsonParser.h"
#include "config/ConfigJsonSerializer.h"
#include "config/ConfigValidator.h"
#include "config/sections/DatabaseConfigParser.h"
#include "config/sections/XrayConfigParser.h"
#include "config/sections/TestConfigParser.h"
#include "config/sections/LogConfigParser.h"
#include "config/sections/SubscriptionConfigParser.h"
#include "config/sections/DedupConfigParser.h"
#include "config/sections/NotificationConfigParser.h"
#include "config/sections/SyncConfigParser.h"
#include "config/sections/AutoTaskConfigParser.h"
#include "config/sections/NetworkMonitorConfigParser.h"
#include "config/sections/ProxyConfigParser.h"
#include "config/sections/ProxyProcessMonitorConfigParser.h"

namespace config {

ConfigReader::ErrorReporter ConfigReader::errorReporter_ = [](const std::string& title, const std::string& message) {
    MessageBoxA(NULL, message.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
};

std::string ConfigReader::getDefaultConfigPath() {
    return ConfigPathResolver::detectExeDir() + "\\config.json";
}

std::optional<AppConfig> ConfigReader::load(const std::string& configPath) {
    // Step 1: Read file via ConfigFileStore
    ConfigFileStore fileStore;
    std::string content;
    try {
        content = fileStore.read(configPath);
    } catch (const std::runtime_error& e) {
        errorReporter_("Configuration Error", e.what());
        return std::nullopt;
    }

    // Step 2: Parse JSON via ConfigJsonParser
    ConfigJsonParser jsonParser;
    boost::json::value jv = jsonParser.parse(content);
    if (jv.is_null()) {
        std::string errMsg = "Failed to parse configuration file.\n\n";
        errMsg += "Path:\n" + configPath + "\n\n";
        errMsg += "Error:\n" + jsonParser.lastError();
        errorReporter_("Configuration Error", errMsg);
        return std::nullopt;
    }

    // Step 3: Get exeDir for path resolution
    std::string exeDir = utils::getExecutableDir();

    // Step 4: Parse each config section via section parsers
    AppConfig config;

    DatabaseConfigParser().parse(jv, config, exeDir);
    XrayConfigParser().parse(jv, config, exeDir);
    TestConfigParser().parse(jv, config, exeDir);
    LogConfigParser().parse(jv, config, exeDir);
    SubscriptionConfigParser().parse(jv, config, exeDir);
    DedupConfigParser().parse(jv, config, exeDir);
    NotificationConfigParser().parse(jv, config, exeDir);
    SyncConfigParser().parse(jv, config, exeDir);
    AutoTaskConfigParser().parse(jv, config, exeDir);
    NetworkMonitorConfigParser().parse(jv, config, exeDir);
    ProxyConfigParser().parse(jv, config, exeDir);
    ProxyProcessMonitorConfigParser().parse(jv, config);

    // Step 5: Log SQL queries
    if (!config.sql_query.empty())
        Logger::write("SQL query: " + config.sql_query, LogLevel::DEBUG);
    if (!config.sql_by_subid.empty())
        Logger::write("SQL by_subid: " + config.sql_by_subid, LogLevel::DEBUG);

    // Step 6: Validate via ConfigValidator
    ConfigValidator validator;
    ConfigValidator::ValidationResult validationResult = validator.validate(config, configPath);

    if (!validationResult.valid) {
        for (size_t i = 0; i < validationResult.errors.size(); ++i) {
            Logger::write("ERROR: " + validationResult.errors[i], LogLevel::ERR);
        }
        std::string errMsg;
        if (!validationResult.errors.empty()) {
            errMsg = validationResult.errors[0] + "\n\n";
            errMsg += "Config path:\n" + configPath + "\n\n";
            errMsg += "The application cannot start.";
            errorReporter_("Configuration Error", errMsg);
            return std::nullopt;
        }
    }

    for (size_t i = 0; i < validationResult.warnings.size(); ++i) {
        Logger::write("WARNING: " + validationResult.warnings[i], LogLevel::WARN);
    }

    return config;
}

bool ConfigReader::save(const std::string& configPath, const AppConfig& config) {
    // Step 1: Serialize via ConfigJsonSerializer
    ConfigJsonSerializer serializer;
    boost::json::object root = serializer.serialize(config);

    // Step 2: Write via ConfigFileStore
    ConfigFileStore fileStore;
    try {
        fileStore.write(configPath, boost::json::serialize(root));
    } catch (const std::runtime_error& e) {
        Logger::write("Failed to write config to: " + configPath, LogLevel::ERR);
        return false;
    }

    return true;
}

} // namespace config
