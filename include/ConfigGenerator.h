#ifndef CONFIG_GENERATOR_H
#define CONFIG_GENERATOR_H

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <boost/json.hpp>

#include "Profileitem.h"
#include "Profileexitem.h"

namespace config {

struct XrayConfig {
    std::string outbound_json;
    std::string inbound_json;
};

class ConfigGenerator {
public:
    explicit ConfigGenerator(sqlite3* db);
    std::vector<db::models::Profileitem> loadProfiles(const std::string& sqlQuery = "");
    std::vector<db::models::Profileexitem> loadProfileExItems();
    XrayConfig generateConfig(const db::models::Profileitem& profile);
    bool updateProfileExItem(const db::models::Profileexitem& exitem);

private:
    sqlite3* db_;
    boost::json::object buildStreamSettings(const db::models::Profileitem& p);
    boost::json::object buildVLESSOutbound(const db::models::Profileitem& p, const std::string& outboundTag);
    boost::json::object buildVMessOutbound(const db::models::Profileitem& p, const std::string& outboundTag);
    boost::json::object buildSSOutbound(const db::models::Profileitem& p, const std::string& outboundTag);
    boost::json::object buildTrojanOutbound(const db::models::Profileitem& p, const std::string& outboundTag);
};

} // namespace config

#endif // CONFIG_GENERATOR_H
