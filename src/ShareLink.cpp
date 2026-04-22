#include "ShareLink.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <iostream>

namespace share {

static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

std::string ShareLink::base64Encode(const std::string& str) {
    std::string result;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    int len = str.length();
    int pos = 0;
    
    while (len--) {
        char_array_3[i++] = str[pos++];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++) result += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = 0;
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        if (i == 1) {
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2);
        } else {
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
        }
        for (j = 0; j < i + 1; j++) result += base64_chars[char_array_4[j]];
        while(i++ < 3) result += '=';
    }
    
    return result;
}

std::string ShareLink::base64Decode(const std::string& str) {
    std::string result;
    int i = 0;
    int j = 0;
    int len = str.length();
    unsigned char char_array_4[4];
    unsigned char char_array_3[3];
    std::string base64_table = base64_chars;
    
    while (len-- && (str[j] != '=') && (std::isalnum(str[j]) || str[j] == '+' || str[j] == '/')) {
        char_array_4[i++] = str[j++];
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                char_array_4[i] = (unsigned char)base64_table.find(char_array_4[i]);
            }
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6);
            for (i = 0; i < 3; i++) result += char_array_3[i];
            i = 0;
        }
    }
    
    if (i) {
        for (j = 0; j < i; j++) {
            char_array_4[j] = (unsigned char)base64_table.find(char_array_4[j]);
        }
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        for (j = 0; j < i - 1; j++) result += char_array_3[j];
    }
    
    return result;
}

std::string ShareLink::urlEncode(const std::string& str) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex << std::uppercase;
    
    for (char c : str) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::setw(2) << int((unsigned char)c);
        }
    }
    return escaped.str();
}

std::string ShareLink::buildQueryString(const std::map<std::string, std::string>& params) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!value.empty()) {
            if (!first) oss << "&";
            oss << urlEncode(key) << "=" << urlEncode(value);
            first = false;
        }
    }
    return oss.str();
}

bool ShareLink::isValidIpv6(const std::string& addr) {
    return addr.find(':') != std::string::npos && addr.find('[') != std::string::npos;
}

std::string ShareLink::formatIpv6(const std::string& addr) {
    std::string result = addr;
    if (result.front() != '[') result = "[" + result;
    if (result.back() != ']') result += "]";
    return result;
}

std::string ShareLink::vmessToUri(const std::string& address,
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
                                const std::string& remarks) {
    std::string json;
    
    if (id.length() > 100) {
        std::string decoded = base64Decode(id);
        if (!decoded.empty() && decoded.find("\"v\":") != std::string::npos) {
            json = decoded;
        } else {
            json = id;
        }
    } else {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"v\": \"2\",\n";
        oss << "  \"ps\": \"" << remarks << "\",\n";
        oss << "  \"add\": \"" << address << "\",\n";
        oss << "  \"port\": \"" << port << "\",\n";
        oss << "  \"id\": \"" << id << "\",\n";
        oss << "  \"aid\": \"" << alterid << "\",\n";
        oss << "  \"scy\": \"" << security << "\",\n";
        oss << "  \"net\": \"" << network << "\",\n";
        oss << "  \"type\": \"" << headertype << "\",\n";
        oss << "  \"host\": \"" << requesthost << "\",\n";
        oss << "  \"path\": \"" << path << "\",\n";
        oss << "  \"tls\": \"" << streamsecurity << "\",\n";
        oss << "  \"sni\": \"" << sni << "\",\n";
        oss << "  \"alpn\": \"" << alpn << "\",\n";
        oss << "  \"fp\": \"" << fingerprint << "\",\n";
        oss << "  \"insecure\": \"" << (allowinsecure.empty() ? "0" : allowinsecure) << "\"\n";
        oss << "}";
        json = oss.str();
    }
    
    std::string b64 = base64Encode(json);
    return "vmess://" + b64 + "@" + address + ":" + port;
}

std::string ShareLink::vlessToUri(const std::string& address,
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
                                const std::string& remarks) {
    std::map<std::string, std::string> params;
    
    if (!flow.empty()) params["flow"] = flow;
    if (!network.empty()) params["type"] = (network == "tcp" || network.empty()) ? "tcp" : network;
    if (!headertype.empty()) params["headerType"] = headertype;
    if (!sni.empty()) params["sni"] = sni;
    if (!alpn.empty()) params["alpn"] = alpn;
    if (!fingerprint.empty()) params["fp"] = fingerprint;
    if (!path.empty()) params["path"] = path;
    if (!requesthost.empty()) params["host"] = requesthost;
    if (!remarks.empty()) params["remark"] = remarks;
    if (streamsecurity == "tls") {
        params["security"] = "tls";
    } else if (streamsecurity == "reality") {
        params["security"] = "reality";
    }
    
    std::string query = buildQueryString(params);
    return "vless://" + id + "@" + address + ":" + port + (query.empty() ? "" : "?" + query);
}

std::string ShareLink::trojanToUri(const std::string& address,
                                   const std::string& port,
                                   const std::string& password,
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
                                   const std::string& remarks) {
    std::map<std::string, std::string> params;
    
    if (!flow.empty()) params["flow"] = flow;
    if (!network.empty()) params["type"] = (network == "tcp" || network.empty()) ? "tcp" : network;
    if (!headertype.empty()) params["headerType"] = headertype;
    if (!sni.empty()) params["sni"] = sni;
    if (!alpn.empty()) params["alpn"] = alpn;
    if (!fingerprint.empty()) params["fp"] = fingerprint;
    if (!path.empty()) params["path"] = path;
    if (!requesthost.empty()) params["host"] = requesthost;
    if (!remarks.empty()) params["remark"] = remarks;
    if (streamsecurity == "tls") {
        params["security"] = "tls";
    } else if (streamsecurity == "reality") {
        params["security"] = "reality";
    }
    
    std::string query = buildQueryString(params);
    return "trojan://" + password + "@" + address + ":" + port + (query.empty() ? "" : "?" + query);
}

std::string ShareLink::ssToUri(const std::string& address,
                                const std::string& port,
                                const std::string& method,
                                const std::string& password,
                                const std::string& network,
                                const std::string& path,
                                const std::string& streamsecurity,
                                const std::string& sni,
                                const std::string& remarks) {
    std::string userInfo = method + ":" + password;
    std::string encoded = base64Encode(userInfo);
    
    std::string result = "ss://" + encoded + "@" + address + ":" + port;
    std::string query;
    std::string pathFormatted = path;
    replaceAll(pathFormatted, ";", "%3B");
    replaceAll(pathFormatted, "=", "%3D");
    
    if (network == "ws" || network == "websocket") {
        query = "plugin=v2ray-plugin";
        query += "%3Bmode%3Dwebsocket";
        if (!sni.empty()) {
            query += "%3Bhost%3D" + sni;
        }
        if (!path.empty()) {
            query += "%3Bpath%3D" + pathFormatted;
        }
        if (streamsecurity == "tls") {
            query += "%3Btls";
        }
        query += "%3Bmux%3D0";
    } else if (!network.empty() && network != "tcp") {
        query = "obfs=" + network;
        if (!path.empty()) {
            query += "&obfs-path=" + urlEncode(path);
        }
    }
    
    if (!query.empty()) {
        result += "?" + query;
    }
    if (!remarks.empty()) {
        result += "#" + remarks;
    }
    return result;
}

std::string ShareLink::hysteria2ToUri(const std::string& address,
                                       const std::string& port,
                                       const std::string& password,
                                       const std::string& sni,
                                       const std::string& alpn,
                                       const std::string& fingerprint,
                                       const std::string& path,
                                       const std::string& remarks) {
    std::map<std::string, std::string> params;
    
    if (!sni.empty()) params["sni"] = sni;
    if (!alpn.empty()) params["alpn"] = alpn;
    if (!fingerprint.empty()) params["fp"] = fingerprint;
    if (!path.empty()) params["path"] = path;
    
    std::string query = buildQueryString(params);
    std::string result = "hy2://" + password + "@" + address + ":" + port;
    if (!query.empty()) {
        result += "?" + query;
    }
    if (!remarks.empty()) {
        result += "#" + urlEncode(remarks);
    }
    return result;
}

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
                                 const std::string& remarks) {
int type = std::stoi(configType);
    
    // v2rayN ConfigType: 1=VMess, 3=SS, 4=Socks, 5=VLESS, 6=Trojan, 7=Hysteria2, 8=TUIC, 9=WireGuard, 10=HTTP
    switch (type) {
        case 1: // VMess
            return vmessToUri(address, port, id, "0", security, network, headertype,
                           requesthost, path, streamsecurity, sni, alpn, fingerprint,
                           allowinsecure, remarks);
        case 3: // Shadowsocks
            return ssToUri(address, port, security, id, network, path,
                         streamsecurity, sni, remarks);
        case 4: // Socks - not supported
            std::cerr << "Socks not supported: " << configType << std::endl;
            return "";
        case 5: // VLESS
            return vlessToUri(address, port, id, flow, network, headertype,
                           requesthost, path, streamsecurity, sni, alpn, fingerprint,
                           allowinsecure, remarks);
        case 6: // Trojan
            return trojanToUri(address, port, id, flow, network, headertype,
                             requesthost, path, streamsecurity, sni, alpn, fingerprint,
                             allowinsecure, remarks);
        case 7: // Hysteria2
            return hysteria2ToUri(address, port, id, sni, alpn, fingerprint, path, remarks);
        case 8: // TUIC
            return vlessToUri(address, port, id, flow, network, headertype,
                           requesthost, path, streamsecurity, sni, alpn, fingerprint,
                           allowinsecure, remarks);
        case 9: // WireGuard - not supported
            std::cerr << "WireGuard not supported: " << configType << std::endl;
            return "";
        case 10: // HTTP - not supported
            std::cerr << "HTTP not supported: " << configType << std::endl;
            return "";
        default:
            std::cerr << "Unsupported config type: " << configType << std::endl;
            return "";
    }
}

std::string getConfigTypeName(int configType) {
    switch (configType) {
        case 1: return "VMess";
        case 2: return "Custom";
        case 3: return "Shadowsocks";
        case 4: return "Socks";
        case 5: return "VLESS";
        case 6: return "Trojan";
        case 7: return "Hysteria2";
        case 8: return "TUIC";
        case 9: return "WireGuard";
        case 10: return "HTTP";
        case 11: return "Anytls";
        default: return "Unknown";
    }
}

}