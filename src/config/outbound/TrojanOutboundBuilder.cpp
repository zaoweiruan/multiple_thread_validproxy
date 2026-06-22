#include "config/outbound/TrojanOutboundBuilder.h"
#include "config/StreamSettingsBuilder.h"

namespace config {

boost::json::object TrojanOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "trojan";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;

    server["address"] = p.address;
    server["port"] = std::stoi(p.port);
    server["password"] = p.id;
    
    if (!p.security.empty()) {
        server["method"] = p.security;
    } else {
        server["method"] = "chacha20";
    }
    
    server["ota"] = false;
    server["level"] = 1;

    serversArr.push_back(server);
    settings["servers"] = serversArr;
    outbound["settings"] = settings;

    StreamSettingsBuilder streamBuilder;
    boost::json::object streamSettings = streamBuilder.build(p);
    outbound["streamSettings"] = streamSettings;

    boost::json::object mux;
    mux["enabled"] = p.muxEnabled == 1;
    mux["concurrency"] = -1;
    outbound["mux"] = mux;

    return outbound;
}

} // namespace config
