#include "config/outbound/TUICOutboundBuilder.h"

namespace config {

boost::json::object TUICOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "tuic";

    boost::json::object settings;
    settings["address"] = p.address;
    settings["port"] = std::stoi(p.port);
    settings["password"] = p.id;
    settings["uuid"] = p.id;

    if (!p.sni.empty()) {
        settings["sni"] = p.sni;
    }

    if (!p.alpn.empty()) {
        std::vector<std::string> alpnList;
        std::stringstream ss(p.alpn);
        std::string item;
        while (std::getline(ss, item, ',')) {
            alpnList.push_back(item);
        }
        boost::json::array alpnArr;
        for (const std::string& a : alpnList) {
            alpnArr.push_back(boost::json::value(a));
        }
        settings["alpn"] = alpnArr;
    }

    if (p.allowinsecure == "1") {
        settings["allowinsecure"] = true;
    }

    if (!p.publickey.empty()) {
        settings["publicKey"] = p.publickey;
    }

    outbound["settings"] = settings;

    return outbound;
}

} // namespace config
