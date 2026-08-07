#ifndef CONFIG_GENERATOR_H
#define CONFIG_GENERATOR_H

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <boost/json.hpp>

#include "Profileitem.h"
#include "ProfileExItem.h"

namespace config {

struct XrayConfig {
    std::string outbound_json;
    std::string inbound_json;
    bool configFailed = false;
};

class ConfigGenerator {
public:
    explicit ConfigGenerator(sqlite3* db);
    std::vector<db::models::Profileitem> loadProfiles(const std::string& sqlQuery = "");
    std::vector<db::models::ProfileExItem> loadProfileExItems();
    XrayConfig generateConfig(const db::models::Profileitem& profile);
    bool updateProfileExItem(const db::models::ProfileExItem& exitem);

private:
    sqlite3* db_;
};

} // namespace config

#endif // CONFIG_GENERATOR_H
