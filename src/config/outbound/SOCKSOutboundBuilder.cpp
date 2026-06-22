#include "config/outbound/SOCKSOutboundBuilder.h"

namespace config {

boost::json::object SOCKSOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "socks";

    boost::json::object settings;
    boost::json::array serversArr;
    boost::json::object server;
    server["address"] = p.address;
    server["port"] = std::stoi(p.port);
    
    if (!p.security.empty() && !p.id.empty()) {
        boost::json::object user;
        user["user"] = p.security;
        user["pass"] = p.id;
        boost::json::array usersArr;
        usersArr.push_back(user);
        server["users"] = usersArr;
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
