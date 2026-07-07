#include "config/outbound/SingBoxHysteria2OutboundBuilder.h"
#include "Profileitem.h"

namespace config {

boost::json::object SingBoxHysteria2OutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;

    outbound["type"] = "hysteria2";
    outbound["tag"] = outboundTag;

    outbound["server"] = p.address;
    outbound["server_port"] = std::stoi(p.port);

    boost::json::object tlsObj;
    tlsObj["enabled"] = true;
    if (!p.sni.empty()) {
        tlsObj["server_name"] = p.sni;
    }
    if (p.allowinsecure == "1") {
        tlsObj["insecure"] = true;
    }
    outbound["tls"] = tlsObj;

    boost::json::object hysteriaObj;
    hysteriaObj["version"] = 2;
    
    // Parse up/down from extra field
    int upMbps = 100;
    int downMbps = 100;
    std::string extra_clean = p.extra;
    if (!extra_clean.empty() && extra_clean.front() == ',') {
        extra_clean.erase(extra_clean.begin());
    }
    if (!extra_clean.empty()) {
        std::string upVal;
        std::string downVal;
        if (extra_clean.find("up=") != std::string::npos) {
            size_t upPos = extra_clean.find("up=") + 3;
            size_t upEnd = extra_clean.find(",", upPos);
            if (upEnd == std::string::npos) upEnd = extra_clean.length();
            upVal = extra_clean.substr(upPos, upEnd - upPos);
            // Convert "mbps" suffix to integer
            if (upVal.find("mbps") != std::string::npos) {
                upMbps = std::stoi(upVal.substr(0, upVal.find("mbps")));
            } else {
                upMbps = std::stoi(upVal);
            }
        }
        if (extra_clean.find("down=") != std::string::npos) {
            size_t downPos = extra_clean.find("down=") + 5;
            size_t downEnd = extra_clean.find(",", downPos);
            if (downEnd == std::string::npos) downEnd = extra_clean.length();
            downVal = extra_clean.substr(downPos, downEnd - downPos);
            if (downVal.find("mbps") != std::string::npos) {
                downMbps = std::stoi(downVal.substr(0, downVal.find("mbps")));
            } else {
                downMbps = std::stoi(downVal);
            }
        }
    }
    outbound["password"] = p.id;
    outbound["up_mbps"] = upMbps;
    outbound["down_mbps"] = downMbps;
    
    // Salamander obfuscation
    if (p.path.empty() && p.extra.find("obfs=salamander") != std::string::npos) {
        outbound["obfs"] = boost::json::object{
            {"type", "salamander"},
            {"password", p.id}
        };
    } else if (!p.path.empty()) {
        outbound["obfs"] = boost::json::object{
            {"type", "salamander"},
            {"password", p.path}
        };
    }
    
    // Note: hysteria object not needed for hysteria2 - settings are at top level
    
    return outbound;
}

} // namespace config