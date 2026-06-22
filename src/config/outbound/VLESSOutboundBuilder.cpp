#include "config/outbound/VLESSOutboundBuilder.h"
#include "config/StreamSettingsBuilder.h"

namespace config {

boost::json::object VLESSOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "vless";

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
    user["email"] = "t@t.tt";
    user["encryption"] = "none";

    std::string flowValue = p.flow;
    if (!flowValue.empty()) {
        if (flowValue.find("xtls") != std::string::npos || flowValue.find("vision") != std::string::npos) {
            if (p.streamsecurity != "reality" && !p.publickey.empty()) {
                flowValue = "";
            }
        }
        user["flow"] = flowValue;
    }

    if (!p.security.empty() && p.security != "none") {
        user["security"] = p.security;
    }
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
