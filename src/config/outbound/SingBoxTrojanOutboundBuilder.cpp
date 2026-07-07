#include "config/outbound/SingBoxTrojanOutboundBuilder.h"
#include "Profileitem.h"

namespace config {

boost::json::object SingBoxTrojanOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["type"] = "trojan";
    outbound["tag"] = outboundTag;

    outbound["server"] = p.address;
    outbound["server_port"] = std::stoi(p.port);
    outbound["password"] = p.id;

    // TLS settings (trojan typically uses TLS)
    if (p.streamsecurity == "tls" || p.streamsecurity == "reality") {
        boost::json::object tlsObj;
        tlsObj["enabled"] = true;
        
        if (!p.sni.empty()) {
            tlsObj["server_name"] = p.sni;
        }
        
        if (!p.allowinsecure.empty()) {
            tlsObj["insecure"] = (p.allowinsecure == "1");
        }
        if (!p.alpn.empty()) {
            boost::json::array alpnArr;
            std::string alpnStr = p.alpn;
            size_t pos = 0;
            while ((pos = alpnStr.find(',')) != std::string::npos) {
                alpnArr.push_back(boost::json::value(alpnStr.substr(0, pos)));
                alpnStr = alpnStr.substr(pos + 1);
            }
            if (!alpnStr.empty()) {
                alpnArr.push_back(boost::json::value(alpnStr));
            }
            tlsObj["alpn"] = alpnArr;
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