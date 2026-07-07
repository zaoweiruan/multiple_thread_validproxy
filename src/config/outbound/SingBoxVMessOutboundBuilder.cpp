#include "config/outbound/SingBoxVMessOutboundBuilder.h"
#include "Profileitem.h"

namespace config {

boost::json::object SingBoxVMessOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["type"] = "vmess";
    outbound["tag"] = outboundTag;

    outbound["server"] = p.address;
    outbound["server_port"] = std::stoi(p.port);

    boost::json::array usersArr;
    boost::json::object user;
    user["name"] = "user";
    user["uuid"] = p.id;
    user["alterId"] = 0;
    if (!p.security.empty()) {
        user["security"] = p.security;
    } else {
        user["security"] = "auto";
    }
    usersArr.push_back(user);
    outbound["users"] = usersArr;

    // TLS settings
    if (p.streamsecurity == "tls" || p.streamsecurity == "reality") {
        boost::json::object tlsObj;
        tlsObj["enabled"] = true;
        
        if (!p.sni.empty()) {
            tlsObj["server_name"] = p.sni;
        }
        
        if (!p.allowinsecure.empty()) {
            tlsObj["insecure"] = (p.allowinsecure == "1");
        }
        
        outbound["tls"] = tlsObj;
    }

    // Transport settings
    if (p.network == "ws") {
        boost::json::object transport;
        transport["type"] = "ws";
        if (!p.path.empty()) {
            transport["path"] = p.path;
        }
        if (!p.requesthost.empty()) {
            boost::json::object headers;
            headers["Host"] = p.requesthost;
            transport["headers"] = headers;
        }
        outbound["transport"] = transport;
    }

    return outbound;
}

} // namespace config