#include "config/outbound/SSOutboundBuilder.h"
#include "config/StreamSettingsBuilder.h"

namespace config {

boost::json::object SSOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["tag"] = outboundTag;
    outbound["protocol"] = "shadowsocks";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;

    server["address"] = p.address;
    server["port"] = std::stoi(p.port);
    server["method"] = p.security;
    server["password"] = p.id;

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
