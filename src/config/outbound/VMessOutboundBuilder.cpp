#include "config/outbound/VMessOutboundBuilder.h"
#include "config/StreamSettingsBuilder.h"

namespace config {

boost::json::object VMessOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "vmess";

    boost::json::object settings;
    boost::json::array vnextArr;
    boost::json::object vnext;

    vnext["address"] = p.address;
    vnext["port"] = std::stoi(p.port);

    boost::json::array usersArr;
    boost::json::object user;
    if (!p.id.empty()) {
        user["id"] = p.id;
    }
    user["alterId"] = 0;
    user["security"] = p.security.empty() ? "auto" : p.security;
    usersArr.push_back(user);

    vnext["users"] = usersArr;
    vnextArr.push_back(vnext);
    settings["vnext"] = vnextArr;
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
