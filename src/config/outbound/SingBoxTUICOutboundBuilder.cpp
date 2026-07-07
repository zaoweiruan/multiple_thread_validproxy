#include "config/outbound/SingBoxTUICOutboundBuilder.h"
#include "Profileitem.h"

namespace config {

boost::json::object SingBoxTUICOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["type"] = "tuic";
    outbound["tag"] = outboundTag;

    outbound["server"] = p.address;
    outbound["server_port"] = std::stoi(p.port);
    outbound["uuid"] = p.id;
    outbound["password"] = p.id;

    boost::json::object tlsObj;
    tlsObj["enabled"] = true;
    if (!p.sni.empty()) {
        tlsObj["server_name"] = p.sni;
    }
    if (p.allowinsecure == "1") {
        tlsObj["insecure"] = true;
    }
    outbound["tls"] = tlsObj;

    // TUIC-specific settings (at top level in sing-box)
    outbound["congestion_control"] = "cubic";
    outbound["udp_relay_mode"] = "native";
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
        outbound["alpn"] = alpnArr;
    }
    if (!p.publickey.empty()) {
        outbound["public_key"] = p.publickey;
    }
    
    return outbound;
}

} // namespace config