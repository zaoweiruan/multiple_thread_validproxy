#include "config/outbound/HTTPOutboundBuilder.h"

namespace config {

boost::json::object HTTPOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "http";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;
    server["address"] = p.address;
    server["port"] = std::stoi(p.port);

    if (!p.security.empty() && !p.id.empty()) {
        server["user"] = p.security;
        server["pass"] = p.id;
    }

    if (!p.requesthost.empty()) {
        server["headers"] = boost::json::object({ {"host", p.requesthost} });
    }

    serversArr.push_back(server);
    settings["servers"] = serversArr;
    outbound["settings"] = settings;

    if (!p.streamsecurity.empty() && (p.streamsecurity == "tls" || p.streamsecurity == "reality")) {
        boost::json::object streamSettings;
        streamSettings["security"] = p.streamsecurity;
        if (!p.streamsecurity.empty()) {
            if (p.streamsecurity == "tls" || p.streamsecurity == "reality") {
                boost::json::object tlsSettings;
                tlsSettings["allowInsecure"] = (p.allowinsecure == "1");
                if (!p.sni.empty()) {
                    tlsSettings["serverName"] = p.sni;
                }
                streamSettings["tlsSettings"] = tlsSettings;
            }
        }
        outbound["streamSettings"] = streamSettings;
    }

    return outbound;
}

} // namespace config
