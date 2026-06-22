#include "share/TrojanUriBuilder.h"
#include "share/UriCodec.h"

namespace share {

std::string TrojanUriBuilder::build(const db::models::Profileitem& profile) {
    std::string query;

    if (profile.streamsecurity == "tls" || profile.streamsecurity == "reality") {
        query += "security=" + profile.streamsecurity;
    }
    if (!profile.sni.empty()) {
        if (!query.empty()) query += "&";
        query += "sni=" + profile.sni;
    }
    if (!profile.fingerprint.empty()) {
        if (!query.empty()) query += "&";
        query += "fp=" + profile.fingerprint;
    }

    std::string insecureFlag = (profile.allowinsecure == "true" || profile.allowinsecure == "1") ? "1" : "0";
    if (!query.empty()) query += "&";
    query += "insecure=" + insecureFlag + "&allowInsecure=" + insecureFlag;

    std::string net = profile.network.empty() ? "tcp" : profile.network;
    if (net != "tcp") {
        query += "&type=" + net;
    }

    if (!profile.requesthost.empty()) {
        query += "&host=" + profile.requesthost;
    }

    if (!profile.path.empty()) {
        std::string pathFormatted = UriCodec::urlEncodeRemarksOrPath(profile.path);
        query += "&path=" + pathFormatted;
    }

    std::string result = "trojan://" + profile.id + "@" + profile.address + ":" + profile.port + "?" + query;

    if (!profile.remarks.empty()) {
        std::string remarksFormatted = UriCodec::urlEncodeRemarksOrPath(profile.remarks);
        result += "#" + remarksFormatted;
    }

    return result;
}

} // namespace share
