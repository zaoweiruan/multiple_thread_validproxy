#include "share/VLESSUriBuilder.h"
#include "share/UriCodec.h"

namespace share {

std::string VLESSUriBuilder::build(const db::models::Profileitem& profile) {
    std::string query;

    query += "encryption=none";

    std::string streamsecurity = profile.streamsecurity;
    if (streamsecurity == "tls" || streamsecurity == "reality") {
        query += "&security=" + streamsecurity;
    }
    if (!profile.sni.empty()) {
        query += "&sni=" + profile.sni;
    }
    if (!profile.fingerprint.empty()) {
        query += "&fp=" + profile.fingerprint;
    }
    if (!profile.publickey.empty()) {
        query += "&pbk=" + profile.publickey;
    }
    if (!profile.shortid.empty()) {
        query += "&sid=" + profile.shortid;
    }
    if (!profile.echconfiglist.empty()) {
        query += "&ech=" + UriCodec::urlEncodeRemarksOrPath(profile.echconfiglist);
    }

    if (streamsecurity == "tls" || streamsecurity == "reality") {
        std::string insecureFlag = (profile.allowinsecure == "true" || profile.allowinsecure == "1") ? "1" : "0";
        if (!query.empty()) query += "&";
        query += "insecure=" + insecureFlag + "&allowInsecure=" + insecureFlag;
    }

    if (!profile.flow.empty()) {
        query += "&flow=" + profile.flow;
    }

    query += "&type=" + (profile.network.empty() ? "tcp" : profile.network);

    if (!profile.requesthost.empty()) {
        query += "&host=" + profile.requesthost;
    }

    if (!profile.path.empty()) {
        std::string pathFormatted = UriCodec::urlEncodeRemarksOrPath(profile.path);
        query += "&path=" + pathFormatted;
    }

    std::string result = "vless://" + profile.id + "@" + profile.address + ":" + profile.port + "?" + query;

    if (!profile.remarks.empty()) {
        std::string remarksFormatted = UriCodec::urlEncodeRemarksOrPath(profile.remarks);
        result += "#" + remarksFormatted;
    }

    return result;
}

} // namespace share
