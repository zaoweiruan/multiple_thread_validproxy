#include "ConfigGenerator.h"
#include "config/ProfileConfigRepository.h"
#include "i18n/I18n.h"
#include "config/OutboundBuilderFactory.h"
#include "config/XrayConfigAssembler.h"
#include "config/ConfigBuilder.h"
#include "Profileitem.h"
#include "Logger.h"
#include <stdexcept>

namespace config {

ConfigGenerator::ConfigGenerator(sqlite3* db) : db_(db) {}

std::vector<db::models::Profileitem> ConfigGenerator::loadProfiles(const std::string& sqlQuery) {
    ProfileConfigRepository repo(db_);
    return repo.loadProfiles(sqlQuery);
}

std::vector<db::models::ProfileExItem> ConfigGenerator::loadProfileExItems() {
    ProfileConfigRepository repo(db_);
    return repo.loadProfileExItems();
}

bool ConfigGenerator::updateProfileExItem(const db::models::ProfileExItem& exitem) {
    ProfileConfigRepository repo(db_);
    return repo.updateProfileExItem(exitem);
}

XrayConfig ConfigGenerator::generateConfig(const db::models::Profileitem& profile) {
    // Use ConfigBuilder when DB is available
    if (db_) {
        config::ConfigBuilder builder(db_);
        config::ConfigVariables vars;
        vars.socksPort = 10808;
        vars.apiPort = 10085;
        boost::json::value configJson = builder.build(config::ConfigMode::Standalone, profile, vars);
        XrayConfig config;
        config.outbound_json = builder.serialize(configJson);
        return config;
    }
    
    // Fallback: legacy JSON generation (no DB required)
    if (profile.address.empty()) {
        throw std::runtime_error(i18n::u8(i18n::StrId::ConfigGenerator_ProxyAddressMustEmpty));
    }
    if (std::stoi(profile.port) <= 0 || std::stoi(profile.port) > 65535) {
        throw std::runtime_error(i18n::u8(i18n::StrId::ConfigGenerator_InvalidPortNumber) + profile.port);
    }
    if (profile.id.empty()) {
        throw std::runtime_error(i18n::u8(i18n::StrId::ConfigGenerator_ProxyIDPasswordMust));
    }
    
    if (profile.streamsecurity == "reality") {
        if (profile.publickey.empty()) {
            throw std::runtime_error(i18n::u8(i18n::StrId::ConfigGenerator_REALITYConfigurationMissingPublicKey));
        }
        if (profile.sni.empty()) {
            throw std::runtime_error(i18n::u8(i18n::StrId::ConfigGenerator_REALITYConfigurationMissingSni));
        }
    }
    
    OutboundBuilderFactory factory;
    boost::json::object outbound = factory.create(profile, "proxy");
    
    XrayConfigAssembler assembler;
    boost::json::object root = assembler.assemble(outbound);
    
    XrayConfig config;
    config.outbound_json = boost::json::serialize(root);
    return config;
}

} // namespace config