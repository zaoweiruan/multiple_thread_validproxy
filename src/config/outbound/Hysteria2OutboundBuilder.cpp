#include "config/outbound/Hysteria2OutboundBuilder.h"

namespace config {

boost::json::object Hysteria2OutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "hysteria";

    boost::json::object settings;
    settings["address"] = p.address;
    settings["port"] = std::stoi(p.port);
    settings["version"] = 2;
    outbound["settings"] = settings;

    boost::json::object streamSettings;
    streamSettings["network"] = "hysteria";
    
    bool hasTls = !p.sni.empty() || p.allowinsecure == "1";
    if (hasTls) {
        streamSettings["security"] = "tls";
        boost::json::object tlsSettings;
        if (p.allowinsecure == "1") {
            tlsSettings["allowInsecure"] = true;
        }
        if (!p.sni.empty()) {
            tlsSettings["serverName"] = p.sni;
        }
        streamSettings["tlsSettings"] = tlsSettings;
    }

    boost::json::object hysteriaSettings;
    hysteriaSettings["version"] = 2;
    hysteriaSettings["auth"] = p.id;
    
    std::string upSpeed = "100mbps";
    std::string downSpeed = "100mbps";
    std::string extra_clean = p.extra;
    if (!extra_clean.empty() && extra_clean.front() == ',') {
        extra_clean.erase(extra_clean.begin());
    }
    if (!extra_clean.empty()) {
        if (extra_clean.find("up=") != std::string::npos) {
            size_t upPos = extra_clean.find("up=") + 3;
            size_t upEnd = extra_clean.find(",", upPos);
            if (upEnd == std::string::npos) upEnd = extra_clean.length();
            upSpeed = extra_clean.substr(upPos, upEnd - upPos);
        }
        if (extra_clean.find("down=") != std::string::npos) {
            size_t downPos = extra_clean.find("down=") + 5;
            size_t downEnd = extra_clean.find(",", downPos);
            if (downEnd == std::string::npos) downEnd = extra_clean.length();
            downSpeed = extra_clean.substr(downPos, downEnd - downPos);
        }
    }
    hysteriaSettings["up"] = upSpeed;
    hysteriaSettings["down"] = downSpeed;
    streamSettings["hysteriaSettings"] = hysteriaSettings;

    std::string obfsPassword;
    if (!extra_clean.empty() && extra_clean.find("obfs-password=") != std::string::npos) {
        size_t keyPos = extra_clean.find("obfs-password=");
        size_t pos = keyPos + 14;
        size_t end = extra_clean.find(",", pos);
        if (end == std::string::npos) end = extra_clean.length();
        obfsPassword = extra_clean.substr(pos, end - pos);
    }
    
    if (!p.path.empty() || p.extra.find("obfs=salamander") != std::string::npos) {
        boost::json::object finalmask;
        boost::json::array udpArr;
        boost::json::object salamander;
        salamander["type"] = "salamander";
        boost::json::object salamanderSettings;
        std::string salamanderPwd = obfsPassword.empty() ? p.id : obfsPassword;
        salamanderSettings["password"] = salamanderPwd;
        salamander["settings"] = salamanderSettings;
        udpArr.push_back(salamander);
        finalmask["udp"] = udpArr;
        streamSettings["finalmask"] = finalmask;
    }

    outbound["streamSettings"] = streamSettings;

    boost::json::object mux;
    mux["enabled"] = false;
    outbound["mux"] = mux;
    
    return outbound;
}

} // namespace config
