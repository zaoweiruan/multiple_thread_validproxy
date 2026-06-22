#include "share/SSUriBuilder.h"
#include "share/UriCodec.h"

namespace share {

std::string SSUriBuilder::build(const db::models::Profileitem& profile) {
    std::string userInfo = profile.security + ":" + profile.id;
    std::string encoded = UriCodec::base64Encode(userInfo);
    while (!encoded.empty() && encoded.back() == '=') {
        encoded.pop_back();
    }

    std::string result = "ss://" + encoded + "@" + profile.address + ":" + profile.port;
    std::string query;
    std::string pathStr = profile.path;
    {
        std::string from = "=";
        std::string to = "\\=";
        size_t pos = 0;
        while ((pos = pathStr.find(from, pos)) != std::string::npos) {
            pathStr.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
    std::string pathFormatted = UriCodec::urlEncodeRemarksOrPath(pathStr);

    std::string network = profile.network;
    if (network == "ws" || network == "websocket") {
        query = "plugin=v2ray-plugin";
        query += "%3Bmode%3Dwebsocket";
        if (!profile.sni.empty()) {
            query += "%3Bhost%3D" + profile.sni;
        }
        if (!profile.path.empty()) {
            query += "%3Bpath%3D" + pathFormatted;
        }
        if (profile.streamsecurity == "tls") {
            query += "%3Btls";
        }
        query += "%3Bmux%3D0";
    } else if (!network.empty() && network != "tcp") {
        std::string netOut = network;
        if (netOut == "splithttp") netOut = "xhttp";
        query = "obfs=" + netOut;
        if (!profile.path.empty()) {
            query += "&obfs-path=" + UriCodec::urlEncodeRemarksOrPath(profile.path);
        }
    }

    if (!query.empty()) {
        result += "?" + query;
    }
    if (!profile.remarks.empty()) {
        std::string remarksFormatted = profile.remarks;
        {
            std::string from = " ";
            std::string to = "%20";
            size_t pos = 0;
            while ((pos = remarksFormatted.find(from, pos)) != std::string::npos) {
                remarksFormatted.replace(pos, from.length(), to);
                pos += to.length();
            }
        }
        {
            std::string from = "|";
            std::string to = "%7C";
            size_t pos = 0;
            while ((pos = remarksFormatted.find(from, pos)) != std::string::npos) {
                remarksFormatted.replace(pos, from.length(), to);
                pos += to.length();
            }
        }
        result += "#" + remarksFormatted;
    }
    return result;
}

} // namespace share
