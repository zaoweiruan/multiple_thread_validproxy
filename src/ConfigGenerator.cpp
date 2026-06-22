#include "ConfigGenerator.h"
#include "config/ProfileConfigRepository.h"
#include "config/OutboundBuilderFactory.h"
#include "config/XrayConfigAssembler.h"
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
    if (profile.address.empty()) {
        throw std::runtime_error("代理地址不能为空");
    }
    if (std::stoi(profile.port) <= 0 || std::stoi(profile.port) > 65535) {
        throw std::runtime_error("无效的端口号: " + profile.port);
    }
    if (profile.id.empty()) {
        throw std::runtime_error("代理ID/密码不能为空");
    }

    if (profile.streamsecurity == "reality") {
        if (profile.publickey.empty()) {
            throw std::runtime_error("REALITY配置缺少publicKey");
        }
        if (profile.sni.empty()) {
            throw std::runtime_error("REALITY配置缺少sni(serverName)");
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
