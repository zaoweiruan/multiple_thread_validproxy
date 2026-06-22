#include "config/outbound/WireGuardOutboundBuilder.h"

namespace config {

boost::json::object WireGuardOutboundBuilder::build(const db::models::Profileitem& p, const std::string& outboundTag) const {
    boost::json::object outbound;
    outbound["tag"] = outboundTag;
    outbound["protocol"] = "wireguard";

    boost::json::object settings;

    if (!p.address.empty()) {
        boost::json::array addressArr;
        addressArr.push_back(boost::json::value(p.address));
        settings["localAddresses"] = addressArr;
    }

    settings["privateKey"] = p.id;

    boost::json::array peersArr;
    boost::json::object peer;
    peer["publicKey"] = p.publickey;
    peer["endpoint"] = p.address + ":" + p.port;

    if (!p.presocksport.empty()) {
        boost::json::array reserved;
        reserved.push_back(boost::json::value(p.presocksport));
        peer["reserved"] = reserved;
    }

    peersArr.push_back(peer);
    settings["peers"] = peersArr;

    outbound["settings"] = settings;

    return outbound;
}

} // namespace config
