#include "config/outbound/SingBoxSSOutboundBuilder.h"
#include "Profileitem.h"

namespace config {

boost::json::object SingBoxSSOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["type"] = "shadowsocks";
    outbound["tag"] = outboundTag;

    outbound["server"] = p.address;
    outbound["server_port"] = std::stoi(p.port);
    outbound["method"] = p.security;
    outbound["password"] = p.id;

    // Plugin support (ss2022/obfs)
    if (p.network == "ws" || p.network == "h2" || !p.path.empty()) {
        boost::json::object plugin;
        plugin["enabled"] = true;
        if (p.network == "ws") {
            plugin["type"] = "obfs";
            plugin["mode"] = "websocket";
            if (!p.path.empty()) {
                plugin["path"] = p.path;
            }
            if (!p.requesthost.empty()) {
                plugin["host"] = p.requesthost;
            }
        }
        outbound["plugin"] = plugin;
    }

    return outbound;
}

} // namespace config