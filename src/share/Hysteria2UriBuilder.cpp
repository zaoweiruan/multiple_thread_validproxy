#include "share/Hysteria2UriBuilder.h"
#include "share/UriCodec.h"
#include <map>

namespace share {

std::string Hysteria2UriBuilder::build(const db::models::Profileitem& profile) {
    std::map<std::string, std::string> params;

    if (!profile.sni.empty()) params["sni"] = profile.sni;
    if (!profile.alpn.empty()) params["alpn"] = profile.alpn;
    if (!profile.fingerprint.empty()) params["fp"] = profile.fingerprint;
    if (!profile.path.empty()) params["path"] = profile.path;

    std::string query = UriCodec::buildQueryString(params);
    std::string result = "hy2://" + profile.id + "@" + profile.address + ":" + profile.port;
    if (!query.empty()) {
        result += "?" + query;
    }
    if (!profile.remarks.empty()) {
        result += "#" + UriCodec::urlEncodeStandard(profile.remarks);
    }
    return result;
}

} // namespace share
