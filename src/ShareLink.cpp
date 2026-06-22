#include "ShareLink.h"
#include "share/ShareLinkFactory.h"
#include "Profileitem.h"

namespace share {

std::string ShareLink::toShareUri(const std::string& configType,
                                   const std::string& address,
                                   const std::string& port,
                                   const std::string& id,
                                   const std::string& security,
                                   const std::string& network,
                                   const std::string& flow,
                                   const std::string& sni,
                                   const std::string& alpn,
                                   const std::string& fingerprint,
                                   const std::string& allowinsecure,
                                   const std::string& path,
                                   const std::string& requesthost,
                                   const std::string& headertype,
                                   const std::string& streamsecurity,
                                   const std::string& remarks,
                                   const std::string& echConfigList,
                                   const std::string& publicKey,
                                   const std::string& shortId) {
    db::models::Profileitem profile;
    profile.configtype = configType;
    profile.address = address;
    profile.port = port;
    profile.id = id;
    profile.security = security;
    profile.network = network;
    profile.flow = flow;
    profile.sni = sni;
    profile.alpn = alpn;
    profile.fingerprint = fingerprint;
    profile.allowinsecure = allowinsecure;
    profile.path = path;
    profile.requesthost = requesthost;
    profile.headertype = headertype;
    profile.streamsecurity = streamsecurity;
    profile.remarks = remarks;
    profile.echconfiglist = echConfigList;
    profile.publickey = publicKey;
    profile.shortid = shortId;

    static ShareLinkFactory factory;
    return factory.toShareUri(profile);
}

}
