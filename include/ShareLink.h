#ifndef SHARE_LINK_H
#define SHARE_LINK_H

#include <string>
#include <map>

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
                                   const std::string& shortId = "");;

private:
    static std::string vmessToUri(const std::string& address,
                                  const std::string& port,
                                  const std::string& id,
                                  const std::string& alterid,
                                  const std::string& security,
                                  const std::string& network,
                                  const std::string& headertype,
                                  const std::string& requesthost,
                                  const std::string& path,
                                  const std::string& streamsecurity,
                                  const std::string& sni,
                                  const std::string& alpn,
                                  const std::string& fingerprint,
                                  const std::string& allowinsecure,
                                  const std::string& remarks);

    static std::string vlessToUri(const std::string& address,
                                  const std::string& port,
                                  const std::string& id,
                                  const std::string& flow,
                                  const std::string& network,
                                  const std::string& headertype,
                                  const std::string& requesthost,
                                  const std::string& path,
                                  const std::string& streamsecurity,
                                  const std::string& sni,
                                  const std::string& alpn,
                                  const std::string& fingerprint,
                                  const std::string& allowinsecure,
                                  const std::string& remarks,
                                  const std::string& echConfigList = "",
                                  const std::string& publicKey = "",
                                  const std::string& shortId = "");

    static std::string trojanToUri(const std::string& address,
                                    const std::string& port,
                                    const std::string& id,
                                    const std::string& flow,
                                    const std::string& network,
                                    const std::string& headertype,
                                    const std::string& requesthost,
                                    const std::string& path,
                                    const std::string& streamsecurity,
                                    const std::string& sni,
                                    const std::string& alpn,
                                    const std::string& fingerprint,
                                    const std::string& allowinsecure,
                                    const std::string& remarks);

    static std::string ssToUri(const std::string& address,
                                const std::string& port,
                                const std::string& method,
                                const std::string& password,
                                const std::string& network,
                                const std::string& path,
                                const std::string& streamsecurity,
                                const std::string& sni,
                                const std::string& remarks);

    static std::string hysteria2ToUri(const std::string& address,
                                       const std::string& port,
                                       const std::string& password,
                                       const std::string& sni,
                                       const std::string& alpn,
                                       const std::string& fingerprint,
                                       const std::string& path,
                                       const std::string& remarks);

    static std::string urlEncode(const std::string& str);
    static std::string base64Encode(const std::string& str);
    static std::string base64Decode(const std::string& str);
    static std::string buildQueryString(const std::map<std::string, std::string>& params);
    static bool isValidIpv6(const std::string& addr);
    static std::string formatIpv6(const std::string& addr);
};

std::string getConfigTypeName(int configType);

}

#endif // SHARE_LINK_H