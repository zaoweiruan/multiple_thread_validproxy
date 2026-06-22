#ifndef SHARE_LINK_H
#define SHARE_LINK_H

#include <string>
#include "ProxyTypeStrings.h"

namespace share {

class ShareLink {
public:
    static std::string toShareUri(const std::string& configType,
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
                                   const std::string& echConfigList = "",
                                   const std::string& publicKey = "",
                                   const std::string& shortId = "");
};

}

#endif // SHARE_LINK_H
