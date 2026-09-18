#ifndef CONFIG_GENERATOR_H
#define CONFIG_GENERATOR_H

#include <string>
#include <vector>
#include <memory>
#include <sqlite3.h>
#include <boost/json.hpp>

#include "Profileitem.h"
#include "ProfileExItem.h"
#include "ConfigReader.h"

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

    // Build the full xray config JSON for the standalone proxy pool process.
    // Members (tag px-<indexId>) are injected at runtime via XrayApi::addOutbound;
    // this only generates the static control plane (api services, socks mixed
    // inbound, direct outbound, observatory, and routing.balancers). The balancer
    // is declared under routing.balancers (xray 26.x); the legacy outbound
    // protocol "balancing" is no longer registered. The socks inbound uses a
    // plain `listen` address plus a separate `port` (xray rejects the combined
    // "address:port" form in an inbound's `listen` field).
    static std::string buildPoolConfig(int socksPort, int apiPort, const StandalonePoolConfig& cfg);

private:
    sqlite3* db_;
};

} // namespace config

#endif // CONFIG_GENERATOR_H
